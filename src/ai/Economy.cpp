#include "ai/Economy.h"

#include <algorithm>
#include <array>

#include "ai/Catalog.h"
#include "ai/Facts.h"
#include "ai/Placement.h"
#include "core/FixedMath.h"
#include "core/Rules.h"
#include "ecs/Animals.h"
#include "ecs/BuildingGeometry.h"
#include "ecs/Buildings.h"
#include "ecs/Combat.h"
#include "ecs/DropSites.h"
#include "ecs/Players.h"
#include "ecs/Score.h"
#include "ecs/Vision.h"
#include "ecs/components/Building.h"
#include "ecs/components/Combat.h"
#include "ecs/components/Identity.h"
#include "ecs/components/Mortal.h"
#include "ecs/components/Owner.h"
#include "ecs/components/Path.h"
#include "ecs/components/Resource.h"
#include "ecs/components/Transform.h"
#include "ecs/components/Worker.h"
#include "pathfinding/Pathfinder.h"

namespace AI::Economy {

namespace {

namespace R = Core::Rules;
using Vector2D = ECS::Components::Vector2D;

constexpr int kFarmClass = 49;
constexpr int kForageClass = 7;
constexpr int kSheepClass = 58;
constexpr int kFishClass = 5;
constexpr int kShoreFishClass = 33;
// Retasked villagers per pass: the shares are reached over a few passes.
constexpr int kRetasksPerPass = 2;
// A sheep further than this from home is walked in.
const Core::Fixed32 kHerdAway(5);
const Core::Fixed32 kHerdAt(4);
// The dat's class of the wild boar (and the wolf, which carries no food).
constexpr int kBoarClass = 10;
// An idle villager this near a farm nobody works is its builder.
const Core::Fixed32 kBesideFarm(3);
// Food gathered further from home than this is "far off", for leaving it for
// an idle farm.
const Core::Fixed32 kFarFood(10);
// Food gatherers a boar hunt leaves at the food they were on.
constexpr int kLeftAtFood = 2;
// What a gatherer must hand in before it may be sent to another resource,
// with sn-use-by-type-max-gathering: at a farm, of wood, of gold or stone.
// At other food it is all the food there was.
constexpr int kFarmTask = 50;
constexpr int kWoodTask = 75;
constexpr int kMineTask = 50;
constexpr int kFarmUnit = 50;

Core::Fixed32 Distance(const Vector2D& a, const Vector2D& b) {
    return Core::FixedMath::Distance(a.x - b.x, a.y - b.y);
}

const Vector2D& Where(const entt::registry& registry, entt::entity e) {
    return registry.get<ECS::Components::Transform>(e).position;
}

int Sn(const Turn& turn, Lang::SN sn) { return turn.mind->sn[static_cast<size_t>(sn)]; }

// Whether something the census counted still stands in the world: an order
// given earlier in the same pass -- a unit deleted, a sheep slaughtered -- can
// take it out, and its components with it.
bool Stands(const entt::registry& registry, entt::entity e) {
    return registry.valid(e) && registry.all_of<ECS::Components::Transform>(e) &&
           ECS::Combat::Alive(registry, e);
}

// The same, for a villager whose job is read.
bool Working(const entt::registry& registry, entt::entity v) {
    return Stands(registry, v) && registry.all_of<ECS::Components::Worker>(v);
}

bool Exploring(const Turn& turn, entt::entity v) {
    const auto& ex = turn.brain->explorers;
    return std::find(ex.begin(), ex.end(), v) != ex.end();
}

bool Forward(const Turn& turn, entt::entity v) {
    const auto& fb = turn.brain->forward_builders;
    return std::find(fb.begin(), fb.end(), v) != fb.end();
}

// Out of the gathering: exploring, or kept to build forward.
bool Aside(const Turn& turn, entt::entity v) { return Exploring(turn, v) || Forward(turn, v); }

int TownSize(const Turn& turn) { return std::max(8, Sn(turn, Lang::SN::MaximumTownSize)); }

// Something to gather, with how far it is from the nearest place that takes
// it, and how much worse it is than the distances say (a deer runs, a
// carcass already cut is better than a fresh kill) -- worse again for every
// villager already on it, by `crowd`: a deer wants one hunter, a tree two or
// three, a mine many.
struct Source {
    entt::entity entity = entt::null;
    Vector2D at{};
    Core::Fixed32 drop{};
    Core::Fixed32 bias{};
    Core::Fixed32 crowd{};
    bool farm = false;
};

// The cost, in tiles of walking, of each villager more on one source.
const Core::Fixed32 kCrowdPrey(30);
const Core::Fixed32 kCrowdHerd(8);
const Core::Fixed32 kCrowdCarcass(2);
const Core::Fixed32 kCrowdForage(2);
const Core::Fixed32 kCrowdTree(4);
const Core::Fixed32 kCrowdMine(1);

struct Sources {
    std::vector<Source> of[4];
};

int MaxDrop(const Turn& turn, Core::ResourceType type, bool hunt) {
    switch (type) {
    case Core::ResourceType::Food:
        return Sn(turn, hunt ? Lang::SN::MaximumHuntDropDistance : Lang::SN::MaximumFoodDropDistance);
    case Core::ResourceType::Wood:  return Sn(turn, Lang::SN::MaximumWoodDropDistance);
    case Core::ResourceType::Gold:  return Sn(turn, Lang::SN::MaximumGoldDropDistance);
    case Core::ResourceType::Stone: return Sn(turn, Lang::SN::MaximumStoneDropDistance);
    default:                        return -1;
    }
}

// Everything the player has seen to gather, by resource, each with its walk
// to the nearest drop site.
void Survey(const Turn& turn, Sources& out) {
    const entt::registry& registry = *turn.registry;
    const Census& c = *turn.census;
    struct Drop {
        ECS::Buildings::Bounds ground;
        bool takes[4];
    };
    std::vector<Drop> drops;
    for (const Census::Site& site : c.buildings) {
        if (!site.complete) continue;
        Drop d{ECS::Buildings::FootprintBounds(Where(registry, site.entity), site.unit_id), {}};
        bool any = false;
        for (int r = 0; r < 4; ++r) {
            d.takes[r] = ECS::DropSites::Accepts(site.unit_id, static_cast<Core::ResourceType>(r));
            any = any || d.takes[r];
        }
        if (any) drops.push_back(d);
    }
    if (drops.empty()) return;
    // Farms with a farmer already.
    std::vector<entt::entity> worked;
    for (entt::entity v : c.villagers) {
        if (!Working(registry, v)) continue;
        const auto& w = registry.get<ECS::Components::Worker>(v);
        if (registry.valid(w.target_resource)) worked.push_back(w.target_resource);
    }
    std::sort(worked.begin(), worked.end());

    const ECS::Vision::Map* map = ECS::Vision::MapOf(registry);
    auto stock = registry.view<const ECS::Components::Resource, const ECS::Components::Identity,
                               const ECS::Components::Transform>();
    for (auto e : stock) {
        const auto& res = stock.get<const ECS::Components::Resource>(e);
        if (res.current_amount <= 0) continue;
        const int r = static_cast<int>(res.type);
        if (r < 0 || r >= 4) continue;
        const R::UnitRules* rules = R::Unit(stock.get<const ECS::Components::Identity>(e).unit_id);
        const int cls = rules != nullptr ? rules->cls : -1;
        if (cls == kFishClass || cls == kShoreFishClass) continue;
        Source s;
        s.entity = e;
        s.at = stock.get<const ECS::Components::Transform>(e).position;
        const bool building = registry.all_of<ECS::Components::Building>(e);
        // What the player has not explored it does not know of; its own
        // farms it does.
        if (!building && !ECS::Vision::Explored(map, turn.player, Physics::TileOf(s.at.x), Physics::TileOf(s.at.y))) {
            continue;
        }
        bool hunt = false;
        if (building) {
            // A farm: the player's own, finished, and not yet worked.
            const auto* owner = registry.try_get<ECS::Components::Owner>(e);
            if (owner == nullptr || owner->player_id != turn.player || cls != kFarmClass) continue;
            if (registry.get<ECS::Components::Building>(e).state != ECS::Components::BuildingState::Complete) continue;
            if (std::binary_search(worked.begin(), worked.end(), e)) continue;
            s.farm = true;
        } else if (res.type == Core::ResourceType::Food) {
            const bool carcass = registry.all_of<ECS::Components::Corpse>(e);
            const bool herd = ECS::Animals::Herdable(registry, e);
            if (!carcass && !herd && registry.all_of<ECS::Components::Combat>(e)) continue;   // boar, wolf
            if (herd || (carcass && cls == kSheepClass)) {
                const auto* owner = registry.try_get<ECS::Components::Owner>(e);
                if (owner != nullptr && owner->player_id != turn.player && owner->player_id != Core::PLAYER_GAIA) continue;
            }
            if (carcass) {
                s.bias = Core::Fixed32(-4);
                s.crowd = kCrowdCarcass;
            } else if (ECS::Animals::Prey(registry, e)) {
                hunt = true;
                s.bias = Core::Fixed32(4);
                s.crowd = kCrowdPrey;
            } else if (herd) {
                s.crowd = kCrowdHerd;
            } else if (cls == kForageClass) {
                s.crowd = kCrowdForage;
            } else {
                hunt = true;
                s.crowd = kCrowdPrey;
            }
        } else {
            s.crowd = res.type == Core::ResourceType::Wood ? kCrowdTree : kCrowdMine;
        }
        if (!s.farm) {
            const auto range = std::equal_range(worked.begin(), worked.end(), e);
            s.bias = s.bias + s.crowd * Core::Fixed32(static_cast<int>(range.second - range.first));
        }
        Core::Fixed32 drop(1 << 20);
        for (const Drop& d : drops) {
            if (d.takes[r]) drop = std::min(drop, d.ground.DistanceFrom(s.at));
        }
        if (drop.ToInt() >= (1 << 19)) continue;
        // Game is gone after as far from a drop site as the rules'
        // sn-maximum-hunt-drop-distance allows.
        const int limit = MaxDrop(turn, res.type, hunt);
        if (limit >= 0 && drop > Core::Fixed32(limit)) continue;
        s.drop = drop;
        out.of[r].push_back(s);
    }
}

// The source of `type` that costs `villager` least, or null.
Source* Best(const Turn& turn, Sources& sources, Core::ResourceType type, entt::entity villager) {
    const entt::registry& registry = *turn.registry;
    const Vector2D& from = Where(registry, villager);
    const int region = Pathfinding::RegionAt(*turn.grid, Physics::TileOf(from.x), Physics::TileOf(from.y));
    Source* best = nullptr;
    Core::Fixed32 best_cost{};
    for (Source& s : sources.of[static_cast<int>(type)]) {
        const Core::Fixed32 cost = s.drop * Core::Fixed32(3) + Distance(s.at, from) + s.bias;
        if (best != nullptr && (cost > best_cost || (cost == best_cost && s.entity >= best->entity))) continue;
        if (!registry.valid(s.entity)) continue;
        if (!s.farm && !Pathfinding::Approachable(*turn.grid, region, Physics::TileOf(s.at.x), Physics::TileOf(s.at.y))) {
            continue;
        }
        best = &s;
        best_cost = cost;
    }
    return best;
}

// A gatherer sent to `target`: what it must hand in before it may be sent to
// another resource -- by the resource with sn-use-by-type-max-gathering, else
// sn-retask-gather-amount, at most sn-max-retask-gather-amount.
void Tasked(Turn& turn, entt::entity villager, Core::ResourceType type, entt::entity target) {
    const entt::registry& registry = *turn.registry;
    int required = std::min(Sn(turn, Lang::SN::RetaskGatherAmount), Sn(turn, Lang::SN::MaxRetaskGatherAmount));
    if (Sn(turn, Lang::SN::UseByTypeMaxGathering) == 1) {
        const auto* id = registry.try_get<ECS::Components::Identity>(target);
        const auto* res = registry.try_get<ECS::Components::Resource>(target);
        if (id != nullptr && id->unit_id == kFarmUnit) required = kFarmTask;
        else if (type == Core::ResourceType::Food) required = res != nullptr ? res->current_amount : 0;
        else if (type == Core::ResourceType::Wood) required = kWoodTask;
        else required = kMineTask;
    }
    const int delivered = registry.get<ECS::Components::Worker>(villager).delivered;
    auto& list = turn.brain->gathering;
    auto it = std::find_if(list.begin(), list.end(), [&](const Brain::Gathering& g) { return g.villager == villager; });
    if (it == list.end()) it = list.insert(list.end(), Brain::Gathering{villager});
    it->required = required;
    it->delivered_at = delivered;
}

// Whether a gatherer has handed in what it was sent for, so may be sent to
// another resource.
bool Retaskable(const Turn& turn, entt::entity villager) {
    const auto& list = turn.brain->gathering;
    const auto it = std::find_if(list.begin(), list.end(), [&](const Brain::Gathering& g) { return g.villager == villager; });
    if (it == list.end()) return true;
    return turn.registry->get<ECS::Components::Worker>(villager).delivered - it->delivered_at >= it->required;
}

void Send(Turn& turn, Sources& sources, entt::entity villager, Core::ResourceType type, Source* s) {
    Give(*turn.registry, *turn.grid, Core::GatherCommand{villager, s->entity, turn.player});
    Tasked(turn, villager, type, s->entity);
    s->bias = s->bias + s->crowd;
    // A farm takes one farmer.
    if (s->farm) {
        auto& list = sources.of[static_cast<int>(type)];
        const entt::entity taken = s->entity;
        list.erase(std::remove_if(list.begin(), list.end(), [&](const Source& x) { return x.entity == taken; }),
                   list.end());
    }
}

// The player's sheep, walked in to the Town Center so the shepherds' walk is
// short.
void Herd(Turn& turn) {
    const entt::registry& registry = *turn.registry;
    const Census& c = *turn.census;
    if (c.town_center == entt::null || c.herd.empty()) return;
    std::vector<entt::entity> eaten;
    for (entt::entity v : c.villagers) {
        if (!Working(registry, v)) continue;
        const auto& w = registry.get<ECS::Components::Worker>(v);
        if (registry.valid(w.target_resource)) eaten.push_back(w.target_resource);
    }
    for (entt::entity sheep : c.herd) {
        if (!Stands(registry, sheep)) continue;
        if (std::find(eaten.begin(), eaten.end(), sheep) != eaten.end()) continue;
        const auto* path = registry.try_get<ECS::Components::Path>(sheep);
        if (path != nullptr && path->current_waypoint_index < path->waypoints.size()) continue;
        const Vector2D& at = Where(registry, sheep);
        const Core::Fixed32 d = Distance(at, c.home);
        if (d <= kHerdAway || d.value <= 0) continue;
        const Vector2D to{c.home.x + (at.x - c.home.x) * kHerdAt / d, c.home.y + (at.y - c.home.y) * kHerdAt / d};
        Give(*turn.registry, *turn.grid, Core::MoveCommand{sheep, to.x, to.y, turn.player});
    }
}

// The enemies' units and buildings standing where the player has explored.
int EnemyThingsSeen(const Turn& turn) {
    const entt::registry& registry = *turn.registry;
    int seen = 0;
    auto things = registry.view<const ECS::Components::Owner, const ECS::Components::Transform,
                                const ECS::Components::Identity>();
    for (auto e : things) {
        const Core::PlayerId p = things.get<const ECS::Components::Owner>(e).player_id;
        if (p == Core::PLAYER_GAIA || p == turn.player || Facts::StanceToward(turn, p) != Lang::Stance::Enemy) continue;
        const Vector2D& at = things.get<const ECS::Components::Transform>(e).position;
        if (ECS::Vision::Explored(registry, turn.player, Physics::TileOf(at.x), Physics::TileOf(at.y))) ++seen;
    }
    return seen;
}

// How many villagers explore, as the game reckons it: of three or more, the
// share sn-percent-civilian-explorers asks, rounded up, no more than
// sn-total-number-explorers less the explore groups out, nor than
// sn-cap-civilian-explorers; then down to sn-minimum-civilian-explorers once
// sn-percent-exploration-required of the map is explored and more than
// sn-number-enemy-objects-required of the enemies' things seen, or else
// halved once sn-percent-half-exploration of it is. Of two, one may explore,
// within the cap; a lone villager explores.
int ExplorersWanted(const Turn& turn, int villagers) {
    const int cap = Sn(turn, Lang::SN::CapCivilianExplorers);
    if (villagers <= 0) return 0;
    if (villagers == 1) return 1;
    if (villagers == 2) {
        int gatherers = 1;
        const int gatherer_cap = Sn(turn, Lang::SN::CapCivilianGatherers);
        if (gatherer_cap >= 0 && gatherer_cap < 1) gatherers = gatherer_cap;
        int wanted = villagers - gatherers;
        if (cap >= 0) wanted = std::min(wanted, cap);
        return std::max(0, wanted);
    }
    int wanted = (villagers * Sn(turn, Lang::SN::PercentCivilianExplorers) + 99) / 100;
    int groups = turn.census->scout != entt::null && Sn(turn, Lang::SN::NumberExploreGroups) > 0 ? 1 : 0;
    for (const auto& group : turn.brain->explore_groups) groups += group.empty() ? 0 : 1;
    const int total = Sn(turn, Lang::SN::TotalNumberExplorers);
    if (wanted + groups > total) wanted = total - groups;
    if (cap >= 0) wanted = std::min(wanted, cap);
    int explored = 0;
    if (const ECS::Players::Player* seat = ECS::Players::Find(*turn.registry, turn.player)) {
        explored = ECS::Players::Resource(*seat, ECS::Score::Res::MapExplored).ToInt();
    }
    if (explored >= Sn(turn, Lang::SN::PercentExplorationRequired) &&
        EnemyThingsSeen(turn) > Sn(turn, Lang::SN::NumberEnemyObjectsRequired)) {
        wanted = std::min(wanted, Sn(turn, Lang::SN::MinimumCivilianExplorers));
    } else if (explored >= Sn(turn, Lang::SN::PercentHalfExploration)) {
        wanted /= 2;
    }
    return std::max(0, wanted);
}

// Villagers exploring: as many as the strategic numbers ask.
void Explorers(Turn& turn) {
    auto& explorers = turn.brain->explorers;
    const Census& c = *turn.census;
    explorers.erase(std::remove_if(explorers.begin(), explorers.end(), [&](entt::entity e) {
                        return std::find(c.villagers.begin(), c.villagers.end(), e) == c.villagers.end();
                    }),
                    explorers.end());
    const int wanted =
        ExplorersWanted(turn, static_cast<int>(c.villagers.size() - turn.brain->forward_builders.size()));
    while (static_cast<int>(explorers.size()) > wanted) {
        const entt::entity freed = explorers.back();
        explorers.pop_back();
        Give(*turn.registry, *turn.grid, Core::StopCommand{freed, turn.player});
    }
    for (entt::entity v : c.villagers) {
        if (static_cast<int>(explorers.size()) >= wanted) break;
        if (!Working(*turn.registry, v)) continue;
        const auto& w = turn.registry->get<ECS::Components::Worker>(v);
        if (w.state == Core::UnitState::Building || Aside(turn, v)) continue;
        explorers.push_back(v);
    }
}

// Villagers kept aside to build forward: as many as
// sn-number-forward-builders asks, taken from the rest in order and stood
// down from their work; let go as fewer are asked for.
void ForwardBuilders(Turn& turn) {
    auto& kept = turn.brain->forward_builders;
    const Census& c = *turn.census;
    std::erase_if(kept, [&](entt::entity e) {
        return std::find(c.villagers.begin(), c.villagers.end(), e) == c.villagers.end();
    });
    const int wanted = std::max(0, Sn(turn, Lang::SN::NumberForwardBuilders));
    if (static_cast<int>(kept.size()) > wanted) kept.resize(static_cast<size_t>(wanted));
    for (entt::entity v : c.villagers) {
        if (static_cast<int>(kept.size()) >= wanted) break;
        if (!Working(*turn.registry, v) || Aside(turn, v)) continue;
        if (turn.registry->get<ECS::Components::Worker>(v).state == Core::UnitState::Building) continue;
        kept.push_back(v);
        Give(*turn.registry, *turn.grid, Core::StopCommand{v, turn.player});
    }
}

int BuildersWanted(const entt::registry& registry, int unit_id) {
    (void)registry;
    const int tiles = ECS::Buildings::FootprintTiles(unit_id);
    return std::clamp(tiles - 1, 1, 4);
}

// Foundations get builders, the unstaffed first, up to what the strategic
// numbers allow.
void Staff(Turn& turn, int allowed) {
    entt::registry& registry = *turn.registry;
    Census& c = *turn.census;
    int builders = c.builders;
    for (int round = 0; round < 2 && builders < allowed; ++round) {
        for (const Census::Site& site : c.buildings) {
            if (builders >= allowed) return;
            if (site.complete || !registry.valid(site.entity)) continue;
            const int on = c.BuildersOn(registry, site.entity);
            const int want = round == 0 ? 1 : BuildersWanted(registry, site.unit_id);
            if (on >= want) continue;
            const Vector2D& at = Where(registry, site.entity);
            const bool out = Distance(at, c.home) > Core::Fixed32(TownSize(turn));
            const entt::entity worker = PickWorker(turn, at, out);
            if (worker == entt::null) return;
            Give(registry, *turn.grid, Core::BuildCommand{worker, site.unit_id, Physics::TileOf(at.x),
                                                          Physics::TileOf(at.y), turn.player});
            std::erase(c.idle_villagers, worker);
            ++builders;
            ++c.builders;
        }
    }
}

// An idle villager beside a farm nobody works farms it, whatever the shares
// say, so a villager who has just finished a farm starts farming it.
bool FarmBeside(Turn& turn, Sources& sources, entt::entity villager) {
    const Vector2D& at = Where(*turn.registry, villager);
    for (Source& s : sources.of[static_cast<int>(Core::ResourceType::Food)]) {
        if (!s.farm || Distance(s.at, at) > kBesideFarm) continue;
        Send(turn, sources, villager, Core::ResourceType::Food, &s);
        return true;
    }
    return false;
}

// A food gatherer walking its loads home from far off, with a farm by home
// standing idle: once it has dropped its load it farms instead.
void ToIdleFarms(Turn& turn, Sources& sources) {
    const entt::registry& registry = *turn.registry;
    for (entt::entity v : turn.census->villagers) {
        if (!Working(registry, v)) continue;
        bool any = false;
        for (const Source& s : sources.of[static_cast<int>(Core::ResourceType::Food)]) any = any || s.farm;
        if (!any) return;
        const auto& w = registry.get<ECS::Components::Worker>(v);
        if (w.carry_type != Core::ResourceType::Food || w.state == Core::UnitState::Building) continue;
        if (!registry.valid(w.target_resource) || registry.all_of<ECS::Components::Building>(w.target_resource)) continue;
        int carried = 0;
        for (int k : w.carried) carried += k;
        if (carried > 0 || Aside(turn, v)) continue;
        const auto* at = registry.try_get<ECS::Components::Transform>(w.target_resource);
        if (at == nullptr || Distance(at->position, turn.census->home) <= kFarFood) continue;
        Source* farm = Best(turn, sources, Core::ResourceType::Food, v);
        if (farm == nullptr || !farm->farm) continue;
        Send(turn, sources, v, Core::ResourceType::Food, farm);
    }
}

void AssignIdle(Turn& turn, Sources& sources, std::array<int, 4>& working, const std::array<int, 4>& wanted) {
    const Census& c = *turn.census;
    for (entt::entity villager : c.idle_villagers) {
        if (!Working(*turn.registry, villager) || Aside(turn, villager)) continue;
        if (FarmBeside(turn, sources, villager)) {
            ++working[static_cast<size_t>(Core::ResourceType::Food)];
            continue;
        }
        std::array<int, 4> order = {0, 1, 2, 3};
        std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
            return wanted[static_cast<size_t>(a)] - working[static_cast<size_t>(a)] >
                   wanted[static_cast<size_t>(b)] - working[static_cast<size_t>(b)];
        });
        // What is wanted first; failing that, anything there is -- a
        // villager standing idle gathers nothing.
        bool sent = false;
        for (int pass = 0; pass < 2 && !sent; ++pass) {
            for (int r : order) {
                if (pass == 0 && wanted[static_cast<size_t>(r)] <= 0) continue;
                const auto type = static_cast<Core::ResourceType>(r);
                Source* s = Best(turn, sources, type, villager);
                if (s == nullptr) continue;
                Send(turn, sources, villager, type, s);
                ++working[static_cast<size_t>(r)];
                sent = true;
                break;
            }
        }
    }
}

// One resource over its share, another under: move a villager that has just
// dropped its load across.
void Rebalance(Turn& turn, Sources& sources, std::array<int, 4>& working, const std::array<int, 4>& wanted) {
    const entt::registry& registry = *turn.registry;
    for (int move = 0; move < kRetasksPerPass; ++move) {
        int over = -1, under = -1, most = 0, least = 0;
        for (int r = 0; r < 4; ++r) {
            const int d = working[static_cast<size_t>(r)] - wanted[static_cast<size_t>(r)];
            if (d > most) { most = d; over = r; }
            if (d < least && !sources.of[r].empty()) { least = d; under = r; }
        }
        if (over < 0 || under < 0) return;
        entt::entity pick = entt::null;
        for (entt::entity v : turn.census->villagers) {
            if (!Working(registry, v)) continue;
            const auto& w = registry.get<ECS::Components::Worker>(v);
            if (w.state == Core::UnitState::Building || w.state == Core::UnitState::Idle) continue;
            if (static_cast<int>(w.carry_type) != over || Aside(turn, v)) continue;
            int carried = 0;
            for (int k : w.carried) carried += k;
            if (carried > 0 || !Retaskable(turn, v)) continue;
            pick = v;
            break;
        }
        if (pick == entt::null) return;
        const auto type = static_cast<Core::ResourceType>(under);
        Source* s = Best(turn, sources, type, pick);
        if (s == nullptr) return;
        Send(turn, sources, pick, type, s);
        --working[static_cast<size_t>(over)];
        ++working[static_cast<size_t>(under)];
    }
}

// A boar is food that fights back: it is hunted only by a party of at least
// sn-minimum-boar-hunt-group-size of the food gatherers, who cut the carcass
// after.
void HuntBoar(Turn& turn) {
    entt::registry& registry = *turn.registry;
    Brain& brain = *turn.brain;
    const Census& c = *turn.census;
    if (c.town_center == entt::null) return;
    if (registry.valid(brain.boar) && ECS::Combat::Alive(registry, brain.boar) &&
        !registry.all_of<ECS::Components::Dying>(brain.boar)) {
        // Under way: whoever of the party has dropped it is sent back.
        for (entt::entity v : brain.hunters) {
            if (!registry.valid(v) || !registry.all_of<ECS::Components::Worker>(v)) continue;
            const auto& w = registry.get<ECS::Components::Worker>(v);
            if (w.target_resource != brain.boar && w.state != Core::UnitState::Building) {
                Give(registry, *turn.grid, Core::GatherCommand{v, brain.boar, turn.player});
            }
        }
        return;
    }
    brain.boar = entt::null;
    brain.hunters.clear();
    const int party = std::max(1, Sn(turn, Lang::SN::MinimumBoarHuntGroupSize));
    std::vector<entt::entity> food;
    for (entt::entity v : c.villagers) {
        if (!Working(registry, v)) continue;
        const auto& w = registry.get<ECS::Components::Worker>(v);
        if (w.state == Core::UnitState::Building || Aside(turn, v)) continue;
        if (w.carry_type == Core::ResourceType::Food && w.state != Core::UnitState::Idle) food.push_back(v);
    }
    if (static_cast<int>(food.size()) < party + kLeftAtFood) return;
    // As far from home as the rules' sn-maximum-hunt-drop-distance: the
    // Town Center is where the meat goes.
    const int reach = MaxDrop(turn, Core::ResourceType::Food, true);
    entt::entity boar = entt::null;
    Core::Fixed32 best{};
    auto animals = registry.view<const ECS::Components::Resource, const ECS::Components::Identity,
                                 const ECS::Components::Transform, const ECS::Components::Combat>();
    for (auto e : animals) {
        const auto& r = animals.get<const ECS::Components::Resource>(e);
        if (r.type != Core::ResourceType::Food || r.current_amount <= 0) continue;
        const R::UnitRules* rules = R::Unit(animals.get<const ECS::Components::Identity>(e).unit_id);
        if (rules == nullptr || rules->cls != kBoarClass || !ECS::Combat::Alive(registry, e)) continue;
        const Vector2D& at = animals.get<const ECS::Components::Transform>(e).position;
        const Core::Fixed32 d = Distance(at, c.home);
        if ((reach >= 0 && d > Core::Fixed32(reach)) || (boar != entt::null && (d > best || (d == best && e >= boar)))) continue;
        if (!ECS::Vision::Explored(registry, turn.player, Physics::TileOf(at.x), Physics::TileOf(at.y))) continue;
        boar = e;
        best = d;
    }
    if (boar == entt::null) return;
    const Vector2D& at = Where(registry, boar);
    std::stable_sort(food.begin(), food.end(), [&](entt::entity a, entt::entity b) {
        return Distance(Where(registry, a), at) < Distance(Where(registry, b), at);
    });
    brain.boar = boar;
    for (int i = 0; i < party; ++i) {
        brain.hunters.push_back(food[static_cast<size_t>(i)]);
        Give(registry, *turn.grid, Core::GatherCommand{food[static_cast<size_t>(i)], boar, turn.player});
        Tasked(turn, food[static_cast<size_t>(i)], Core::ResourceType::Food, boar);
    }
}

bool Pending(const Census& census, int slot, int allowed) {
    int n = 0;
    for (const Census::Site& site : census.buildings) {
        if (!site.complete && site.unit_id == slot) ++n;
    }
    return n >= allowed;
}

} // namespace

entt::entity PickWorker(const Turn& turn, const Vector2D& at, bool forward) {
    const entt::registry& registry = *turn.registry;
    const Census& c = *turn.census;
    entt::entity best = entt::null;
    Core::Fixed32 best_d{};
    if (forward) {
        for (entt::entity v : turn.brain->forward_builders) {
            if (!Working(registry, v) || registry.get<ECS::Components::Worker>(v).state == Core::UnitState::Building) {
                continue;
            }
            const Core::Fixed32 d = Distance(Where(registry, v), at);
            if (best == entt::null || d < best_d || (d == best_d && v < best)) {
                best = v;
                best_d = d;
            }
        }
        if (best != entt::null) return best;
    }
    auto consider = [&](entt::entity v) {
        if (!Working(registry, v) || Aside(turn, v)) return;
        const Core::Fixed32 d = Distance(Where(registry, v), at);
        if (best == entt::null || d < best_d || (d == best_d && v < best)) {
            best = v;
            best_d = d;
        }
    };
    for (entt::entity v : c.idle_villagers) consider(v);
    if (best != entt::null) return best;
    for (entt::entity v : c.villagers) {
        if (!Working(registry, v)) continue;
        const auto& worker = registry.get<ECS::Components::Worker>(v);
        if (worker.state == Core::UnitState::Building) continue;
        consider(v);
    }
    return best;
}

bool Build(Turn& turn, int slot, bool forward) {
    const R::UnitRules* rules = ECS::Players::UnitOf(*turn.registry, turn.player, slot);
    if (rules == nullptr) return false;
    // What stands once it is placed: a Town Center's button unit is placed as
    // the Town Center, which is what the census and the counts know it by.
    const int placed = ECS::Buildings::PlacedUnit(slot);
    if (Pending(*turn.census, placed, rules->cls == kFarmClass ? 2 : 1)) return false;
    int x = 0, y = 0;
    if (!Placement::Site(turn, slot, forward, x, y)) return false;
    const entt::entity worker = PickWorker(turn, {Core::Fixed32(x), Core::Fixed32(y)}, forward);
    if (worker == entt::null) return false;
    Give(*turn.registry, *turn.grid, Core::BuildCommand{worker, slot, x, y, turn.player});
    // Keep the census in step: a foundation going up, a builder on it.
    turn.census->buildings.push_back({entt::null, placed, false, true});
    std::erase(turn.census->idle_villagers, worker);
    ++turn.census->builders;
    if (turn.world != nullptr) {
        Counts& own = turn.world->all[turn.player];
        const int form = turn.world->form_of[turn.player][static_cast<size_t>(placed)];
        ++own.buildings_total[static_cast<size_t>(form)];
        ++own.building_total;
    }
    return true;
}

void Run(Turn& turn) {
    Census& c = *turn.census;
    std::erase_if(turn.brain->gathering, [&](const Brain::Gathering& g) {
        return std::find(c.villagers.begin(), c.villagers.end(), g.villager) == c.villagers.end();
    });
    if (c.villagers.empty()) return;
    Herd(turn);
    ForwardBuilders(turn);
    Explorers(turn);
    HuntBoar(turn);

    const int villagers = static_cast<int>(c.villagers.size());
    int allowed = (villagers * Sn(turn, Lang::SN::PercentCivilianBuilders) + 99) / 100;
    const int cap = Sn(turn, Lang::SN::CapCivilianBuilders);
    if (cap >= 0) allowed = std::min(allowed, cap);
    Staff(turn, allowed);

    // Forward builders at work are among the builders already.
    int standing_by = 0;
    for (entt::entity v : turn.brain->forward_builders) {
        if (Working(*turn.registry, v) &&
            turn.registry->get<ECS::Components::Worker>(v).state != Core::UnitState::Building) {
            ++standing_by;
        }
    }
    int gatherers =
        std::max(0, villagers - c.builders - static_cast<int>(turn.brain->explorers.size()) - standing_by);
    const int gatherer_cap = Sn(turn, Lang::SN::CapCivilianGatherers);
    if (gatherer_cap >= 0) gatherers = std::min(gatherers, gatherer_cap);
    const std::array<int, 4> share = {Sn(turn, Lang::SN::FoodGathererPercentage),
                                      Sn(turn, Lang::SN::WoodGathererPercentage),
                                      Sn(turn, Lang::SN::GoldGathererPercentage),
                                      Sn(turn, Lang::SN::StoneGathererPercentage)};
    std::array<int, 4> wanted{};
    for (size_t r = 0; r < 4; ++r) wanted[r] = (gatherers * share[r] + 50) / 100;
    std::array<int, 4> working = {c.gatherers[0], c.gatherers[1], c.gatherers[2], c.gatherers[3]};

    Sources sources;
    Survey(turn, sources);
    AssignIdle(turn, sources, working, wanted);
    ToIdleFarms(turn, sources);
    Rebalance(turn, sources, working, wanted);
}

} // namespace AI::Economy
