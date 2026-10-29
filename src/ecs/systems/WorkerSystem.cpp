#include "WorkerSystem.h"
#include "ecs/components/Worker.h"
#include "ecs/components/Resource.h"
#include "ecs/components/Transform.h"
#include "ecs/components/Path.h"
#include "ecs/components/Building.h"
#include "ecs/components/Mortal.h"
#include "ecs/components/Identity.h"
#include "core/ConfigManager.h"
#include "core/FixedMath.h"
#include "ecs/components/Health.h"
#include "ecs/components/Sprite.h"
#include "ecs/BuildingGeometry.h"
#include "ecs/Buildings.h"
#include "ecs/DropSites.h"
#include "ecs/Events.h"
#include "ecs/Felling.h"
#include "ecs/Navigation.h"
#include "ecs/VillagerJobs.h"
#include "ecs/components/Animation.h"
#include "ecs/components/Owner.h"
#include "ecs/Combat.h"
#include "ecs/Cheats.h"
#include "ecs/Players.h"
#include "ecs/Repairs.h"
#include "ecs/Resources.h"
#include "ecs/Victory.h"
#include "ecs/Vision.h"
#include "ecs/Turning.h"
#include "ecs/components/Combat.h"
#include "pathfinding/ApproachPlanner.h"
#include "pathfinding/Pathfinder.h"

#include <algorithm>
#include <vector>

namespace ECS::Systems {

namespace {
// How much a crew of builders is worth, as a multiple of one: every builder
// after the first adds a third of a builder, so a crew of n raises a building
// in 3T/(n+2) of the time T one villager takes -- the building's own
// construction time in the dat.
Core::Fixed32 CrewRate(int crew) {
    return Core::Fixed32(crew + 2) / Core::Fixed32(3);
}


// How far apart two workers on one job stand, as a share of a worker's width:
// side by side. Past the room for that the rest wait, this many widths
// further out, for a spot to come free.
const Core::Fixed32 kCrewSpacing = Core::Fixed32(1);
const Core::Fixed32 kWaitOut = Core::Fixed32(1);

// How much further than its range a worker may be and still be at the job:
// what a walk ending on the range's edge may fall short by.
const Core::Fixed32 kArriveSlack = Core::Fixed32(5) / Core::Fixed32(100);

// Fallback reach when the dat has no work range for the unit. The dat's own
// figure for a villager's build task is 0.1 tiles -- close enough to touch the
// wall -- and that is what is used when it is there.
const Core::Fixed32 kDefaultWorkRange = Core::Fixed32(1) / Core::Fixed32(10);

// Give up on a site after this many approaches have failed to get the villager
// near the wall. A crowd shifts constantly and each retry is one short walk, so
// this allows a good many. Reset the moment the villager starts working.
constexpr int kMaxBuildAttempts = 40;

// What a worker is currently making its way to. Every job has one, and the
// approach is the same walk whichever it is, so one routine serves all three.
entt::entity ActiveTarget(const Components::Worker& worker) {
    switch (worker.state) {
    case Core::UnitState::Building:  return worker.target_building;
    case Core::UnitState::Repairing: return worker.target_building;
    case Core::UnitState::Returning: return worker.target_dropoff;
    default:                         return worker.target_resource;
    }
}

// How the walk to a job is going.
enum class Approach {
    Arrived,      // within reach; get on with the work
    Walking,      // still on the way, nothing to decide
    Waiting,      // every spot at it taken: standing by for one to come free
    Unreachable,  // nowhere to stand, or no route to it
};

// Walks `self` up to `target`, whose ground is `ground`, until its box is
// within `range` of it -- the dat's work range for the job -- and reports
// whether it is there yet.
//
// The one place a worker approaches anything: a tree, a drop site and a
// foundation are all a patch of ground to stand beside and not on. The walk
// makes for a spot beside the ground facing the worker -- at a job (`crew`),
// clear of the rest of the crew; at a drop site, which takes a load at once,
// the nearest -- and ends as soon as the worker is in range, wherever that is.
Approach ApproachGround(entt::registry& registry, const Physics::Grid& grid,
                        entt::entity self, const Components::Worker& worker,
                        const Components::Transform& transform, entt::entity target_entity,
                        const ECS::Buildings::Bounds& ground, Core::Fixed32 range,
                        bool crew = true) {
    const Core::Fixed32 radius = Navigation::RadiusOf(registry, self);
    const ECS::Buildings::Bounds reach{ground.min_x - radius, ground.min_y - radius,
                                       ground.max_x + radius, ground.max_y + radius};
    const Core::Fixed32 gap = reach.DistanceFrom(transform.position);
    const bool within = gap <= range + kArriveSlack;
    const Core::Fixed32 spacing = (radius + radius) * kCrewSpacing;

    // What the rest of the crew on this same job has taken: where those already
    // at work stand, where those still walking are headed (a destination is
    // chosen once and held), and where those waiting for a spot stand.
    // Counting walkers keeps a group ordered together from all being handed
    // the same spot.
    const entt::entity target = ActiveTarget(worker);
    std::vector<Components::Vector2D> taken;
    bool crowded = false;
    auto mates = registry.view<Components::Worker, Components::Transform>();
    for (auto mate : mates) {
        if (!crew) break;
        if (mate == self) continue;
        const auto& other = mates.get<Components::Worker>(mate);
        if (ActiveTarget(other) != target) continue;
        if (other.working) {
            const auto& there = mates.get<Components::Transform>(mate).position;
            taken.push_back(there);
            if (Core::FixedMath::Distance(there.x - transform.position.x,
                                          there.y - transform.position.y) * Core::Fixed32(2) < spacing) {
                crowded = true;
            }
            continue;
        }
        const auto* heading = registry.try_get<Components::Path>(mate);
        if (heading && heading->has_goal &&
            heading->current_waypoint_index < heading->waypoints.size()) {
            taken.push_back(heading->goal);
        } else {
            taken.push_back(mates.get<Components::Transform>(mate).position);
        }
    }
    // Within reach and not standing over a crew mate at work: at work here.
    if (within && !crowded) return Approach::Arrived;

    const auto* route = registry.try_get<Components::Path>(self);
    if (route && route->current_waypoint_index < route->waypoints.size()) {
        return Approach::Walking;
    }

    Pathfinding::ApproachRequest ask;
    ask.target = ground;
    ask.from = transform.position;
    ask.standoff = radius + range;
    ask.clearance = spacing;
    ask.taken = taken.empty() ? nullptr : taken.data();
    ask.taken_count = taken.size();
    ask.layer = Navigation::LayerOf(registry, grid, self);

    const Pathfinding::ApproachResult plan =
        Pathfinding::FindStandingSpot(grid, ask);
    if (!plan.found && !plan.full) return within ? Approach::Arrived : Approach::Unreachable;
    if (!plan.found) {
        // Every spot taken: it stands by a body's width outside the crew, as
        // the original's do, and takes the first spot to come free.
        ask.standoff = ask.standoff + spacing * kWaitOut;
        const Pathfinding::ApproachResult by = Pathfinding::FindStandingSpot(grid, ask);
        if (!by.found || Core::FixedMath::Distance(by.spot.x - transform.position.x,
                                                   by.spot.y - transform.position.y) * Core::Fixed32(2) < spacing) {
            return Approach::Waiting;
        }
        auto walk = Navigation::Route(registry, grid, self, transform.position, by.spot, entt::null,
                                      Core::Fixed32(0));
        if (walk.waypoints.empty()) return Approach::Waiting;
        registry.emplace_or_replace<Components::Path>(self, std::move(walk));
        return Approach::Waiting;
    }
    // Over a crew mate, the free spot where it already stands: at work there.
    if (within && Core::FixedMath::Distance(plan.spot.x - transform.position.x,
                                            plan.spot.y - transform.position.y) * Core::Fixed32(2) < spacing) {
        return Approach::Arrived;
    }

    // Stepping aside from a crew mate, already within reach: a walk to the
    // spot itself, which a walk to the job would end before it began.
    auto path = within ? Navigation::Route(registry, grid, self, transform.position, plan.spot,
                                           entt::null, Core::Fixed32(0))
                       : Navigation::Route(registry, grid, self, transform.position, plan.spot,
                                           target_entity, range);
    if (path.waypoints.empty()) return Approach::Unreachable;

    registry.emplace_or_replace<Components::Path>(self, std::move(path));
    return Approach::Walking;
}

// The dat's work range for `rules`' tasks of `action`: how near the job has
// to be done from, box to box. Zero when none says.
Core::Fixed32 TaskRange(const Core::Rules::UnitRules* rules, std::initializer_list<int> actions) {
    Core::Fixed32 range(0);
    if (rules == nullptr) return range;
    for (const auto& task : rules->tasks) {
        for (int action : actions) {
            if (task.action == action) range = Core::FixedMath::Max(range, task.work_range);
        }
    }
    return range;
}

// The rules a worker gathers by: a villager its job's unit -- a lumberjack's
// rate and load, not a villager's -- and anything else that gathers, its own.
const Core::Rules::UnitRules* GathererRules(const entt::registry& registry, entt::entity self,
                                            const Components::Worker& worker,
                                            Core::PlayerId owner) {
    // By the table it plays by: a converted villager gathers at its maker's
    // rates.
    (void)owner;
    if (worker.gatherer >= 0) {
        const auto* identity = registry.try_get<Components::Identity>(self);
        return Players::RulesFor(registry, self,
                                 identity != nullptr ? identity->unit_id : worker.gatherer);
    }
    const auto* anim = registry.try_get<Components::Animation>(self);
    return Players::RulesFor(registry, self,
                           Villagers::UnitId(Villagers::JobFor(worker),
                                             anim != nullptr && anim->female));
}

// How much of its rate a worker gathers `target` at: its gather task's work
// value for that target -- a fishing ship 1.75 at the open sea's fish, 0.75 at
// a fish trap. One for a villager's tasks, which all state it, and for a
// target no task names.
Core::Fixed32 TaskValue(const entt::registry& registry, const Core::Rules::UnitRules* rules,
                        entt::entity target) {
    const auto* identity = registry.try_get<Components::Identity>(target);
    if (rules == nullptr || identity == nullptr) return Core::Fixed32(1);
    const auto* aimed = Core::Rules::Unit(identity->unit_id);
    const int cls = aimed != nullptr ? aimed->cls : -1;
    for (int t = 0; t < rules->task_count; ++t) {
        const auto& task = rules->tasks[t];
        if (task.action != Core::Rules::TaskAction::Gather &&
            task.action != Core::Rules::TaskAction::Hunt) {
            continue;
        }
        if (!task.Targets(identity->unit_id, cls)) continue;
        return task.work_value.value > 0 ? task.work_value : Core::Fixed32(1);
    }
    return Core::Fixed32(1);
}

// Whether a worker can take its load to `site`: the worker's own player's,
// and on ground its layer reaches -- a fishing ship's dock on the same sea, a
// villager's camp on the same land. A site still going up is accepted too;
// the computer players' economy relies on that.
bool UsableDropSite(const entt::registry& registry, const Physics::Grid& grid,
                    entt::entity self, entt::entity site, int region) {
    const auto* mine = registry.try_get<Components::Owner>(self);
    const auto* theirs = registry.try_get<Components::Owner>(site);
    if (mine == nullptr || theirs == nullptr || mine->player_id != theirs->player_id) return false;
    const auto* identity = registry.try_get<Components::Identity>(site);
    if (identity == nullptr || region < 0) return identity != nullptr;
    const auto ground = ECS::Buildings::FootprintBounds(
        registry.get<Components::Transform>(site).position, identity->unit_id);
    const Physics::Layer layer = Navigation::LayerOf(registry, grid, self);
    for (int y = Physics::TileOf(ground.min_y) - 1; y <= Physics::TileOf(ground.max_y) + 1; ++y) {
        for (int x = Physics::TileOf(ground.min_x) - 1; x <= Physics::TileOf(ground.max_x) + 1; ++x) {
            if (Pathfinding::RegionAt(grid, x, y, layer) == region) return true;
        }
    }
    return false;
}

// A builder whose building is up does not stand idle beside it: it takes up
// the nearest other foundation of its owner's within reach -- which is how one
// order raises a whole line of wall: the closest site by true distance, in
// range when a tile of it lies less than about nine and a half tiles away,
// tile to tile.
constexpr int kKeepBuildingReachSquared = 90;

entt::entity NextSite(const entt::registry& registry, entt::entity self, entt::entity done) {
    const auto* owner = registry.try_get<Components::Owner>(self);
    const auto* at = registry.try_get<Components::Transform>(self);
    if (owner == nullptr || at == nullptr) return entt::null;
    const int tx = Physics::TileOf(at->position.x);
    const int ty = Physics::TileOf(at->position.y);
    entt::entity best = entt::null;
    Core::Fixed32 nearest{};
    auto sites = registry.view<const Components::Building, const Components::Identity,
                               const Components::Transform, const Components::Owner>();
    for (auto site : sites) {
        if (site == done) continue;
        if (sites.get<const Components::Owner>(site).player_id != owner->player_id) continue;
        const auto& building = sites.get<const Components::Building>(site);
        if (building.state != Components::BuildingState::UnderConstruction ||
            building.progress >= Core::Fixed32(1)) {
            continue;
        }
        const auto ground = ECS::Buildings::FootprintBounds(
            sites.get<const Components::Transform>(site).position,
            sites.get<const Components::Identity>(site).unit_id);
        // In reach: a tile of it near enough, counting whole tiles.
        const int nx = std::clamp(tx, Physics::TileOf(ground.min_x + Core::Fixed32(1) / Core::Fixed32(2)),
                                  Physics::TileOf(ground.max_x - Core::Fixed32(1) / Core::Fixed32(2)));
        const int ny = std::clamp(ty, Physics::TileOf(ground.min_y + Core::Fixed32(1) / Core::Fixed32(2)),
                                  Physics::TileOf(ground.max_y - Core::Fixed32(1) / Core::Fixed32(2)));
        if ((nx - tx) * (nx - tx) + (ny - ty) * (ny - ty) >= kKeepBuildingReachSquared) continue;
        const Core::Fixed32 gap = ground.DistanceFrom(at->position);
        if (best == entt::null || gap < nearest || (gap == nearest && site < best)) {
            best = site;
            nearest = gap;
        }
    }
    return best;
}

// A gatherer whose target is gone -- run dry, or finished by someone else --
// does not stand idle: every gather task of the villager jobs and of the
// fishing ship has AutoSearchTargets set, and finds another target of that
// task within the gatherer's search radius (4 tiles a villager, 5 a male
// hunter, 12 a fishing ship). The nearest to where the last one stood, of
// what the player has explored; the map's or the player's own, never
// another's; a farm only finished and with nobody on it. The tasks name the
// living animals, not the carcasses, so a shepherd goes on to the next sheep.
entt::entity NextResource(const entt::registry& registry, entt::entity self,
                          const Components::Worker& worker, Core::PlayerId owner,
                          const Components::Vector2D& near, entt::entity gone) {
    const Core::Rules::UnitRules* rules = GathererRules(registry, self, worker, owner);
    if (rules == nullptr || rules->search_radius.value <= 0) return entt::null;
    const Core::Fixed32 reach = rules->search_radius;
    std::vector<entt::entity> worked;
    bool worked_known = false;
    entt::entity best = entt::null;
    Core::Fixed32 nearest{};
    auto stock = registry.view<const Components::Resource, const Components::Identity,
                               const Components::Transform>();
    for (auto e : stock) {
        if (e == gone || stock.get<const Components::Resource>(e).current_amount <= 0) continue;
        const auto& at = stock.get<const Components::Transform>(e).position;
        const Core::Fixed32 gap = Core::FixedMath::Distance(at.x - near.x, at.y - near.y);
        if (gap > reach || (best != entt::null && (gap > nearest || (gap == nearest && e > best)))) continue;
        const int unit_id = stock.get<const Components::Identity>(e).unit_id;
        const Core::Rules::UnitRules* what = Core::Rules::Unit(unit_id);
        const int cls = what != nullptr ? what->cls : -1;
        bool named = false;
        for (int t = 0; t < rules->task_count && !named; ++t) {
            const auto& task = rules->tasks[t];
            named = task.auto_search && task.Targets(unit_id, cls) &&
                    (task.action == Core::Rules::TaskAction::Gather ||
                     task.action == Core::Rules::TaskAction::Hunt);
        }
        if (!named || registry.all_of<Components::Dying>(e)) continue;
        if (const auto* theirs = registry.try_get<Components::Owner>(e)) {
            if (theirs->player_id != Core::PLAYER_GAIA && theirs->player_id != owner) continue;
        }
        if (const auto* farm = registry.try_get<Components::Building>(e)) {
            if (farm->state != Components::BuildingState::Complete) continue;
            if (!worked_known) {
                auto others = registry.view<const Components::Worker>();
                for (auto other : others) {
                    if (other != self) worked.push_back(others.get<const Components::Worker>(other).target_resource);
                }
                std::sort(worked.begin(), worked.end());
                worked_known = true;
            }
            if (std::binary_search(worked.begin(), worked.end(), e)) continue;
        }
        if (!Vision::Explored(registry, owner, Physics::TileOf(at.x), Physics::TileOf(at.y))) continue;
        best = e;
        nearest = gap;
    }
    return best;
}

Core::PlayerId OwnerOf(const entt::registry& registry, entt::entity e) {
    const auto* owner = registry.try_get<Components::Owner>(e);
    return owner != nullptr ? owner->player_id : Core::PLAYER_GAIA;
}

// A farm takes one farmer: someone else is on it, or walking its food home
// from it. With `first_only`, only someone who came first -- the lower entity
// -- counts, so of two that arrive together one stays.
bool FarmTaken(const entt::registry& registry, entt::entity self, entt::entity farm,
               bool first_only) {
    if (!registry.valid(farm) || !registry.all_of<Components::Building, Components::Resource>(farm)) {
        return false;
    }
    auto workers = registry.view<const Components::Worker>();
    for (auto other : workers) {
        if (other == self || (first_only && other > self)) continue;
        const auto& w = workers.get<const Components::Worker>(other);
        if (w.target_resource != farm) continue;
        if (w.state == Core::UnitState::Gathering || w.state == Core::UnitState::Returning) return true;
    }
    return false;
}

// What a builder takes up on finishing `site`: a Farm, to farm it, unless
// another farmer has it; a Mill, Lumber Camp or Mining Camp, the nearest of
// what it takes in -- bushes, trees, gold or stone -- within the villager's
// search radius of its walls. Not a Town Center, which takes everything in,
// nor a Dock, which no villager job hands a load to.
entt::entity WorkAt(const entt::registry& registry, entt::entity self, Core::PlayerId owner,
                    entt::entity site) {
    if (!registry.valid(site) || !registry.all_of<Components::Building, Components::Identity,
                                                  Components::Transform>(site)) {
        return entt::null;
    }
    if (registry.all_of<Components::Resource>(site)) {
        return FarmTaken(registry, self, site, false) ? entt::null : site;
    }
    const int site_unit = registry.get<Components::Identity>(site).unit_id;
    bool takes[static_cast<int>(Core::ResourceType::COUNT)] = {};
    int kinds = 0;
    for (int r = 0; r < static_cast<int>(Core::ResourceType::COUNT); ++r) {
        takes[r] = DropSites::Accepts(site_unit, static_cast<Core::ResourceType>(r));
        kinds += takes[r] ? 1 : 0;
    }
    if (kinds == 0 || kinds >= 3) return entt::null;
    const auto ground = ECS::Buildings::FootprintBounds(
        registry.get<Components::Transform>(site).position, site_unit);
    const Core::Rules::UnitRules* villager = Players::RulesFor(registry, self,
        registry.all_of<Components::Identity>(self) ? registry.get<Components::Identity>(self).unit_id : -1);
    const Core::Fixed32 reach = villager != nullptr && villager->search_radius.value > 0
                              ? villager->search_radius : Core::Fixed32(4);
    entt::entity best = entt::null;
    Core::Fixed32 nearest{};
    auto stock = registry.view<const Components::Resource, const Components::Identity,
                               const Components::Transform>();
    for (auto e : stock) {
        const auto& res = stock.get<const Components::Resource>(e);
        if (res.current_amount <= 0 || !takes[static_cast<int>(res.type)]) continue;
        if (registry.any_of<Components::Building, Components::Combat, Components::Dying>(e)) continue;
        const auto& at = stock.get<const Components::Transform>(e).position;
        const Core::Fixed32 gap = ground.DistanceFrom(at);
        if (gap > reach || (best != entt::null && (gap > nearest || (gap == nearest && e > best)))) continue;
        // What stands still to be gathered -- a tree is "hunted" in the dat
        // too, felled before it is cut -- never game or a herd.
        bool hunt = false;
        const auto job = Villagers::JobForTarget(registry, e, hunt);
        if (job != Components::VillagerJob::Forage && job != Components::VillagerJob::Lumber &&
            job != Components::VillagerJob::Gold && job != Components::VillagerJob::Mine) {
            continue;
        }
        if (const auto* theirs = registry.try_get<Components::Owner>(e)) {
            if (theirs->player_id != Core::PLAYER_GAIA && theirs->player_id != owner) continue;
        }
        if (!Vision::Explored(registry, owner, Physics::TileOf(at.x), Physics::TileOf(at.y))) continue;
        best = e;
        nearest = gap;
    }
    return best;
}

// Takes up `next` as the worker's target: the job it calls for, and where it
// stands.
void TakeUp(const entt::registry& registry, Components::Worker& worker, entt::entity next) {
    worker.target_resource = next;
    if (next == entt::null) return;
    worker.resource_at = registry.get<Components::Transform>(next).position;
    if (worker.gatherer < 0) {
        bool hunt = false;
        worker.job = Villagers::JobForTarget(registry, next, hunt);
    }
}

} // namespace

void WorkerSystem::Update(entt::registry& registry, Physics::Grid& grid, int32_t fixed_dt_ms) {
    auto view = registry.view<Components::Worker, Components::Transform>();

    // How many builders are swinging at each site, from where they stood at the
    // end of the last tick. A crew shares one building's progress, so each
    // builder needs to know how many others are on it.
    std::vector<std::pair<entt::entity, int>> crews;
    for (auto entity : view) {
        const auto& worker = view.get<Components::Worker>(entity);
        if (worker.state != Core::UnitState::Building || !worker.working) continue;
        auto found = std::find_if(crews.begin(), crews.end(), [&](const auto& c) {
            return c.first == worker.target_building;
        });
        if (found == crews.end()) crews.emplace_back(worker.target_building, 1);
        else ++found->second;
    }
    auto crew_on = [&crews](entt::entity site) {
        for (const auto& c : crews) if (c.first == site) return std::max(1, c.second);
        return 1;
    };
    // The same for repairers: a crew shares the mending of one thing.
    std::vector<std::pair<entt::entity, int>> menders;
    for (auto entity : view) {
        const auto& worker = view.get<Components::Worker>(entity);
        if (worker.state != Core::UnitState::Repairing || !worker.working) continue;
        auto found = std::find_if(menders.begin(), menders.end(), [&](const auto& c) {
            return c.first == worker.target_building;
        });
        if (found == menders.end()) menders.emplace_back(worker.target_building, 1);
        else ++found->second;
    }
    auto menders_on = [&menders](entt::entity target) {
        for (const auto& c : menders) if (c.first == target) return std::max(1, c.second);
        return 1;
    };

    for (auto entity : view) {
        auto& worker = view.get<Components::Worker>(entity);
        auto& transform = view.get<Components::Transform>(entity);

        // `working` says the villager is swinging a hammer or an axe *right
        // now*, and the animation system wears the job graphic because of it.
        // Anything may end the job from outside -- a move order, an attack
        // order, the building finishing -- without going through this system,
        // so the flag is cleared here. Only the states that actually do a job
        // keep it; walking to one or carrying a load home does not.
        const bool on_a_job = worker.state == Core::UnitState::Building ||
                              worker.state == Core::UnitState::Gathering ||
                              worker.state == Core::UnitState::Repairing;
        if (!on_a_job) {
            worker.working = false;
        }
        if (worker.state != Core::UnitState::Building &&
            worker.state != Core::UnitState::Repairing) {
            worker.target_building = entt::null;
            worker.build_attempts = 0;
        }

        switch (worker.state) {
            case Core::UnitState::Idle:
                break;
                
            case Core::UnitState::Moving: {
                if (!registry.valid(worker.target_resource)) {
                    // Finished by someone else on the way: the next of the
                    // same near where it stood, or stand.
                    TakeUp(registry, worker,
                           NextResource(registry, entity, worker, OwnerOf(registry, entity),
                                        worker.resource_at, entt::null));
                    if (!registry.valid(worker.target_resource)) {
                        worker.state = Core::UnitState::Idle;
                        break;
                    }
                    registry.remove<Components::Path>(entity);
                }
                if (registry.all_of<Components::Transform>(worker.target_resource)) {
                    worker.resource_at = registry.get<Components::Transform>(worker.target_resource).position;
                }
                // A farm someone else is farming: the next farm free near it,
                // or stand.
                if (FarmTaken(registry, entity, worker.target_resource, false)) {
                    const entt::entity taken = worker.target_resource;
                    TakeUp(registry, worker,
                           NextResource(registry, entity, worker, OwnerOf(registry, entity),
                                        worker.resource_at, taken));
                    registry.remove<Components::Path>(entity);
                    if (!registry.valid(worker.target_resource)) {
                        worker.state = Core::UnitState::Idle;
                        break;
                    }
                }
                
                // A living animal has to be killed before it can be gathered:
                // that is a fight, and the combat module runs it. The carcass
                // it leaves becomes the target, and the walk resumes below.
                // A farm has hit points too, but it is ground to stand on and
                // work, not something to kill.
                // Still falling: wait for the carcass it is about to leave,
                // which becomes the target when it lands (ecs/Mortality).
                if (registry.all_of<Components::Dying>(worker.target_resource)) {
                    registry.remove<Components::Path>(entity);
                    break;
                }
                if (Combat::Alive(registry, worker.target_resource) &&
                    !registry.all_of<Components::Building>(worker.target_resource)) {
                    auto* combat = registry.try_get<Components::Combat>(entity);
                    if (combat == nullptr) {
                        worker.state = Core::UnitState::Idle;
                    } else if (combat->target != worker.target_resource) {
                        // Set on it already and dropped: no way to it, as to
                        // a deer in a clearing walled in by trees. It stands,
                        // as an order the original cannot carry out leaves it.
                        if (worker.hunted == worker.target_resource) {
                            worker.hunted = entt::null;
                            worker.target_resource = entt::null;
                            worker.state = Core::UnitState::Idle;
                            break;
                        }
                        Combat::Engage(registry, entity, worker.target_resource, true);
                        worker.hunted = worker.target_resource;
                    }
                    break;
                }

                // Up to the resource's own box -- a tree's tile, a carcass's
                // body -- within the job's range.
                const auto ground = Combat::BoxOf(registry, worker.target_resource);
                const Core::Fixed32 range = TaskRange(
                    GathererRules(registry, entity, worker, OwnerOf(registry, entity)),
                    {Core::Rules::TaskAction::Gather, Core::Rules::TaskAction::Hunt});

                switch (ApproachGround(registry, grid, entity, worker, transform,
                                       worker.target_resource, ground, range)) {
                case Approach::Walking:
                case Approach::Waiting:
                    break;
                case Approach::Unreachable:
                    // Walled in: no open ground beside it.
                    worker.target_resource = entt::null;
                    worker.state = Core::UnitState::Idle;
                    break;
                case Approach::Arrived:
                    worker.state = Core::UnitState::Gathering;
                    // At the resource, so at work from this tick, with no
                    // frame of it standing idle against the tree.
                    worker.working = true;
                    if (registry.any_of<Components::Path>(entity)) {
                        registry.remove<Components::Path>(entity);
                    }
                    break;
                }
                break;
            }
                
            case Core::UnitState::Gathering: {
                if (!registry.valid(worker.target_resource)) {
                    // Gone from under it: the next of the same, or home.
                    worker.working = false;
                    TakeUp(registry, worker,
                           NextResource(registry, entity, worker, OwnerOf(registry, entity),
                                        worker.resource_at, entt::null));
                    worker.state = registry.valid(worker.target_resource)
                                 ? Core::UnitState::Moving
                                 : Core::UnitState::Returning;
                    break;
                }
                worker.resource_at = registry.get<Components::Transform>(worker.target_resource).position;

                // Face what is being worked on, so an axe swings at the tree
                // rather than wherever the villager happened to walk in from;
                // a fishing ship turns to it an angle at a time and only then
                // fishes. Standing at the resource and taking from it: the job
                // animation, not the standing one.
                worker.working = Turning::Toward(
                    registry, entity,
                    registry.get<Components::Transform>(worker.target_resource).position,
                    fixed_dt_ms);
                if (!worker.working) break;

                auto& resource = registry.get<Components::Resource>(worker.target_resource);

                // The rate the owner's rules give this job, in resources per
                // second -- the dat's figure, raised by whatever gathering
                // technologies the owner has. Fractional, so it is accumulated
                // rather than rounded.
                const Core::PlayerId owner =
                    registry.all_of<Components::Owner>(entity)
                        ? registry.get<Components::Owner>(entity).player_id
                        : Core::PLAYER_GAIA;
                const auto* job = GathererRules(registry, entity, worker, owner);

                // A standing tree is felled first: its strokes, then it lies
                // on the ground, and only then is its wood cut.
                if (Felling::Standing(registry, worker.target_resource)) {
                    if (worker.felling != worker.target_resource) {
                        worker.felling = worker.target_resource;
                        worker.fell_ms = Felling::FellingMs(job);
                    }
                    worker.fell_ms -= fixed_dt_ms;
                    if (worker.fell_ms > 0) break;
                    Felling::Fell(registry, worker.target_resource);
                }
                worker.felling = entt::null;
                const Core::Fixed32 per_second =
                    ((job && job->work_rate.value > 0) ? job->work_rate : Core::Fixed32(1)) *
                    TaskValue(registry, job, worker.target_resource);

                worker.gather_progress = worker.gather_progress +
                    per_second * Core::Fixed32(fixed_dt_ms) / Core::Fixed32(1000);
                // "aegis": a full load at a stroke.
                if (Cheats::Aegis(registry, owner)) {
                    worker.gather_progress = worker.gather_progress + Core::Fixed32(worker.max_carry * 10);
                }

                // Two at one farm -- they arrived on the same tick: the one
                // that came later goes to the next free farm, or stands.
                // Asked as each whole unit is taken, not every tick.
                if (worker.gather_progress >= Core::Fixed32(1) &&
                    FarmTaken(registry, entity, worker.target_resource, true)) {
                    worker.working = false;
                    worker.gather_progress = Core::Fixed32(0);
                    const entt::entity taken = worker.target_resource;
                    TakeUp(registry, worker,
                           NextResource(registry, entity, worker, owner, worker.resource_at, taken));
                    worker.state = registry.valid(worker.target_resource) ? Core::UnitState::Moving
                                                                          : Core::UnitState::Idle;
                    break;
                }
                if (worker.gather_progress >= Core::Fixed32(1)) {
                    const int whole = worker.gather_progress.ToInt();
                    worker.gather_progress =
                        worker.gather_progress - Core::Fixed32(whole);

                    // Into this resource's own slot, leaving anything else the
                    // villager is already holding untouched.
                    const int slot = static_cast<int>(resource.type);
                    // How much this job can shoulder, from the dat: ten for a
                    // villager, thirty-five for a hunter carrying meat.
                    const int capacity =
                        (job && job->carry > 0) ? job->carry : worker.max_carry;
                    const int room = capacity - worker.carried[slot];
                    const int taken = std::min(
                        std::min(whole, resource.current_amount),
                        std::max(0, room));
                    worker.carried[slot] += taken;
                    resource.current_amount -= taken;
                    worker.carry_type = resource.type;

                    if (resource.current_amount <= 0) {
                        // Run dry: on to the next of the same nearby -- with
                        // room left in hand straight there, full by way of
                        // the drop site -- or home with the load and stand.
                        const entt::entity gone = worker.target_resource;
                        worker.resource_at = registry.get<Components::Transform>(gone).position;
                        ECS::Resources::Exhaust(registry, grid, gone);
                        TakeUp(registry, worker,
                               NextResource(registry, entity, worker, owner, worker.resource_at, gone));
                        const bool room = worker.carried[slot] < capacity;
                        worker.state = registry.valid(worker.target_resource) && room
                                     ? Core::UnitState::Moving
                                     : Core::UnitState::Returning;
                        if (worker.state == Core::UnitState::Moving) worker.working = false;
                    }
                    // The cap is per resource: a full load of wood sends the
                    // villager home even if it has room for gold.
                    else if (worker.carried[slot] >= capacity) {
                        worker.state = Core::UnitState::Returning;
                    }
                }
                break;
            }
                
            case Core::UnitState::Returning: {
                // Sent to a building that is gone: the order is over.
                if (worker.ordered_drop && !registry.valid(worker.target_dropoff)) {
                    worker.ordered_drop = false;
                    worker.state = Core::UnitState::Idle;
                    break;
                }
                if (!registry.valid(worker.target_dropoff)) {
                    const int region = Navigation::RegionOf(registry, grid, entity);
                    auto b_view = registry.view<Components::Transform, Components::Building>();
                    entt::entity best_dropoff = entt::null;
                    // True distance, not a count of tiles between them.
                    Core::Fixed32 nearest = Core::Fixed32(1 << 20);

                    for (auto b_ent : b_view) {
                        // Only somewhere that will actually take what is being
                        // carried.
                        const auto* what =
                            registry.try_get<Components::Identity>(b_ent);
                        if (what == nullptr ||
                            !ECS::DropSites::AcceptsAnything(what->unit_id, worker) ||
                            !UsableDropSite(registry, grid, entity, b_ent, region)) {
                            continue;
                        }
                        auto& b_trans = b_view.get<Components::Transform>(b_ent);
                        const Core::Fixed32 distance = Core::FixedMath::Distance(
                            transform.position.x - b_trans.position.x,
                            transform.position.y - b_trans.position.y);
                        if (distance < nearest) {
                            nearest = distance;
                            best_dropoff = b_ent;
                        }
                    }

                    if (best_dropoff == entt::null) {
                        worker.state = Core::UnitState::Idle;
                        break;
                    }
                    worker.target_dropoff = best_dropoff;
                }

                // Walk up to the drop site's wall, not into it: the pathfinder
                // refuses a tile inside the footprint.
                const auto* site_id = registry.try_get<Components::Identity>(
                    worker.target_dropoff);
                const auto& site_at =
                    registry.get<Components::Transform>(worker.target_dropoff);
                const auto ground =
                    site_id ? ECS::Buildings::FootprintBounds(site_at.position,
                                                              site_id->unit_id)
                            : ECS::Buildings::TileBounds(site_at.position);

                switch (ApproachGround(registry, grid, entity, worker, transform,
                                       worker.target_dropoff, ground, Core::Fixed32(0), false)) {
                case Approach::Walking:
                case Approach::Waiting:
                    break;
                case Approach::Unreachable:
                    // Try another drop site next tick rather than stand here
                    // holding a full load -- unless sent to this one.
                    worker.target_dropoff = entt::null;
                    if (worker.ordered_drop) {
                        worker.ordered_drop = false;
                        worker.state = Core::UnitState::Idle;
                    }
                    break;
                case Approach::Arrived: {
                    if (registry.any_of<Components::Path>(entity)) {
                        registry.remove<Components::Path>(entity);
                    }

                    // Hand over everything this site takes; anything it does
                    // not stays in hand for the next one that does.
                    const auto* owner = registry.try_get<Components::Owner>(entity);
                    Players::Player* stores = owner ? Players::Find(registry, owner->player_id) : nullptr;
                    worker.target_dropoff = entt::null;
                    // Sent here by an order: all of it, however little, and
                    // then it stands.
                    if (worker.ordered_drop) {
                        if (site_id != nullptr) ECS::DropSites::DepositAll(site_id->unit_id, worker, stores);
                        worker.ordered_drop = false;
                        worker.state = Core::UnitState::Idle;
                        break;
                    }
                    if (site_id != nullptr) ECS::DropSites::Deposit(site_id->unit_id, worker, stores);

                    // Straight back to the same resource, so a villager stays
                    // on one job until it runs dry -- or, if it has gone
                    // meanwhile, to the next of the same there.
                    const bool gathering = worker.gatherer >= 0 ||
                                           (worker.job != Components::VillagerJob::None &&
                                            worker.job != Components::VillagerJob::Base);
                    if (!registry.valid(worker.target_resource) && gathering) {
                        TakeUp(registry, worker,
                               NextResource(registry, entity, worker, OwnerOf(registry, entity),
                                            worker.resource_at, entt::null));
                    }
                    worker.state = registry.valid(worker.target_resource)
                                 ? Core::UnitState::Moving
                                 : Core::UnitState::Idle;
                    break;
                }
                }
                break;
            }
            
            case Core::UnitState::Building: {
                auto stop_building = [&worker]() {
                    worker.target_building = entt::null;
                    worker.build_attempts = 0;
                    worker.working = false;
                    worker.state = Core::UnitState::Idle;
                };
                // Done with this site: on to the next foundation in reach, or
                // stand. A farm's builder stays with its farm.
                auto move_on = [&](entt::entity done) {
                    // The builders of a Farm farm it, and of a camp gather
                    // beside it.
                    if (const entt::entity job = WorkAt(registry, entity, OwnerOf(registry, entity), done);
                        job != entt::null) {
                        stop_building();
                        worker.carry_type = registry.get<Components::Resource>(job).type;
                        TakeUp(registry, worker, job);
                        worker.state = Core::UnitState::Moving;
                        registry.remove<Components::Path>(entity);
                        return;
                    }
                    const bool farm = registry.valid(done) &&
                                      registry.all_of<Components::Resource>(done);
                    const entt::entity next = farm ? entt::null : NextSite(registry, entity, done);
                    if (next == entt::null) {
                        stop_building();
                        return;
                    }
                    worker.target_building = next;
                    worker.build_attempts = 0;
                    worker.working = false;
                    registry.remove<Components::Path>(entity);
                };

                if (!registry.valid(worker.target_building) ||
                    !registry.all_of<Components::Health, Components::Transform>(
                        worker.target_building)) {
                    move_on(entt::null);
                    break;
                }

                auto& health = registry.get<Components::Health>(worker.target_building);
                auto* raising = registry.try_get<Components::Building>(worker.target_building);
                if (raising == nullptr ||
                    raising->state != Components::BuildingState::UnderConstruction ||
                    raising->progress >= Core::Fixed32(1)) {
                    // Someone else finished it.
                    move_on(worker.target_building);
                    break;
                }

                const auto& site = registry.get<Components::Transform>(worker.target_building);

                // Ask the building where its ground is. A builder stands beside
                // a footprint, never on it, and the footprint's limits are the
                // building module's to report -- this system does not work them
                // out.
                const auto* identity = registry.try_get<Components::Identity>(
                    worker.target_building);
                const ECS::Buildings::Bounds ground =
                    identity ? ECS::Buildings::FootprintBounds(site.position,
                                                               identity->unit_id)
                             : ECS::Buildings::Bounds{site.position.x, site.position.y,
                                                      site.position.x, site.position.y};

                // How close the dat says this villager has to be to do the job.
                // A villager's build task states 0.1 tiles -- near enough to
                // touch the wall.
                const auto* self_id = registry.try_get<Components::Identity>(entity);
                const Core::Fixed32 work_range =
                    self_id ? Core::ConfigManager::Get().GetWorkRange(
                                  self_id->unit_id, kDefaultWorkRange)
                            : kDefaultWorkRange;

                switch (ApproachGround(registry, grid, entity, worker, transform,
                                       worker.target_building, ground, work_range)) {
                case Approach::Walking:
                case Approach::Waiting:
                    worker.working = false;
                    break;
                case Approach::Unreachable:
                    worker.working = false;
                    // Worth a few more tries -- the crowd shifts and a
                    // foundation's surroundings change as it goes up -- but not
                    // forever.
                    if (++worker.build_attempts > kMaxBuildAttempts) {
                        stop_building();
                    }
                    break;
                case Approach::Arrived:
                    worker.build_attempts = 0;
                    worker.working = true;
                    break;
                }
                if (!worker.working) break;
                // The first work on a foundation: its ground taken turns the
                // builder away; its own side standing on it, the builder waits.
                if (!raising->begun) {
                    const auto start = ECS::Buildings::Begin(registry, grid, worker.target_building);
                    if (start == ECS::Buildings::Start::Taken) {
                        const auto* site_owner = registry.try_get<Components::Owner>(worker.target_building);
                        Events::Emit(registry, {.kind = Events::Kind::Obstructed,
                                                .subject = worker.target_building, .other = entity,
                                                .player = site_owner ? site_owner->player_id : Core::PLAYER_GAIA,
                                                .id = identity ? identity->unit_id : -1,
                                                .at = transform.position});
                        stop_building();
                        break;
                    }
                    if (start == ECS::Buildings::Start::Crowded) {
                        worker.working = false;
                        break;
                    }
                }

                // Face the building, so a ring of builders all swing inward
                // rather than keeping whatever direction they walked in on.
                if (auto* look = registry.try_get<Components::Sprite>(entity)) {
                    const Core::Fixed32 fx = site.position.x - transform.position.x;
                    const Core::Fixed32 fy = site.position.y - transform.position.y;
                    if (fx.value != 0 || fy.value != 0) {
                        look->heading = Core::FixedMath::WorldVectorToHeading(fx, fy);
                    }
                }


                // This builder's share of the crew's progress this tick. Hit
                // points rise with the work, by however much of the building it
                // amounts to, so damage taken while it goes up stays taken.
                {
                    const auto* site_owner =
                        registry.try_get<Components::Owner>(worker.target_building);
                    const Core::PlayerId site_player =
                        site_owner ? site_owner->player_id : Core::PLAYER_GAIA;
                    const auto* rules = Players::UnitOf(registry, site_player,
                                                        identity ? identity->unit_id : -1);
                    // What goes up is the dat's head unit where there is one,
                    // a gate's site unit: its time is the building's.
                    if (rules != nullptr && rules->head_unit >= 0) {
                        if (const auto* head = Players::UnitOf(registry, site_player, rules->head_unit)) {
                            rules = head;
                        }
                    }
                    const int build_ms =
                        std::max(1, rules != nullptr ? rules->train_time : 1) * 1000;
                    const int crew = crew_on(worker.target_building);
                    // "aegis": up at the first blow.
                    const bool instant = site_owner != nullptr &&
                                         Cheats::Aegis(registry, site_owner->player_id);
                    const Core::Fixed32 share = instant ? Core::Fixed32(1)
                        : CrewRate(crew) * Core::Fixed32(fixed_dt_ms) /
                              (Core::Fixed32(crew) * Core::Fixed32(build_ms));
                    const Core::Fixed32 before = raising->progress;
                    raising->progress =
                        Core::FixedMath::Min(Core::Fixed32(1), raising->progress + share);
                    if (before.value == 0 && raising->progress.value > 0) {
                        Victory::Begun(registry, worker.target_building);
                        Events::Emit(registry, {.kind = Events::Kind::Begun,
                                                .subject = worker.target_building,
                                                .player = site_owner ? site_owner->player_id
                                                                     : Core::PLAYER_GAIA,
                                                .id = identity ? identity->unit_id : -1,
                                                .at = site.position});
                    }
                    const int gained = (raising->progress * Core::Fixed32(health.max_hp)).ToInt() -
                                       (before * Core::Fixed32(health.max_hp)).ToInt();
                    health.current_hp = std::min(health.max_hp, health.current_hp + gained);
                    if (raising->progress >= Core::Fixed32(1)) {
                        // The buildings module swaps the site for the finished
                        // art; the builder moves on.
                        move_on(worker.target_building);
                    }
                }
                break;
            }

            case Core::UnitState::Repairing: {
                const entt::entity target = worker.target_building;
                auto stop_repairing = [&worker]() {
                    worker.target_building = entt::null;
                    worker.build_attempts = 0;
                    worker.working = false;
                    worker.repair_progress = Core::Fixed32(0);
                    worker.state = Core::UnitState::Idle;
                };
                // Mended to full, gone, or gone over to a side it may not mend.
                if (!Repairs::CanRepair(registry, entity, target)) {
                    stop_repairing();
                    break;
                }
                // A building is reached at its wall; a ship or a trebuchet at
                // its body.
                const auto& there = registry.get<Components::Transform>(target).position;
                ECS::Buildings::Bounds ground = ECS::Buildings::TileBounds(there);
                if (const auto* aimed = registry.try_get<Components::Identity>(target)) {
                    if (registry.all_of<Components::Building>(target)) {
                        ground = ECS::Buildings::FootprintBounds(there, aimed->unit_id);
                    } else if (const auto* rules = Players::RulesFor(registry, target, aimed->unit_id)) {
                        ground = {there.x - rules->size_x, there.y - rules->size_y,
                                  there.x + rules->size_x, there.y + rules->size_y};
                    }
                }
                const auto* self_id = registry.try_get<Components::Identity>(entity);
                const Core::Fixed32 work_range =
                    self_id ? Core::ConfigManager::Get().GetWorkRange(
                                  self_id->unit_id, kDefaultWorkRange)
                            : kDefaultWorkRange;
                switch (ApproachGround(registry, grid, entity, worker, transform, target, ground,
                                       work_range)) {
                case Approach::Walking:
                case Approach::Waiting:
                    worker.working = false;
                    break;
                case Approach::Unreachable:
                    worker.working = false;
                    if (++worker.build_attempts > kMaxBuildAttempts) stop_repairing();
                    break;
                case Approach::Arrived:
                    worker.build_attempts = 0;
                    worker.working = true;
                    break;
                }
                if (!worker.working) break;
                if (auto* look = registry.try_get<Components::Sprite>(entity)) {
                    const Core::Fixed32 fx = there.x - transform.position.x;
                    const Core::Fixed32 fy = there.y - transform.position.y;
                    if (fx.value != 0 || fy.value != 0) {
                        look->heading = Core::FixedMath::WorldVectorToHeading(fx, fy);
                    }
                }
                // This repairer's share of the crew's pace, a fraction of a
                // hit point a tick, mended a whole one at a time and paid for
                // as it is.
                const auto* own = registry.try_get<Components::Owner>(entity);
                const int crew = menders_on(target);
                const Core::Fixed32 share =
                    Repairs::CrewRate(registry, own != nullptr ? own->player_id : Core::PLAYER_GAIA,
                                      target, crew) /
                    Core::Fixed32(crew);
                // Counted in thousandths of a hit point -- hit points a second
                // times milliseconds -- so a tick's share adds up exactly:
                // a fifth of a hit point is not a fraction fixed point holds.
                worker.repair_progress = worker.repair_progress + share * Core::Fixed32(fixed_dt_ms);
                const int whole = (worker.repair_progress / Core::Fixed32(1000)).ToInt();
                if (whole > 0) {
                    worker.repair_progress = worker.repair_progress - Core::Fixed32(whole * 1000);
                    // Whole again, or the owner out of what it costs: the
                    // repair stops.
                    const auto& mended = registry.get<Components::Health>(target);
                    if (Repairs::Mend(registry, target, whole) < whole ||
                        mended.current_hp >= mended.max_hp) {
                        stop_repairing();
                    }
                }
                break;
            }

            default:
                break;
        }
    }
}

} // namespace ECS::Systems
