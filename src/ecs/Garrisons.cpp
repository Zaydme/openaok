#include "ecs/Garrisons.h"

#include <algorithm>
#include <vector>

#include "core/FixedMath.h"
#include "core/Rules.h"
#include "ecs/BuildingGeometry.h"
#include "ecs/Buildings.h"
#include "ecs/Combat.h"
#include "ecs/DropSites.h"
#include "ecs/Navigation.h"
#include "ecs/Orders.h"
#include "ecs/Players.h"
#include "ecs/Units.h"
#include "ecs/VillagerJobs.h"
#include "ecs/components/Building.h"
#include "ecs/components/Combat.h"
#include "ecs/components/Garrison.h"
#include "ecs/components/Health.h"
#include "ecs/components/Identity.h"
#include "ecs/components/Owner.h"
#include "ecs/components/Path.h"
#include "ecs/components/Selectable.h"
#include "ecs/components/Transform.h"
#include "ecs/components/Worker.h"
#include "pathfinding/ApproachPlanner.h"
#include "pathfinding/Pathfinder.h"

namespace ECS::Garrisons {

namespace {

namespace R = Core::Rules;

// The dat's building class: what a unit's Garrison task names to mean "into a
// building".
constexpr int kBuildingClass = 3;

// The kinds a building's garrison type admits, as bits. The dat states the
// mask; KindOf says which unit classes make up each kind.
constexpr int kTakesVillagers = 1;
constexpr int kTakesInfantry = 2;
constexpr int kTakesCavalry = 4;
constexpr int kTakesMonks = 8;

// Each unit class up to 59 is one kind, or none. A King (class 59) goes in
// where villagers do.
int KindOf(int unit_class) {
    switch (unit_class) {
    case 4: case 19: case 59:                         // civilian, the King
        return kTakesVillagers;
    case 0: case 6: case 44: case 45: case 46:        // archer, infantry, hand
    case 50: case 56:                                 // cannoneer, two-handed, pike, spear
        return kTakesInfantry;
    case 12: case 36: case 47: case 57:               // cavalry, cav archer, scout
        return kTakesCavalry;
    case 18: case 43:                                 // monk, monk with relic
        return kTakesMonks;
    default:
        return 0;
    }
}

// What banks nothing a villager carries in: the dat's tower class and the
// Castle.
constexpr int kTowerClass = 52;
constexpr int kCastle = 82;

// A unit is in the building once this close to its wall.
const Core::Fixed32 kEnterReach = Core::Fixed32(6) / Core::Fixed32(10);
// It stands this far off the wall to go in.
const Core::Fixed32 kEnterStandoff = Core::Fixed32(3) / Core::Fixed32(10);
// Routes to the door it may try before giving up.
constexpr int kMaxAttempts = 8;

// The bell's reach, from the Town Center's middle, and a shelter's from the
// villager.
const Core::Fixed32 kBellReach = Core::Fixed32(25);

// The unit classes whose damage lends a building arrows: archers, villagers,
// cavalry archers and hand cannoneers.
constexpr int kArcherClass = 0;
constexpr int kVillagerClass = 4;
constexpr int kCavalryArcherClass = 36;
constexpr int kHandCannoneerClass = 44;
// A villager's damage a second, as its arrows are reckoned, whatever its
// attack.
const Core::Fixed32 kVillagerFirepower = Core::Fixed32(5) / Core::Fixed32(2);
// The dat's attack classes counted as damage: pierce, and hand to hand.
constexpr int kPierceClass = 3;
constexpr int kMeleeClass = 4;
// What reaches no further than this, in tiles, fights hand to hand.
const Core::Fixed32 kMeleeRange = Core::Fixed32(1);

Core::PlayerId OwnerOf(const entt::registry& registry, entt::entity entity) {
    const auto* owner = registry.try_get<Components::Owner>(entity);
    return owner != nullptr ? owner->player_id : Core::PLAYER_GAIA;
}

const R::UnitRules* RulesOf(const entt::registry& registry, entt::entity entity) {
    const auto* identity = registry.try_get<Components::Identity>(entity);
    if (identity == nullptr) return nullptr;
    return Players::RulesFor(registry, entity, identity->unit_id);
}

// Whether a unit's tasks let it garrison into things of the dat's class `cls`:
// a building (3), or -- every land unit's second garrison task -- a transport
// ship (20).
bool CanGarrisonIn(const R::UnitRules& rules, int cls) {
    for (int t = 0; t < rules.task_count; ++t) {
        const R::Task& task = rules.tasks[t];
        if (task.action == R::TaskAction::Garrison && task.target_class == cls) {
            return true;
        }
    }
    return false;
}

// A holder that is not a building: a transport ship.
bool IsShip(const entt::registry& registry, entt::entity holder) {
    return !registry.all_of<Components::Building>(holder);
}

// Units already heading in, so two sent at once cannot both take the last place.
int Incoming(const entt::registry& registry, entt::entity building) {
    int count = 0;
    auto view = registry.view<const Components::Boarding>();
    for (auto unit : view) {
        if (view.get<const Components::Boarding>(unit).building == building) ++count;
    }
    return count;
}

// The ground a holder stands on: a building's footprint, a ship's body.
Buildings::Bounds GroundOf(const entt::registry& registry, entt::entity building) {
    const auto& at = registry.get<Components::Transform>(building).position;
    if (IsShip(registry, building)) {
        const R::UnitRules* rules = RulesOf(registry, building);
        const Core::Fixed32 r = rules != nullptr ? rules->size_x : Core::Fixed32(0);
        return {at.x - r, at.y - r, at.x + r, at.y + r};
    }
    return Buildings::FootprintBounds(at, registry.get<Components::Identity>(building).unit_id);
}

// How long a unit boarding a ship waits between tries at reaching it, and how
// many tries it makes -- the ship may be on its way in from the open sea.
constexpr int32_t kShipRetryMs = 500;
constexpr int kMaxShipAttempts = 60;

// How far from a point the shore is looked for: a ship's landing, a boarder's
// pick-up.
constexpr int kShoreReach = 30;

// The tile, of the ship's own sea region, nearest `near` that touches land --
// of `land_region` if that is set (-1: any land): where a transport comes in
// to the shore. False when no such shore is within reach.
bool ShoreTile(const Physics::Grid& grid, Physics::Layer sea, int sea_region, int land_region,
               Components::Vector2D near, int& out_x, int& out_y) {
    const int cx = Physics::TileOf(near.x);
    const int cy = Physics::TileOf(near.y);
    int best = 1 << 30;
    for (int y = cy - kShoreReach; y <= cy + kShoreReach; ++y) {
        for (int x = cx - kShoreReach; x <= cx + kShoreReach; ++x) {
            if (Pathfinding::RegionAt(grid, x, y, sea) != sea_region || sea_region < 0) continue;
            bool shore = false;
            for (int oy = -1; oy <= 1 && !shore; ++oy) {
                for (int ox = -1; ox <= 1 && !shore; ++ox) {
                    const int land = Pathfinding::RegionAt(grid, x + ox, y + oy);
                    shore = land >= 0 && (land_region < 0 || land == land_region);
                }
            }
            if (!shore) continue;
            const int d = (x - cx) * (x - cx) + (y - cy) * (y - cy);
            if (d < best) {
                best = d;
                out_x = x;
                out_y = y;
            }
        }
    }
    return best < (1 << 30);
}

bool Moving(const entt::registry& registry, entt::entity unit) {
    const auto* route = registry.try_get<Components::Path>(unit);
    return route != nullptr && route->current_waypoint_index < route->waypoints.size();
}

// Brings an idle transport in to the shore beside a unit waiting to board it.
void CallShip(entt::registry& registry, const Physics::Grid& grid, entt::entity ship,
              entt::entity unit) {
    if (Moving(registry, ship) || registry.all_of<Components::Landing>(ship)) return;
    const Physics::Layer sea = Navigation::LayerOf(registry, grid, ship);
    int x = 0, y = 0;
    if (!ShoreTile(grid, sea, Navigation::RegionOf(registry, grid, ship),
                   Navigation::RegionOf(registry, grid, unit),
                   registry.get<Components::Transform>(unit).position, x, y)) {
        return;
    }
    const auto& at = registry.get<Components::Transform>(ship).position;
    if (Physics::TileOf(at.x) == x && Physics::TileOf(at.y) == y) return;
    auto path = Navigation::Route(registry, grid, ship, at,
                                  {Core::Fixed32(x), Core::Fixed32(y)});
    if (!path.waypoints.empty()) registry.emplace_or_replace<Components::Path>(ship, std::move(path));
}

// Steps everyone aboard a transport out onto the land beside it, as many as
// there is ground for. Returns how many are still aboard.
int Disembark(entt::registry& registry, const Physics::Grid& grid, entt::entity ship) {
    auto* garrison = registry.try_get<Components::Garrison>(ship);
    if (garrison == nullptr) return 0;
    const Buildings::Bounds hull = GroundOf(registry, ship);
    std::vector<Components::Vector2D> placed;
    std::vector<entt::entity> still;
    const std::vector<entt::entity> aboard = garrison->inside;
    for (entt::entity unit : aboard) {
        if (!registry.valid(unit) || !registry.all_of<Components::Garrisoned>(unit)) continue;
        bool landed = false;
        // Ring by ring out from the hull, so a full load spreads up the beach.
        for (int ring = 0; ring < 4 && !landed; ++ring) {
            Pathfinding::ApproachRequest ask;
            ask.target = hull;
            ask.from = registry.get<Components::Transform>(ship).position;
            ask.standoff = Core::Fixed32(4 + 5 * ring) / Core::Fixed32(10);
            ask.clearance = Core::Fixed32(1) / Core::Fixed32(2);
            ask.taken = placed.empty() ? nullptr : placed.data();
            ask.taken_count = placed.size();
            ask.layer = Navigation::LayerOf(registry, grid, unit);
            const auto spot = Pathfinding::FindStandingSpot(grid, ask);
            if (!spot.found) continue;
            registry.remove<Components::Garrisoned>(unit);
            registry.emplace<Components::Transform>(unit, Components::Transform{spot.spot, spot.spot});
            placed.push_back(spot.spot);
            landed = true;
        }
        if (!landed) still.push_back(unit);
    }
    garrison->inside = still;
    return static_cast<int>(still.size());
}

Components::Vector2D Middle(const Buildings::Bounds& ground) {
    return {(ground.min_x + ground.max_x) / Core::Fixed32(2),
            (ground.min_y + ground.max_y) / Core::Fixed32(2)};
}

// Takes `unit` out of the world and into `building`. A villager going into a
// building -- not a tower, not the Castle -- banks what it carries.
void Enter(entt::registry& registry, entt::entity unit, entt::entity building,
           const Components::Boarding& boarding) {
    const R::UnitRules* guest = RulesOf(registry, unit);
    const R::UnitRules* shelter = RulesOf(registry, building);
    auto* worker = registry.try_get<Components::Worker>(unit);
    const auto* identity = registry.try_get<Components::Identity>(building);
    if (worker != nullptr && guest != nullptr && guest->cls == Units::kVillagerClass &&
        shelter != nullptr && shelter->cls != kTowerClass && identity != nullptr &&
        identity->unit_id != kCastle && registry.all_of<Components::Building>(building)) {
        DropSites::Bank(*worker, Players::Find(registry, OwnerOf(registry, unit)));
    }
    auto& garrison = registry.get_or_emplace<Components::Garrison>(building);
    garrison.inside.push_back(unit);
    registry.emplace_or_replace<Components::Garrisoned>(
        unit, Components::Garrisoned{building, Core::Fixed32(0), boarding.bell,
                                     boarding.resume_resource, boarding.resume_building});
    registry.remove<Components::Boarding>(unit);
    registry.remove<Components::Path>(unit);
    registry.remove<Components::Transform>(unit);
    if (auto* selectable = registry.try_get<Components::Selectable>(unit)) {
        selectable->is_selected = false;
    }
}

// Puts a unit inside back into the world at the building's door; false if
// there is no room to stand.
bool StepOut(entt::registry& registry, const Physics::Grid& grid, entt::entity unit,
             entt::entity building) {
    Components::Vector2D at{};
    if (!Buildings::Doorstep(registry, grid, building, at,
                             Navigation::LayerOf(registry, grid, unit))) {
        return false;
    }
    registry.emplace<Components::Transform>(unit, Components::Transform{at, at});
    return true;
}

// How near a building's wall a unit goes in from, box to box.
Core::Fixed32 EnterRange(const entt::registry& registry, entt::entity unit) {
    return Core::FixedMath::Max(Core::Fixed32(0), kEnterReach - Navigation::RadiusOf(registry, unit));
}

// Plans the walk to `building`'s wall.
bool RouteTo(entt::registry& registry, const Physics::Grid& grid, entt::entity unit,
             entt::entity building) {
    const auto& at = registry.get<Components::Transform>(unit).position;
    Pathfinding::ApproachRequest ask;
    ask.target = GroundOf(registry, building);
    ask.from = at;
    ask.standoff = kEnterStandoff;
    ask.clearance = Core::Fixed32(0);
    ask.layer = Navigation::LayerOf(registry, grid, unit);
    const auto plan = Pathfinding::FindStandingSpot(grid, ask);
    if (!plan.found) return false;
    // The walk ends as the unit comes within reach of going in.
    auto path = Navigation::Route(registry, grid, unit, at, plan.spot, building,
                                  EnterRange(registry, unit));
    if (path.waypoints.empty()) return false;
    registry.emplace_or_replace<Components::Path>(unit, std::move(path));
    return true;
}

// Stops whatever a unit was doing, as any new order does.
void Drop(entt::registry& registry, entt::entity unit) {
    if (auto* worker = registry.try_get<Components::Worker>(unit)) {
        worker->state = Core::UnitState::Idle;
        worker->working = false;
    }
    if (auto* combat = registry.try_get<Components::Combat>(unit)) {
        combat->state = Components::CombatState::Idle;
        combat->target = entt::null;
        combat->ordered = false;
        combat->strike_ms = -1;
        combat->has_post = false;
    }
    registry.remove<Components::Path>(unit);
}

} // namespace

int Count(const entt::registry& registry, entt::entity building) {
    const auto* garrison = registry.try_get<Components::Garrison>(building);
    return garrison != nullptr ? static_cast<int>(garrison->inside.size()) : 0;
}

int Capacity(const entt::registry& registry, entt::entity building) {
    const R::UnitRules* rules = RulesOf(registry, building);
    return rules != nullptr ? rules->garrison : 0;
}

bool Accepts(const entt::registry& registry, entt::entity building, entt::entity unit) {
    if (!registry.valid(building) || !registry.valid(unit) || building == unit) return false;
    const bool ship = IsShip(registry, building);
    const auto* state = registry.try_get<Components::Building>(building);
    if (!ship && (state == nullptr || state->state != Components::BuildingState::Complete)) {
        return false;
    }
    if (!Combat::Alive(registry, building) || !Combat::Alive(registry, unit)) return false;
    if (OwnerOf(registry, building) != OwnerOf(registry, unit)) return false;
    const R::UnitRules* shelter = RulesOf(registry, building);
    const R::UnitRules* guest = RulesOf(registry, unit);
    if (shelter == nullptr || guest == nullptr || shelter->garrison <= 0) return false;
    if (ship) {
        // A transport takes whoever's tasks name its class -- every land unit,
        // in this dat; no garrison type applies to it.
        if (!CanGarrisonIn(*guest, shelter->cls)) return false;
    } else {
        if (!CanGarrisonIn(*guest, kBuildingClass)) return false;
        if ((shelter->garrison_type & KindOf(guest->cls)) == 0) return false;
    }
    // Room, counting those already on their way -- but not this one.
    const auto* boarding = registry.try_get<Components::Boarding>(unit);
    const int coming = Incoming(registry, building) -
                       (boarding != nullptr && boarding->building == building ? 1 : 0);
    return Count(registry, building) + coming < shelter->garrison;
}

void PutInside(entt::registry& registry, entt::entity unit, entt::entity holder) {
    if (!registry.valid(unit) || !registry.valid(holder) || unit == holder) return;
    if (registry.all_of<Components::Garrisoned>(unit)) return;
    Drop(registry, unit);
    Enter(registry, unit, holder, Components::Boarding{holder});
}

bool Send(entt::registry& registry, entt::entity unit, entt::entity building) {
    if (!Accepts(registry, building, unit)) return false;
    Drop(registry, unit);
    registry.emplace_or_replace<Components::Boarding>(unit, Components::Boarding{building});
    return true;
}

void Eject(entt::registry& registry, const Physics::Grid& grid, entt::entity building) {
    auto* garrison = registry.try_get<Components::Garrison>(building);
    if (garrison == nullptr) return;
    // A transport has no door: it lands its load at the nearest shore.
    if (IsShip(registry, building)) {
        if (registry.all_of<Components::Transform>(building)) {
            Unload(registry, grid, building, registry.get<Components::Transform>(building).position);
        }
        return;
    }
    // Copied: going back to work can send a unit straight back in.
    const std::vector<entt::entity> inside = garrison->inside;
    garrison->inside.clear();
    for (entt::entity unit : inside) {
        if (!registry.valid(unit)) continue;
        const Components::Garrisoned stay = registry.get<Components::Garrisoned>(unit);
        registry.remove<Components::Garrisoned>(unit);
        if (!StepOut(registry, grid, unit, building)) {
            // Nowhere to stand: it stays in.
            garrison->inside.push_back(unit);
            registry.emplace<Components::Garrisoned>(unit, stay);
            continue;
        }
        if (stay.bell) {
            Orders::GoBackToWork(registry, grid, unit, stay.resume_resource,
                                 stay.resume_building);
        }
    }
}

bool IsTransport(const entt::registry& registry, entt::entity holder) {
    if (!registry.valid(holder) || !IsShip(registry, holder)) return false;
    const R::UnitRules* rules = RulesOf(registry, holder);
    return rules != nullptr && rules->garrison > 0;
}

void Unload(entt::registry& registry, const Physics::Grid& grid, entt::entity transport,
            Components::Vector2D at) {
    (void)grid;
    if (!IsTransport(registry, transport) || Count(registry, transport) == 0) return;
    registry.emplace_or_replace<Components::Landing>(transport, Components::Landing{at});
    registry.remove<Components::Path>(transport);
}

bool HasBell(const entt::registry& registry, entt::entity building) {
    if (!registry.valid(building) ||
        !registry.all_of<Components::Building, Components::Identity>(building)) {
        return false;
    }
    const R::UnitRules* villager = Players::UnitOf(
        registry, OwnerOf(registry, building),
        Villagers::UnitId(Components::VillagerJob::Base, false));
    return villager != nullptr &&
           villager->train_loc == registry.get<Components::Identity>(building).unit_id;
}

void RingBell(entt::registry& registry, const Physics::Grid& grid, entt::entity town_center) {
    if (!HasBell(registry, town_center)) return;
    const Core::PlayerId player = OwnerOf(registry, town_center);
    auto& bell = registry.get_or_emplace<Components::Garrison>(town_center);
    const Components::Vector2D centre = Middle(GroundOf(registry, town_center));
    auto within = [&](const Components::Vector2D& at, const Components::Vector2D& from) {
        return Core::FixedMath::Distance(at.x - from.x, at.y - from.y) <= kBellReach;
    };

    if (bell.bell_rung) {
        // Back to work: everyone the bell sent, out of every shelter in reach.
        bell.bell_rung = false;
        std::vector<entt::entity> shelters;
        auto inside = registry.view<Components::Garrisoned>();
        for (auto unit : inside) {
            const auto& stay = inside.get<Components::Garrisoned>(unit);
            if (!stay.bell || !registry.valid(stay.building)) continue;
            if (OwnerOf(registry, stay.building) != player) continue;
            if (!within(Middle(GroundOf(registry, stay.building)), centre)) continue;
            if (std::find(shelters.begin(), shelters.end(), stay.building) == shelters.end()) {
                shelters.push_back(stay.building);
            }
        }
        for (entt::entity shelter : shelters) Eject(registry, grid, shelter);
        // Those still running for cover turn round.
        std::vector<entt::entity> runners;
        auto boarding = registry.view<Components::Boarding>();
        for (auto unit : boarding) {
            if (boarding.get<Components::Boarding>(unit).bell) runners.push_back(unit);
        }
        for (entt::entity unit : runners) {
            const Components::Boarding was = registry.get<Components::Boarding>(unit);
            if (OwnerOf(registry, unit) != player) continue;
            registry.remove<Components::Boarding>(unit);
            registry.remove<Components::Path>(unit);
            Orders::GoBackToWork(registry, grid, unit, was.resume_resource, was.resume_building);
        }
        return;
    }
    bell.bell_rung = true;

    // Shelters in the bell's reach that take villagers, with their room.
    struct Shelter {
        entt::entity building;
        Components::Vector2D middle;
        int room;
    };
    std::vector<Shelter> shelters;
    auto buildings = registry.view<Components::Building, Components::Transform,
                                   Components::Identity>();
    for (auto b : buildings) {
        if (OwnerOf(registry, b) != player) continue;
        if (buildings.get<Components::Building>(b).state != Components::BuildingState::Complete) {
            continue;
        }
        const R::UnitRules* rules = RulesOf(registry, b);
        if (rules == nullptr || rules->garrison <= 0 ||
            (rules->garrison_type & kTakesVillagers) == 0) {
            continue;
        }
        const Components::Vector2D middle = Middle(GroundOf(registry, b));
        if (!within(middle, centre)) continue;
        const int room = rules->garrison - Count(registry, b) - Incoming(registry, b);
        if (room > 0) shelters.push_back({b, middle, room});
    }

    // Villagers in reach, nearest to a shelter first, so a building fills with
    // those closest to it.
    struct Runner {
        entt::entity unit;
        Core::Fixed32 nearest;
    };
    auto distance = [&](entt::entity unit, const Shelter& shelter) {
        return GroundOf(registry, shelter.building)
            .DistanceFrom(registry.get<Components::Transform>(unit).position);
    };
    std::vector<Runner> runners;
    auto villagers = registry.view<Components::Worker, Components::Transform>();
    for (auto v : villagers) {
        if (OwnerOf(registry, v) != player || !Combat::Alive(registry, v)) continue;
        if (registry.any_of<Components::Boarding>(v)) continue;
        const auto& at = villagers.get<Components::Transform>(v).position;
        if (!within(at, centre)) continue;
        Core::Fixed32 nearest(1 << 20);
        for (const Shelter& s : shelters) {
            if (within(s.middle, at)) nearest = std::min(nearest, distance(v, s));
        }
        runners.push_back({v, nearest});
    }
    std::sort(runners.begin(), runners.end(), [](const Runner& a, const Runner& b) {
        return a.nearest != b.nearest ? a.nearest < b.nearest : a.unit < b.unit;
    });

    for (const Runner& runner : runners) {
        const auto& at = registry.get<Components::Transform>(runner.unit).position;
        Shelter* best = nullptr;
        Core::Fixed32 best_gap{};
        for (Shelter& s : shelters) {
            if (s.room <= 0 || !within(s.middle, at)) continue;
            if (!Accepts(registry, s.building, runner.unit)) continue;
            const Core::Fixed32 gap = distance(runner.unit, s);
            if (best == nullptr || gap < best_gap) {
                best = &s;
                best_gap = gap;
            }
        }
        if (best == nullptr) continue;
        // Remember the job, to go back to.
        Components::Boarding boarding{best->building};
        boarding.bell = true;
        if (const auto* worker = registry.try_get<Components::Worker>(runner.unit)) {
            if (worker->state == Core::UnitState::Building) {
                boarding.resume_building = worker->target_building;
            } else if (worker->state != Core::UnitState::Idle) {
                boarding.resume_resource = worker->target_resource;
            }
        }
        Drop(registry, runner.unit);
        registry.emplace_or_replace<Components::Boarding>(runner.unit, boarding);
        --best->room;
    }
}

Core::Fixed32 Firepower(const entt::registry& registry, entt::entity entity) {
    const R::UnitRules* rules = RulesOf(registry, entity);
    if (rules == nullptr) return Core::Fixed32(0);
    if (rules->cls == kVillagerClass) return kVillagerFirepower;
    if (rules->reload.value <= 0 || rules->max_range <= kMeleeRange) return Core::Fixed32(0);
    int damage = 0;
    const R::UnitRules* missile =
        rules->secondary_projectile >= 0
            ? Players::UnitOf(registry, Players::RulesPlayer(registry, entity), rules->secondary_projectile)
            : nullptr;
    if (missile != nullptr) {
        for (int a = 0; a < missile->attack_count; ++a) {
            if (missile->attacks[a].cls == kPierceClass) damage += missile->attacks[a].amount;
        }
    } else {
        for (int a = 0; a < rules->attack_count; ++a) {
            const int cls = rules->attacks[a].cls;
            if (cls == kPierceClass || cls == kMeleeClass) damage += rules->attacks[a].amount;
        }
    }
    return Core::Fixed32(damage) / rules->reload;
}

Core::Fixed32 LentMissiles(const entt::registry& registry, entt::entity building) {
    const auto* garrison = registry.try_get<Components::Garrison>(building);
    if (garrison == nullptr || garrison->inside.empty()) return Core::Fixed32(0);
    const Core::Fixed32 own = Firepower(registry, building);
    if (own.value <= 0) return Core::Fixed32(0);
    Core::Fixed32 total(0);
    for (entt::entity unit : garrison->inside) {
        if (!registry.valid(unit)) continue;
        const R::UnitRules* rules = RulesOf(registry, unit);
        if (rules == nullptr) continue;
        // A swordsman, a monk or a knight inside adds nothing.
        if (rules->cls != kArcherClass && rules->cls != kVillagerClass &&
            rules->cls != kCavalryArcherClass && rules->cls != kHandCannoneerClass) {
            continue;
        }
        total = total + Firepower(registry, unit) / own;
    }
    return total;
}

void Update(entt::registry& registry, const Physics::Grid& grid, int32_t dt_ms) {
    // On the way in.
    std::vector<entt::entity> giving_up;
    std::vector<std::pair<entt::entity, entt::entity>> arriving;
    auto walkers = registry.view<Components::Boarding, Components::Transform>();
    for (auto unit : walkers) {
        auto& boarding = walkers.get<Components::Boarding>(unit);
        const entt::entity building = boarding.building;
        if (!Accepts(registry, building, unit)) {
            giving_up.push_back(unit);
            continue;
        }
        const auto& at = walkers.get<Components::Transform>(unit).position;
        if (Navigation::GapAt(registry, unit, at, building) <= EnterRange(registry, unit)) {
            arriving.emplace_back(unit, building);
            continue;
        }
        const auto* route = registry.try_get<Components::Path>(unit);
        if (route != nullptr && route->current_waypoint_index < route->waypoints.size()) continue;
        if (IsShip(registry, building)) {
            // A ship may still be out at sea: the unit walks to the shore
            // beside it once there is one, and meanwhile the ship comes in.
            boarding.wait_ms -= dt_ms;
            if (boarding.wait_ms > 0) continue;
            boarding.wait_ms = kShipRetryMs;
            if (++boarding.attempts > kMaxShipAttempts) {
                giving_up.push_back(unit);
            } else if (!RouteTo(registry, grid, unit, building)) {
                CallShip(registry, grid, building, unit);
            }
            continue;
        }
        if (boarding.attempts >= kMaxAttempts || !RouteTo(registry, grid, unit, building)) {
            giving_up.push_back(unit);
            continue;
        }
        ++boarding.attempts;
    }
    for (entt::entity unit : giving_up) registry.remove<Components::Boarding>(unit);
    for (const auto& [unit, building] : arriving) {
        if (!registry.valid(unit) || !registry.all_of<Components::Boarding>(unit)) continue;
        Enter(registry, unit, building, registry.get<Components::Boarding>(unit));
    }

    // Transports putting their load ashore: in to the shore nearest where
    // they were sent, then everyone out onto the land.
    std::vector<entt::entity> landed;
    auto landings = registry.view<Components::Landing, Components::Transform>();
    for (auto ship : landings) {
        auto& landing = landings.get<Components::Landing>(ship);
        if (Count(registry, ship) == 0) {
            landed.push_back(ship);
            continue;
        }
        if (!landing.routed) {
            landing.routed = true;
            const Physics::Layer sea = Navigation::LayerOf(registry, grid, ship);
            int x = 0, y = 0;
            if (!ShoreTile(grid, sea, Navigation::RegionOf(registry, grid, ship), -1, landing.at,
                           x, y)) {
                landed.push_back(ship);
                continue;
            }
            const auto& at = landings.get<Components::Transform>(ship).position;
            if (Physics::TileOf(at.x) != x || Physics::TileOf(at.y) != y) {
                auto path = Navigation::Route(registry, grid, ship, at,
                                              {Core::Fixed32(x), Core::Fixed32(y)});
                if (!path.waypoints.empty()) {
                    registry.emplace_or_replace<Components::Path>(ship, std::move(path));
                    continue;
                }
            }
        }
        if (Moving(registry, ship)) continue;
        // In: everyone who has ground to stand on steps ashore; a crowded
        // beach gets a few more tries as the first off move up it.
        if (Disembark(registry, grid, ship) == 0 || ++landing.attempts > 8) landed.push_back(ship);
    }
    for (entt::entity ship : landed) registry.remove<Components::Landing>(ship);

    // Inside: a building whose garrison has gone -- destroyed, or a unit in it
    // died -- forgets it; the rest heal at the building's own rate.
    auto shelters = registry.view<Components::Garrison>();
    for (auto building : shelters) {
        auto& garrison = shelters.get<Components::Garrison>(building);
        std::erase_if(garrison.inside, [&](entt::entity unit) {
            return !registry.valid(unit) || !registry.all_of<Components::Garrisoned>(unit);
        });
        const R::UnitRules* rules = RulesOf(registry, building);
        if (rules == nullptr || rules->garrison_heal.value <= 0) continue;
        const Core::Fixed32 step = rules->garrison_heal * Core::Fixed32(dt_ms) / Core::Fixed32(1000);
        for (entt::entity unit : garrison.inside) {
            auto* health = registry.try_get<Components::Health>(unit);
            if (health == nullptr || health->current_hp >= health->max_hp) continue;
            auto& stay = registry.get<Components::Garrisoned>(unit);
            stay.heal = stay.heal + step;
            const int whole = stay.heal.ToInt();
            if (whole <= 0) continue;
            stay.heal = stay.heal - Core::Fixed32(whole);
            health->current_hp = std::min(health->max_hp, health->current_hp + whole);
        }
    }
}

} // namespace ECS::Garrisons
