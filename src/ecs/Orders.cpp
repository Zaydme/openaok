#include "ecs/Orders.h"

#include <algorithm>

#include <variant>

#include "ecs/Buildings.h"
#include "ecs/Chat.h"
#include "ecs/Cheats.h"
#include "ecs/Combat.h"
#include "ecs/Diplomacy.h"
#include "ecs/DropSites.h"
#include "ecs/Duties.h"
#include "ecs/Flares.h"
#include "ecs/Formations.h"
#include "ecs/components/Formation.h"
#include "ecs/Garrisons.h"
#include "ecs/Market.h"
#include "ecs/Monks.h"
#include "ecs/Mortality.h"
#include "ecs/Navigation.h"
#include "ecs/Packing.h"
#include "ecs/Relics.h"
#include "ecs/Trade.h"
#include "ecs/Triggers.h"
#include "ecs/Tribute.h"
#include "ecs/Repairs.h"
#include "ecs/VillagerJobs.h"
#include "ecs/Victory.h"
#include "ecs/Events.h"
#include "ecs/Spies.h"
#include "ecs/Walls.h"
#include "ecs/Players.h"
#include "ecs/Production.h"
#include "ecs/components/Building.h"
#include "ecs/components/Combat.h"
#include "ecs/components/Duty.h"
#include "ecs/components/Monk.h"
#include "ecs/components/Relic.h"
#include "ecs/components/Garrison.h"
#include "ecs/components/Health.h"
#include "ecs/components/Identity.h"
#include "ecs/components/Owner.h"
#include "ecs/components/Packing.h"
#include "ecs/components/Path.h"
#include "ecs/components/RallyPoint.h"
#include "ecs/components/Resource.h"
#include "ecs/components/Transform.h"
#include "ecs/components/Waypoints.h"
#include "ecs/components/Worker.h"
#include "pathfinding/Pathfinder.h"
#include "core/FixedMath.h"

namespace ECS::Orders {

namespace {

bool Owns(const entt::registry& registry, entt::entity entity, Core::PlayerId player) {
    if (!registry.valid(entity)) return false;
    const auto* owner = registry.try_get<Components::Owner>(entity);
    if (owner == nullptr) return false;
    if (owner->player_id == player) return true;
    // "natural wonders": nature's animals take the cheater's orders.
    return owner->player_id == Core::PLAYER_GAIA && player != Core::PLAYER_GAIA &&
           registry.all_of<Components::Health>(entity) &&
           !registry.all_of<Components::Building>(entity) &&
           !registry.all_of<Components::Resource>(entity) &&
           Cheats::CommandsNature(registry, player);
}

// A unit the player may order about: its own, and out in the world -- one
// sheltering in a building has no place to walk from until it comes out.
bool Commands(const entt::registry& registry, entt::entity unit, Core::PlayerId player) {
    return Owns(registry, unit, player) && registry.all_of<Components::Transform>(unit);
}

// Whatever the unit was doing stops: a new order replaces the old one.
void DropCurrentJob(entt::registry& registry, entt::entity unit) {
    registry.remove<Components::Boarding>(unit);
    // A job broken off for a fight is given up for whatever comes now.
    registry.remove<Components::Interrupted>(unit);
    // Any order but another waypoint forgets the route laid.
    registry.remove<Components::Waypoints>(unit);
    // A transport given another order keeps its load aboard.
    registry.remove<Components::Landing>(unit);
    registry.remove<Components::Duty>(unit);
    registry.remove<Components::RelicErrand>(unit);
    // A trader leaves its route, keeping whatever gold it carries.
    Trade::Stop(registry, unit);
    // A packed trebuchet on its way to set up goes where it is told instead.
    registry.remove<Components::Assaulting>(unit);
    if (auto* monk = registry.try_get<Components::Monk>(unit)) {
        monk->target = entt::null;
        monk->elapsed_ms = 0;
    }
    if (auto* worker = registry.try_get<Components::Worker>(unit)) {
        worker->state = Core::UnitState::Idle;
        worker->working = false;
        worker->ordered_drop = false;
    }
    if (auto* combat = registry.try_get<Components::Combat>(unit)) {
        combat->state = Components::CombatState::Idle;
        combat->target = entt::null;
        combat->ordered = false;
        combat->strike_ms = -1;
        // Whatever it held, it is now where it is told to be.
        combat->has_post = false;
    }
}

void WalkTo(entt::registry& registry, const Physics::Grid& grid, entt::entity unit,
            const Components::Vector2D& to) {
    // Only units walk. A building given a route would slide across the map;
    // a siege engine packing or setting up stays where it is until done.
    if (!registry.all_of<Components::Transform>(unit) ||
        registry.all_of<Components::Building>(unit) || Packing::Busy(registry, unit)) {
        return;
    }
    DropCurrentJob(registry, unit);
    const auto& at = registry.get<Components::Transform>(unit).position;
    // Sent somewhere it cannot stand -- a ship at the land, a villager at the
    // sea -- it goes as near as its own ground allows.
    registry.emplace_or_replace<Components::Path>(
        unit, Navigation::Route(registry, grid, unit, at,
                                Navigation::Toward(registry, grid, unit, to)));
}

// Whether a worker that gathers as itself -- a fishing ship -- has a gather
// task naming `target`, by unit or class. A villager can be sent at anything
// that holds a resource; a fishing ship only at what its tasks name: fish and
// fish traps, not a tree on the shore.
bool OwnTaskNames(const entt::registry& registry, entt::entity unit, entt::entity target) {
    const auto* identity = registry.try_get<Components::Identity>(unit);
    const auto* owner = registry.try_get<Components::Owner>(unit);
    const auto* aimed = registry.try_get<Components::Identity>(target);
    if (identity == nullptr || owner == nullptr || aimed == nullptr) return false;
    const Core::Rules::UnitRules* rules =
        Players::UnitOf(registry, owner->player_id, identity->unit_id);
    const Core::Rules::UnitRules* what = Core::Rules::Unit(aimed->unit_id);
    if (rules == nullptr) return false;
    for (int t = 0; t < rules->task_count; ++t) {
        if (rules->tasks[t].action != Core::Rules::TaskAction::Gather) continue;
        if (rules->tasks[t].Targets(aimed->unit_id, what != nullptr ? what->cls : -1)) return true;
    }
    return false;
}

// Returns false if the unit cannot gather it.
bool GatherFrom(entt::registry& registry, entt::entity unit, entt::entity resource) {
    if (!registry.valid(resource) || !registry.all_of<Components::Resource>(resource)) {
        return false;
    }
    auto* worker = registry.try_get<Components::Worker>(unit);
    if (worker == nullptr) return false;
    if (worker->gatherer >= 0 && !OwnTaskNames(registry, unit, resource)) return false;
    DropCurrentJob(registry, unit);

    // The worker system walks it there, to stand beside the resource's tile.
    // Any route left over from an earlier order is dropped.
    registry.remove<Components::Path>(unit);
    worker->state = Core::UnitState::Moving;
    worker->target_resource = resource;
    worker->hunted = entt::null;
    if (const auto* at = registry.try_get<Components::Transform>(resource)) worker->resource_at = at->position;
    // The job starts when the order is given: a villager sent at a tree
    // walks out as a lumberjack.
    worker->carry_type = registry.get<Components::Resource>(resource).type;
    // Which villager it becomes for this target, from the dat's task lists.
    // Something that gathers as itself stays itself.
    bool hunt = false;
    worker->job = worker->gatherer >= 0 ? Components::VillagerJob::None
                                        : Villagers::JobForTarget(registry, resource, hunt);
    return true;
}

void MarchInFormation(entt::registry& registry, Physics::Grid& grid,
                      const Core::FormationMoveCommand& cmd);

// A unit in a locked formation takes the formation with it.
void Move(entt::registry& registry, Physics::Grid& grid, const Core::MoveCommand& cmd) {
    if (!Commands(registry, cmd.entity, cmd.player)) return;
    if (const auto* in = registry.try_get<Components::InFormation>(cmd.entity)) {
        MarchInFormation(registry, grid, {Formations::Members(registry, cmd.entity), cmd.target_x, cmd.target_y,
                                          in->formation, cmd.player});
        return;
    }
    WalkTo(registry, grid, cmd.entity, {cmd.target_x, cmd.target_y});
}

void Gather(entt::registry& registry, const Core::GatherCommand& cmd) {
    if (!Commands(registry, cmd.entity, cmd.player)) return;
    GatherFrom(registry, cmd.entity, cmd.target_resource);
}

// Whether a unit is a villager: one that gathers by whichever job it is on.
bool IsVillager(const entt::registry& registry, entt::entity unit) {
    const auto* worker = registry.try_get<Components::Worker>(unit);
    return worker != nullptr && worker->gatherer < 0;
}

// Where each villager of a group sent to `at` stands, apart from the rest:
// villagers march in no formation, but come to stand side by side about the
// point, each at its own pace.
std::vector<std::pair<entt::entity, Components::Vector2D>> VillagerPlaces(
    const entt::registry& registry, const std::vector<entt::entity>& villagers, Components::Vector2D at) {
    return Formations::Layout(registry, villagers, at, Core::Formation::Line);
}

// A group marches together: each to its place in the formation, none faster
// than the slowest of them. Villagers take no part in the formation (see
// VillagerPlaces). A locked formation any of them is in goes whole, and in
// its own formation when it is the only one.
void MarchInFormation(entt::registry& registry, Physics::Grid& grid,
                      const Core::FormationMoveCommand& cmd) {
    std::vector<entt::entity> sent;
    std::vector<uint32_t> groups;
    Core::Formation formation = cmd.formation;
    for (auto unit : cmd.units) {
        const auto* in = registry.try_get<Components::InFormation>(unit);
        if (in == nullptr) {
            sent.push_back(unit);
            continue;
        }
        if (std::find(groups.begin(), groups.end(), in->group) != groups.end()) continue;
        groups.push_back(in->group);
        formation = in->formation;
        for (entt::entity member : Formations::Members(registry, unit)) sent.push_back(member);
    }
    if (groups.size() > 1) formation = cmd.formation;
    std::sort(sent.begin(), sent.end());
    sent.erase(std::unique(sent.begin(), sent.end()), sent.end());
    std::vector<entt::entity> ours;
    std::vector<entt::entity> villagers;
    for (auto unit : sent) {
        if (!Commands(registry, unit, cmd.player)) continue;
        (IsVillager(registry, unit) ? villagers : ours).push_back(unit);
    }
    // None of them plans round the rest, which set off with it.
    std::vector<entt::entity> group = ours;
    group.insert(group.end(), villagers.begin(), villagers.end());
    std::sort(group.begin(), group.end());
    Navigation::Marching(&group);
    for (const auto& place : VillagerPlaces(registry, villagers, {cmd.target_x, cmd.target_y})) {
        WalkTo(registry, grid, place.first, place.second);
    }
    const auto places = Formations::Layout(registry, ours, {cmd.target_x, cmd.target_y}, formation);
    Core::Fixed32 slowest{};
    for (const auto& place : places) {
        const auto* rules = Combat::FightingRules(registry, place.first);
        if (rules == nullptr) continue;
        if (slowest.value == 0 || rules->speed < slowest) slowest = rules->speed;
    }
    for (const auto& place : places) {
        WalkTo(registry, grid, place.first, place.second);
        if (auto* path = registry.try_get<Components::Path>(place.first)) {
            path->speed_cap = slowest;
        }
    }
    Navigation::Marching(nullptr);
}

// A formation chosen: the soldiers among `units` that can march are locked
// into it, and form up in it where they stand.
void Form(entt::registry& registry, Physics::Grid& grid, const Core::FormCommand& cmd) {
    std::vector<entt::entity> ours;
    for (auto unit : cmd.units) {
        if (!Commands(registry, unit, cmd.player) || IsVillager(registry, unit)) continue;
        if (!registry.all_of<Components::Combat>(unit) || registry.all_of<Components::Building>(unit)) continue;
        const auto* rules = Combat::FightingRules(registry, unit);
        if (rules == nullptr || rules->speed.value <= 0) continue;
        ours.push_back(unit);
    }
    std::sort(ours.begin(), ours.end());
    ours.erase(std::unique(ours.begin(), ours.end()), ours.end());
    if (ours.size() < 2) return;
    Formations::Lock(registry, ours, cmd.formation);
    Core::Fixed32 cx{}, cy{};
    for (entt::entity unit : ours) {
        const auto& at = registry.get<Components::Transform>(unit).position;
        cx = cx + at.x;
        cy = cy + at.y;
    }
    const Core::Fixed32 n(static_cast<int>(ours.size()));
    MarchInFormation(registry, grid, {ours, cx / n, cy / n, cmd.formation, cmd.player});
}

void Disband(entt::registry& registry, const Core::DisbandCommand& cmd) {
    std::vector<entt::entity> ours;
    for (auto unit : cmd.units) {
        if (Owns(registry, unit, cmd.player)) ours.push_back(unit);
    }
    Formations::Disband(registry, ours);
}

// Whether a unit is walking somewhere and doing nothing else: what a
// waypoint is added after rather than replacing.
bool Walking(const entt::registry& registry, entt::entity unit) {
    const auto* path = registry.try_get<Components::Path>(unit);
    if (path == nullptr || !path->has_goal) return false;
    if (const auto* worker = registry.try_get<Components::Worker>(unit);
        worker != nullptr && worker->state != Core::UnitState::Idle && worker->state != Core::UnitState::Moving) {
        return false;
    }
    const auto* combat = registry.try_get<Components::Combat>(unit);
    return combat == nullptr || combat->target == entt::null;
}

// Shift and the right button: each unit's place at the point, in the group's
// formation, added to its route -- or walked to now, by a unit with nowhere
// to go.
void AddWaypoint(entt::registry& registry, Physics::Grid& grid, const Core::WaypointCommand& cmd) {
    std::vector<entt::entity> ours;
    std::vector<entt::entity> villagers;
    for (auto unit : cmd.units) {
        if (!Commands(registry, unit, cmd.player)) continue;
        (IsVillager(registry, unit) ? villagers : ours).push_back(unit);
    }
    // Villagers each to its own place at the point, as with a move.
    for (const auto& [unit, place] : VillagerPlaces(registry, villagers, {cmd.target_x, cmd.target_y})) {
        if (auto* queue = registry.try_get<Components::Waypoints>(unit); queue != nullptr || Walking(registry, unit)) {
            auto& route = queue != nullptr ? *queue : registry.emplace<Components::Waypoints>(unit);
            if (route.count < Components::Waypoints::kMax) route.points[route.count++] = place;
            continue;
        }
        WalkTo(registry, grid, unit, place);
        registry.emplace<Components::Waypoints>(unit);
    }
    if (ours.empty()) return;
    const auto places = ours.size() == 1
        ? std::vector<std::pair<entt::entity, Components::Vector2D>>{{ours.front(), {cmd.target_x, cmd.target_y}}}
        : Formations::Layout(registry, ours, {cmd.target_x, cmd.target_y}, cmd.formation);
    Core::Fixed32 slowest{};
    if (places.size() > 1) {
        for (const auto& place : places) {
            const auto* rules = Combat::FightingRules(registry, place.first);
            if (rules != nullptr && (slowest.value == 0 || rules->speed < slowest)) slowest = rules->speed;
        }
    }
    for (const auto& [unit, at] : places) {
        if (auto* queue = registry.try_get<Components::Waypoints>(unit); queue != nullptr || Walking(registry, unit)) {
            auto& route = queue != nullptr ? *queue : registry.emplace<Components::Waypoints>(unit);
            if (route.count < Components::Waypoints::kMax) route.points[route.count++] = at;
            route.speed_cap = slowest;
            continue;
        }
        WalkTo(registry, grid, unit, at);
        if (auto* path = registry.try_get<Components::Path>(unit)) path->speed_cap = slowest;
        registry.emplace<Components::Waypoints>(unit).speed_cap = slowest;
    }
}

void SetStance(entt::registry& registry, const Core::StanceCommand& cmd) {
    if (!Owns(registry, cmd.entity, cmd.player)) return;
    auto* combat = registry.try_get<Components::Combat>(cmd.entity);
    if (combat == nullptr || cmd.stance > static_cast<uint8_t>(Components::Stance::NoAttack)) {
        return;
    }
    combat->stance = static_cast<Components::Stance>(cmd.stance);
    // No attack drops whatever fight it was in.
    if (combat->stance == Components::Stance::NoAttack) {
        combat->state = Components::CombatState::Idle;
        combat->target = entt::null;
        combat->ordered = false;
    }
}

void Stop(entt::registry& registry, const Core::StopCommand& cmd) {
    if (!Commands(registry, cmd.entity, cmd.player)) return;
    // Stopping a trebuchet packing or setting up leaves it as it was.
    Packing::Cancel(registry, cmd.entity);
    DropCurrentJob(registry, cmd.entity);
    registry.remove<Components::Path>(cmd.entity);
}

void Rally(entt::registry& registry, const Core::RallyCommand& cmd) {
    if (!Owns(registry, cmd.building, cmd.player)) return;
    if (!registry.all_of<Components::Building>(cmd.building)) return;
    // Set on the building itself, it is taken away.
    if (cmd.target == cmd.building) {
        registry.remove<Components::RallyPoint>(cmd.building);
        return;
    }
    registry.emplace_or_replace<Components::RallyPoint>(
        cmd.building, Components::Vector2D{cmd.x, cmd.y},
        registry.valid(cmd.target) ? cmd.target : entt::null);
}

void Attack(entt::registry& registry, const Physics::Grid& grid, const Core::AttackCommand& cmd) {
    if (!Commands(registry, cmd.entity, cmd.player)) return;
    // A foundation not yet begun is nobody's target.
    if (!Buildings::Begun(registry, cmd.target_entity)) return;
    if (Packing::Busy(registry, cmd.entity)) return;
    // A packed trebuchet sent at an enemy goes to set up within range of it
    // and opens fire (its Unpack and Attack task).
    if (Packing::Packed(registry, cmd.entity)) {
        if (Owns(registry, cmd.target_entity, cmd.player)) return;
        DropCurrentJob(registry, cmd.entity);
        Packing::Assault(registry, grid, cmd.entity, cmd.target_entity);
        return;
    }
    // A monk's attack is a conversion.
    if (registry.all_of<Components::Monk>(cmd.entity) &&
        Monks::CanConvert(registry, cmd.entity, cmd.target_entity)) {
        DropCurrentJob(registry, cmd.entity);
        registry.remove<Components::Path>(cmd.entity);
        Monks::Convert(registry, cmd.entity, cmd.target_entity);
        return;
    }
    if (!Combat::Alive(registry, cmd.target_entity) || cmd.target_entity == cmd.entity) return;
    if (!registry.all_of<Components::Combat>(cmd.entity)) return;
    // Never one's own.
    if (Owns(registry, cmd.target_entity, cmd.player)) return;
    // Nor an ally's. A neutral's may be attacked by order.
    if (const auto* theirs = registry.try_get<Components::Owner>(cmd.target_entity);
        theirs != nullptr && Diplomacy::StanceOf(registry, cmd.player, theirs->player_id) ==
                                 Diplomacy::Stance::Ally) {
        return;
    }
    DropCurrentJob(registry, cmd.entity);
    registry.remove<Components::Path>(cmd.entity);
    Combat::Engage(registry, cmd.entity, cmd.target_entity, true);
}

void PutToWork(entt::registry& registry, entt::entity worker_entity, entt::entity site) {
    DropCurrentJob(registry, worker_entity);
    // Routing is the worker system's: it walks to the edge of the footprint
    // rather than into it.
    registry.remove<Components::Path>(worker_entity);
    auto& worker = registry.get<Components::Worker>(worker_entity);
    worker.state = Core::UnitState::Building;
    worker.target_building = site;
}

void Build(entt::registry& registry, Physics::Grid& grid, const Core::BuildCommand& cmd) {
    if (!Commands(registry, cmd.worker, cmd.player)) return;
    if (!registry.all_of<Components::Worker>(cmd.worker)) return;
    // A Gate button turns to the wall it is put on: which of the dat's four
    // gates goes up depends on the wall under it.
    int unit_id = cmd.unit_id;
    const Core::Rules::UnitRules* asked = Core::Rules::Unit(cmd.unit_id);
    const bool gate = Walls::IsGate(cmd.unit_id) && asked != nullptr && asked->stack_unit >= 0;
    if (gate) unit_id = Walls::GateFor(registry, cmd.player, cmd.unit_id, cmd.target_x, cmd.target_y);
    const int placed = Buildings::PlacedUnit(unit_id);

    // The same building already going up on that tile: join it, at no cost.
    auto sites = registry.view<Components::Building, Components::Transform,
                               Components::Identity, Components::Owner>();
    for (auto site : sites) {
        if (sites.get<Components::Building>(site).state !=
            Components::BuildingState::UnderConstruction) {
            continue;
        }
        if (sites.get<Components::Owner>(site).player_id != cmd.player) continue;
        if (sites.get<Components::Identity>(site).unit_id != placed) continue;
        const auto& at = sites.get<Components::Transform>(site).position;
        if (Physics::TileOf(at.x) == cmd.target_x && Physics::TileOf(at.y) == cmd.target_y) {
            PutToWork(registry, cmd.worker, site);
            return;
        }
    }

    // A new one: the player has to have it, the worker has to be one that
    // builds it -- the dat trains a villager's buildings at the builder
    // villager and a fish trap at the fishing ship -- the ground has to be
    // open, and the price has to be met: refused outright otherwise.
    const Core::Rules::UnitRules* button =
        Players::UnitOf(registry, cmd.player, cmd.unit_id);
    if (button == nullptr || !button->enabled) return;
    {
        const auto& worker = registry.get<Components::Worker>(cmd.worker);
        const int builder = worker.gatherer >= 0
            ? registry.get<Components::Identity>(cmd.worker).unit_id
            : Villagers::UnitId(Components::VillagerJob::Build, false);
        if (button->train_loc != builder) return;
    }
    // A gate may stand over the player's own wall, which it replaces.
    if (!Walls::SiteOpen(registry, grid, cmd.player, cmd.unit_id, cmd.target_x, cmd.target_y)) {
        return;
    }
    Players::Player* player = Players::Find(registry, cmd.player);
    if (player == nullptr || !Players::CanPay(*player, button->costs, button->cost_count)) {
        return;
    }
    Players::Pay(*player, button->costs, button->cost_count, -1);
    if (gate) Walls::ClearForGate(registry, grid, cmd.player, unit_id, cmd.target_x, cmd.target_y);

    const entt::entity site = Buildings::Place(registry, grid, unit_id, cmd.player,
                                               cmd.target_x, cmd.target_y, false);
    if (site != entt::null) PutToWork(registry, cmd.worker, site);
}

// Villagers sent to mend something: a site still going up of their own they
// carry on building, as a build order at it would; anything finished and
// damaged that the repairer's tasks name, they repair (ecs/Repairs).
void Repair(entt::registry& registry, const Core::RepairCommand& cmd) {
    if (!registry.valid(cmd.target)) return;
    const auto* site = registry.try_get<Components::Building>(cmd.target);
    const bool unfinished = site != nullptr &&
                            site->state == Components::BuildingState::UnderConstruction;
    for (entt::entity worker : cmd.workers) {
        if (!Commands(registry, worker, cmd.player)) continue;
        const auto* w = registry.try_get<Components::Worker>(worker);
        if (w == nullptr || w->gatherer >= 0) continue;
        if (unfinished) {
            if (Owns(registry, cmd.target, cmd.player)) PutToWork(registry, worker, cmd.target);
            continue;
        }
        if (!Repairs::CanRepair(registry, worker, cmd.target)) continue;
        DropCurrentJob(registry, worker);
        registry.remove<Components::Path>(worker);
        auto& job = registry.get<Components::Worker>(worker);
        job.state = Core::UnitState::Repairing;
        job.target_building = cmd.target;
        job.repair_progress = Core::Fixed32(0);
        job.build_attempts = 0;
    }
}

// A wall dragged out: its line of foundations laid and paid for, and every
// worker sent to the segment nearest it. Builders move on along the line by
// themselves as each segment is finished (systems/WorkerSystem).
void LayWall(entt::registry& registry, Physics::Grid& grid, const Core::WallCommand& cmd) {
    std::vector<entt::entity> crew;
    const int builder = Villagers::UnitId(Components::VillagerJob::Build, false);
    for (entt::entity worker : cmd.workers) {
        if (!Commands(registry, worker, cmd.player)) continue;
        const auto* w = registry.try_get<Components::Worker>(worker);
        if (w == nullptr || w->gatherer >= 0) continue;
        crew.push_back(worker);
    }
    if (crew.empty() || !Walls::IsWall(cmd.unit_id)) return;
    const Core::Rules::UnitRules* button = Players::UnitOf(registry, cmd.player, cmd.unit_id);
    if (button == nullptr || !button->enabled || button->train_loc != builder) return;
    const std::vector<entt::entity> sites = Walls::Lay(registry, grid, cmd.player, cmd.unit_id,
                                                       cmd.from_x, cmd.from_y, cmd.to_x, cmd.to_y);
    if (sites.empty()) return;
    for (entt::entity worker : crew) {
        const auto& at = registry.get<Components::Transform>(worker).position;
        entt::entity nearest = entt::null;
        Core::Fixed32 best{};
        for (entt::entity site : sites) {
            const auto& p = registry.get<Components::Transform>(site).position;
            const Core::Fixed32 d = Core::FixedMath::Distance(p.x - at.x, p.y - at.y);
            if (nearest == entt::null || d < best) {
                nearest = site;
                best = d;
            }
        }
        PutToWork(registry, worker, nearest);
    }
}

void Garrison(entt::registry& registry, const Core::GarrisonCommand& cmd) {
    if (!Commands(registry, cmd.unit, cmd.player) || !Owns(registry, cmd.building, cmd.player)) {
        return;
    }
    Garrisons::Send(registry, cmd.unit, cmd.building);
}

// A worker sent to drop off what it carries at one of the player's buildings:
// it forgets the job it was on, walks there, hands the load over and stands.
void DropOff(entt::registry& registry, const Core::DropOffCommand& cmd) {
    if (!Commands(registry, cmd.unit, cmd.player) ||
        !DropSites::Takes(registry, cmd.unit, cmd.building)) {
        return;
    }
    DropCurrentJob(registry, cmd.unit);
    registry.remove<Components::Path>(cmd.unit);
    auto& worker = registry.get<Components::Worker>(cmd.unit);
    worker.target_resource = entt::null;
    worker.target_building = entt::null;
    worker.felling = entt::null;
    worker.fell_ms = 0;
    worker.target_dropoff = cmd.building;
    worker.ordered_drop = true;
    worker.state = Core::UnitState::Returning;
}

void Ungarrison(entt::registry& registry, const Physics::Grid& grid,
                const Core::UngarrisonCommand& cmd) {
    if (!Owns(registry, cmd.building, cmd.player)) return;
    Garrisons::Eject(registry, grid, cmd.building);
}

void TownBell(entt::registry& registry, const Physics::Grid& grid,
              const Core::TownBellCommand& cmd) {
    if (!Owns(registry, cmd.town_center, cmd.player)) return;
    Garrisons::RingBell(registry, grid, cmd.town_center);
}

// A standing order for each of the player's units that can walk.
template <typename Give>
void Standing(entt::registry& registry, const std::vector<entt::entity>& units,
              Core::PlayerId player, Give give) {
    for (entt::entity unit : units) {
        if (!Commands(registry, unit, player) || registry.all_of<Components::Building>(unit)) {
            continue;
        }
        DropCurrentJob(registry, unit);
        give(unit);
    }
}

void Delete(entt::registry& registry, Physics::Grid& grid, const Core::DeleteCommand& cmd) {
    if (!Owns(registry, cmd.building, cmd.player)) return;
    if (registry.all_of<Components::Building>(cmd.building)) {
        // What is not yet built of it comes back; a foundation never begun
        // leaves nothing behind.
        Buildings::Refund(registry, cmd.building);
        Buildings::Destroy(registry, grid, cmd.building, Buildings::Begun(registry, cmd.building));
        return;
    }
    // A unit the player deletes dies where it stands: its dying graphic, then
    // its body.
    const auto* health = registry.try_get<Components::Health>(cmd.building);
    if (health != nullptr && health->current_hp > 0) Mortality::Kill(registry, grid, cmd.building);
}

// A computer player's cc-add-resource: only a seat the lobby gave the
// computer may give itself resources.
void ComputerResource(entt::registry& registry, const Core::ComputerResourceCommand& cmd) {
    const auto* setup = registry.ctx().find<Core::GameSetup>();
    if (setup == nullptr || cmd.player == Core::PLAYER_GAIA || cmd.player >= Core::kMaxPlayers) return;
    if (!setup->players[cmd.player].computer || cmd.amount <= 0) return;
    if (cmd.resource < 0 || cmd.resource >= static_cast<int>(Core::ResourceType::COUNT)) return;
    if (Players::Player* player = Players::Find(registry, cmd.player)) {
        Players::AddStock(*player, static_cast<Core::ResourceType>(cmd.resource), cmd.amount);
    }
}

} // namespace

void Execute(entt::registry& registry, Physics::Grid& grid, const Core::Command& command) {
    std::visit([&](const auto& cmd) {
        using T = std::decay_t<decltype(cmd)>;
        if constexpr (std::is_same_v<T, Core::MoveCommand>) {
            Move(registry, grid, cmd);
        } else if constexpr (std::is_same_v<T, Core::GatherCommand>) {
            Gather(registry, cmd);
        } else if constexpr (std::is_same_v<T, Core::AttackCommand>) {
            Attack(registry, grid, cmd);
        } else if constexpr (std::is_same_v<T, Core::BuildCommand>) {
            Build(registry, grid, cmd);
        } else if constexpr (std::is_same_v<T, Core::DeleteCommand>) {
            Delete(registry, grid, cmd);
        } else if constexpr (std::is_same_v<T, Core::ResignCommand>) {
            Victory::Resign(registry, grid, cmd.player);
        } else if constexpr (std::is_same_v<T, Core::TradeCommand>) {
            if (Commands(registry, cmd.unit, cmd.player) &&
                Trade::CanTrade(registry, cmd.unit, cmd.partner)) {
                DropCurrentJob(registry, cmd.unit);
                Trade::Start(registry, grid, cmd.unit, cmd.partner);
            }
        } else if constexpr (std::is_same_v<T, Core::DiplomacyCommand>) {
            // Refused by the diplomacy module while teams are locked.
            Diplomacy::SetStance(registry, cmd.player, cmd.other,
                                 static_cast<Diplomacy::Stance>(cmd.stance));
        } else if constexpr (std::is_same_v<T, Core::AlliedVictoryCommand>) {
            Diplomacy::SetAlliedVictory(registry, cmd.player, cmd.on);
        } else if constexpr (std::is_same_v<T, Core::DifficultyCommand>) {
            if (Players::Player* p = Players::Find(registry, cmd.player)) {
                const uint8_t value = std::min<uint8_t>(cmd.value, 100);
                if (cmd.parameter == 0) p->dodge = value;
                else if (cmd.parameter == 1) p->keep_distance = value;
            }
        } else if constexpr (std::is_same_v<T, Core::ChatCommand>) {
            Chat::Post(registry, cmd.player, static_cast<Chat::Audience>(cmd.audience), cmd.to,
                       cmd.text);
        } else if constexpr (std::is_same_v<T, Core::FlareCommand>) {
            Flares::Send(registry, cmd.player, {cmd.x, cmd.y});
        } else if constexpr (std::is_same_v<T, Core::TreasonCommand>) {
            Spies::Treason(registry, cmd.castle, cmd.player);
        } else if constexpr (std::is_same_v<T, Core::SelectionCommand>) {
            // Only a scenario's triggers keep it.
            Triggers::NoteSelection(registry, cmd.player, cmd.units);
        } else if constexpr (std::is_same_v<T, Core::TributeCommand>) {
            // Who may give whom what, and whether it is paid for, is the
            // tribute module's.
            Tribute::Send(registry, cmd.player, cmd.recipient, cmd.resource, cmd.amount);
        } else if constexpr (std::is_same_v<T, Core::MarketCommand>) {
            // The market module checks the Market is the player's and
            // finished, and that the first lot can be paid for.
            if (cmd.buy) {
                Market::Buy(registry, cmd.market, cmd.player, cmd.resource, cmd.lots);
            } else {
                Market::Sell(registry, cmd.market, cmd.player, cmd.resource, cmd.lots);
            }
        } else if constexpr (std::is_same_v<T, Core::WallCommand>) {
            LayWall(registry, grid, cmd);
        } else if constexpr (std::is_same_v<T, Core::RepairCommand>) {
            Repair(registry, cmd);
        } else if constexpr (std::is_same_v<T, Core::CheatCommand>) {
            Cheats::Apply(registry, grid, cmd.player, cmd.code);
        } else if constexpr (std::is_same_v<T, Core::PackCommand>) {
            for (entt::entity unit : cmd.units) {
                if (!Commands(registry, unit, cmd.player)) continue;
                DropCurrentJob(registry, unit);
                Packing::Start(registry, unit, cmd.pack);
            }
        } else if constexpr (std::is_same_v<T, Core::GateCommand>) {
            if (Owns(registry, cmd.gate, cmd.player)) {
                Walls::SetLocked(registry, grid, cmd.gate, cmd.lock);
            }
        } else if constexpr (std::is_same_v<T, Core::ComputerResourceCommand>) {
            ComputerResource(registry, cmd);
        } else if constexpr (std::is_same_v<T, Core::TrainCommand>) {
            Production::Enqueue(registry, cmd.building, cmd.unit_id, cmd.player);
        } else if constexpr (std::is_same_v<T, Core::CancelTrainCommand>) {
            Production::Cancel(registry, cmd.building, cmd.slot, cmd.player);
        } else if constexpr (std::is_same_v<T, Core::ResearchCommand>) {
            Production::EnqueueTech(registry, cmd.building, cmd.tech_id, cmd.player);
        } else if constexpr (std::is_same_v<T, Core::RallyCommand>) {
            Rally(registry, cmd);
        } else if constexpr (std::is_same_v<T, Core::FormationMoveCommand>) {
            MarchInFormation(registry, grid, cmd);
        } else if constexpr (std::is_same_v<T, Core::FormCommand>) {
            Form(registry, grid, cmd);
        } else if constexpr (std::is_same_v<T, Core::DisbandCommand>) {
            Disband(registry, cmd);
        } else if constexpr (std::is_same_v<T, Core::WaypointCommand>) {
            AddWaypoint(registry, grid, cmd);
        } else if constexpr (std::is_same_v<T, Core::StanceCommand>) {
            SetStance(registry, cmd);
        } else if constexpr (std::is_same_v<T, Core::StopCommand>) {
            Stop(registry, cmd);
        } else if constexpr (std::is_same_v<T, Core::GarrisonCommand>) {
            Garrison(registry, cmd);
        } else if constexpr (std::is_same_v<T, Core::DropOffCommand>) {
            DropOff(registry, cmd);
        } else if constexpr (std::is_same_v<T, Core::UngarrisonCommand>) {
            Ungarrison(registry, grid, cmd);
        } else if constexpr (std::is_same_v<T, Core::TownBellCommand>) {
            TownBell(registry, grid, cmd);
        } else if constexpr (std::is_same_v<T, Core::BackToWorkCommand>) {
            if (Owns(registry, cmd.building, cmd.player)) Garrisons::BackToWork(registry, grid, cmd.building);
        } else if constexpr (std::is_same_v<T, Core::HealCommand>) {
            if (Commands(registry, cmd.unit, cmd.player)) {
                DropCurrentJob(registry, cmd.unit);
                registry.remove<Components::Path>(cmd.unit);
                Monks::Heal(registry, cmd.unit, cmd.target);
            }
        } else if constexpr (std::is_same_v<T, Core::RelicCommand>) {
            if (Commands(registry, cmd.unit, cmd.player) &&
                (Relics::CanFetch(registry, cmd.unit, cmd.target) ||
                 Relics::CanDeposit(registry, cmd.unit, cmd.target))) {
                DropCurrentJob(registry, cmd.unit);
                registry.remove<Components::Path>(cmd.unit);
                if (!Relics::Fetch(registry, cmd.unit, cmd.target)) {
                    Relics::Deposit(registry, cmd.unit, cmd.target);
                }
            }
        } else if constexpr (std::is_same_v<T, Core::UnloadCommand>) {
            if (Commands(registry, cmd.transport, cmd.player)) {
                Garrisons::Unload(registry, grid, cmd.transport, {cmd.target_x, cmd.target_y});
            }
        } else if constexpr (std::is_same_v<T, Core::PatrolCommand>) {
            Standing(registry, cmd.units, cmd.player, [&](entt::entity unit) {
                Duties::Patrol(registry, unit, {cmd.target_x, cmd.target_y});
            });
        } else if constexpr (std::is_same_v<T, Core::GuardCommand>) {
            Standing(registry, cmd.units, cmd.player, [&](entt::entity unit) {
                Duties::Guard(registry, unit, cmd.charge);
            });
        } else if constexpr (std::is_same_v<T, Core::FollowCommand>) {
            Standing(registry, cmd.units, cmd.player, [&](entt::entity unit) {
                Duties::Follow(registry, unit, cmd.charge);
            });
        }
    }, command);
}

void FollowRally(entt::registry& registry, const Physics::Grid& grid, entt::entity unit,
                 const Components::Vector2D& at, entt::entity target) {
    if (!registry.valid(unit)) return;
    if (GatherFrom(registry, unit, target)) return;
    // A trade unit rallied onto a foreign Market or Dock starts trading with
    // it.
    if (Trade::Start(registry, grid, unit, target)) return;
    WalkTo(registry, grid, unit, at);
}

void Interrupt(entt::registry& registry, entt::entity unit) {
    const auto* worker = registry.try_get<Components::Worker>(unit);
    const bool working = worker != nullptr && worker->state != Core::UnitState::Idle;
    const Components::Interrupted kept{working ? worker->target_resource : entt::null,
                                       working ? worker->target_building : entt::null};
    DropCurrentJob(registry, unit);
    registry.remove<Components::Path>(unit);
    if (working) registry.emplace<Components::Interrupted>(unit, kept);
}

void GoBackToWork(entt::registry& registry, const Physics::Grid& grid, entt::entity unit,
                  entt::entity resource, entt::entity site) {
    (void)grid;
    if (!registry.valid(unit) || !registry.all_of<Components::Worker>(unit)) return;
    if (GatherFrom(registry, unit, resource)) return;
    if (registry.valid(site) && registry.all_of<Components::Building>(site) &&
        registry.get<Components::Building>(site).state ==
            Components::BuildingState::UnderConstruction) {
        PutToWork(registry, unit, site);
    }
}

void WalkOnToWaypoints(entt::registry& registry, const Physics::Grid& grid) {
    std::vector<entt::entity> routed;
    for (auto unit : registry.view<Components::Waypoints>()) routed.push_back(unit);
    std::sort(routed.begin(), routed.end());
    for (entt::entity unit : routed) {
        const auto* path = registry.try_get<Components::Path>(unit);
        if (path != nullptr && path->has_goal) continue;   // still on its way
        Components::Waypoints route = registry.get<Components::Waypoints>(unit);
        if (route.count == 0) {
            registry.remove<Components::Waypoints>(unit);
            continue;
        }
        const Components::Vector2D next = route.points[0];
        for (int i = 1; i < route.count; ++i) route.points[static_cast<size_t>(i - 1)] = route.points[static_cast<size_t>(i)];
        --route.count;
        // Walking on drops the unit's job, the route with it: put it back.
        WalkTo(registry, grid, unit, next);
        if (auto* leg = registry.try_get<Components::Path>(unit)) leg->speed_cap = route.speed_cap;
        registry.emplace_or_replace<Components::Waypoints>(unit, route);
    }
}

} // namespace ECS::Orders
