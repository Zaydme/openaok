#include "ecs/Trade.h"

#include <vector>

#include "core/FixedMath.h"
#include "ecs/BuildingGeometry.h"
#include "ecs/Combat.h"
#include "ecs/Events.h"
#include "ecs/Navigation.h"
#include "ecs/Players.h"
#include "ecs/Score.h"
#include "ecs/Units.h"
#include "ecs/components/Building.h"
#include "ecs/components/Identity.h"
#include "ecs/components/Owner.h"
#include "ecs/components/Path.h"
#include "ecs/components/Trader.h"
#include "ecs/components/Transform.h"
#include "pathfinding/ApproachPlanner.h"

namespace ECS::Trade {

namespace {

namespace R = Core::Rules;
using Components::Trader;

// Resource 101, "trade profit": the gold a player's trade has earned, kept
// for the achievements.
constexpr int kTradeProfit = 101;

// How far off a building's wall a trader stops to trade, and the slack on
// arriving: a cart pulls up against the Market, a cog against the Dock.
const Core::Fixed32 kStandoff = Core::Fixed32(3) / Core::Fixed32(10);
const Core::Fixed32 kArriveSlack = Core::Fixed32(6) / Core::Fixed32(10);
// Approaches in a row that may find no way before the route is given up.
constexpr int kMostAttempts = 8;

// The trade formula's figures (see the header).
const Core::Fixed32 kDeadZone = Core::Fixed32(4);
const Core::Fixed32 kBase = Core::Fixed32(3) / Core::Fixed32(10);
const Core::Fixed32 kRate = Core::Fixed32(46) / Core::Fixed32(100);
const Core::Fixed32 kFloor = Core::Fixed32(1) / Core::Fixed32(2);

Core::PlayerId OwnerOf(const entt::registry& registry, entt::entity e) {
    const auto* owner = registry.try_get<Components::Owner>(e);
    return owner != nullptr ? owner->player_id : Core::PLAYER_GAIA;
}

int UnitIdOf(const entt::registry& registry, entt::entity e) {
    const auto* identity = registry.try_get<Components::Identity>(e);
    return identity != nullptr ? identity->unit_id : -1;
}

const R::UnitRules* RulesOf(const entt::registry& registry, entt::entity e) {
    return Players::UnitOf(registry, OwnerOf(registry, e), UnitIdOf(registry, e));
}

const R::Task* TradeTask(const R::UnitRules& rules) {
    for (int t = 0; t < rules.task_count; ++t) {
        if (rules.tasks[t].action == R::TaskAction::Trade) return &rules.tasks[t];
    }
    return nullptr;
}

// A finished, standing building of the kind `site` names.
bool IsSite(const entt::registry& registry, entt::entity building, int site) {
    if (site < 0 || !registry.valid(building)) return false;
    const auto* state = registry.try_get<Components::Building>(building);
    if (state == nullptr || state->state != Components::BuildingState::Complete) return false;
    return UnitIdOf(registry, building) == site && Combat::Alive(registry, building);
}

Components::Vector2D Middle(const entt::registry& registry, entt::entity building) {
    const auto ground = Buildings::FootprintBounds(
        registry.get<Components::Transform>(building).position, UnitIdOf(registry, building));
    return {(ground.min_x + ground.max_x) / Core::Fixed32(2),
            (ground.min_y + ground.max_y) / Core::Fixed32(2)};
}

// The player's own finished building of kind `site` nearest `from`, or null.
// Ties go to the lower entity, the same on every machine.
entt::entity NearestOwn(const entt::registry& registry, Core::PlayerId player, int site,
                        const Components::Vector2D& from) {
    entt::entity best = entt::null;
    Core::Fixed32 best_gap{};
    auto buildings = registry.view<const Components::Building, const Components::Owner,
                                   const Components::Transform>();
    for (auto b : buildings) {
        if (buildings.get<const Components::Owner>(b).player_id != player) continue;
        if (!IsSite(registry, b, site)) continue;
        const auto at = Middle(registry, b);
        const Core::Fixed32 gap = Core::FixedMath::Distance(at.x - from.x, at.y - from.y);
        if (best == entt::null || gap < best_gap || (gap == best_gap && b < best)) {
            best = b;
            best_gap = gap;
        }
    }
    return best;
}

// The unit a trader becomes with a load: the one whose walk is what its
// trade task says it wears coming back. -1 when it stays as it is -- a cog,
// or a cart that is full already.
int LoadedUnit(const R::UnitRules& rules, int unit_id) {
    const R::Task* task = TradeTask(rules);
    if (task == nullptr || task->carrying_graphic < 0) return -1;
    const auto& units = R::Get().units;
    for (int id = 0; id < static_cast<int>(units.size()); ++id) {
        if (id == unit_id || !units[id].valid || units[id].cls != rules.cls) continue;
        if (units[id].walking_graphic == task->carrying_graphic) return id;
    }
    return -1;
}

bool Walking(const entt::registry& registry, entt::entity unit) {
    const auto* path = registry.try_get<Components::Path>(unit);
    return path != nullptr && path->current_waypoint_index < path->waypoints.size();
}

enum class Approach { Arrived, Walking, Unreachable };

// Walks the trader up against `building`, on its own ground -- a cart on
// land, a cog on the sea -- and says whether it is there.
Approach ApproachSite(entt::registry& registry, const Physics::Grid& grid, entt::entity unit,
                      entt::entity building) {
    if (Walking(registry, unit)) return Approach::Walking;
    const auto& at = registry.get<Components::Transform>(unit).position;
    const R::UnitRules* rules = RulesOf(registry, unit);
    const Core::Fixed32 radius = rules != nullptr ? rules->size_x : Core::Fixed32(0);
    const auto ground = Buildings::FootprintBounds(
        registry.get<Components::Transform>(building).position, UnitIdOf(registry, building));
    if (Navigation::GapAt(registry, unit, at, building) <= kStandoff + kArriveSlack) {
        return Approach::Arrived;
    }

    Pathfinding::ApproachRequest ask;
    ask.target = ground;
    ask.from = at;
    ask.standoff = radius + kStandoff;
    ask.layer = Navigation::LayerOf(registry, grid, unit);
    const auto plan = Pathfinding::FindStandingSpot(grid, ask);
    if (!plan.found) return Approach::Unreachable;
    // The walk ends as the trader comes within reach of the building.
    auto path = Navigation::Route(registry, grid, unit, at, plan.spot, building,
                                  kStandoff + kArriveSlack);
    if (path.waypoints.empty()) return Approach::Unreachable;
    registry.emplace_or_replace<Components::Path>(unit, std::move(path));
    return Approach::Walking;
}

// The cart turns into `unit_id`, keeping its route.
void Turn(entt::registry& registry, entt::entity unit, int unit_id) {
    if (unit_id < 0 || UnitIdOf(registry, unit) == unit_id) return;
    Units::Become(registry, unit, unit_id);
}

// The gold is lost: a cart with nowhere to take it.
void Drop(entt::registry& registry, entt::entity unit, Trader& trader) {
    trader.gold = Core::Fixed32(0);
    trader.loaded = false;
    trader.active = false;
    Turn(registry, unit, trader.empty_unit);
}

} // namespace

int TradesAt(const R::UnitRules& rules) {
    const R::Task* task = TradeTask(rules);
    return task != nullptr ? task->target_unit : -1;
}

bool CanTrade(const entt::registry& registry, entt::entity unit, entt::entity partner) {
    if (!Combat::Alive(registry, unit) || registry.all_of<Components::Building>(unit)) {
        return false;
    }
    const R::UnitRules* rules = RulesOf(registry, unit);
    if (rules == nullptr) return false;
    const int site = TradesAt(*rules);
    if (!IsSite(registry, partner, site)) return false;
    const Core::PlayerId player = OwnerOf(registry, unit);
    const Core::PlayerId theirs = OwnerOf(registry, partner);
    // A foreign one only: never one's own, nor Gaia's.
    if (theirs == player || theirs == Core::PLAYER_GAIA) return false;
    return NearestOwn(registry, player, site,
                      registry.get<Components::Transform>(unit).position) != entt::null;
}

bool Start(entt::registry& registry, const Physics::Grid& grid, entt::entity unit,
           entt::entity partner) {
    (void)grid;
    if (!CanTrade(registry, unit, partner)) return false;
    const R::UnitRules* rules = RulesOf(registry, unit);
    const int site = TradesAt(*rules);
    auto& trader = registry.get_or_emplace<Trader>(unit);
    if (trader.empty_unit < 0) trader.empty_unit = UnitIdOf(registry, unit);
    // Its own Market or Dock: the one it set out from, while it stands,
    // else the nearest.
    if (!IsSite(registry, trader.home, site) ||
        OwnerOf(registry, trader.home) != OwnerOf(registry, unit)) {
        trader.home = NearestOwn(registry, OwnerOf(registry, unit), site,
                                 registry.get<Components::Transform>(unit).position);
    }
    trader.partner = partner;
    trader.active = true;
    trader.attempts = 0;
    registry.remove<Components::Path>(unit);
    return true;
}

void Stop(entt::registry& registry, entt::entity unit) {
    if (auto* trader = registry.try_get<Trader>(unit)) trader->active = false;
}

void Forget(entt::registry& registry, entt::entity unit) {
    auto* trader = registry.try_get<Trader>(unit);
    if (trader == nullptr) return;
    const int empty = trader->empty_unit;
    registry.remove<Trader>(unit);
    Turn(registry, unit, empty);
}

Core::Fixed32 Pay(const entt::registry& registry, const Physics::Grid& grid,
                  entt::entity home, entt::entity partner) {
    if (!registry.valid(home) || !registry.valid(partner)) return Core::Fixed32(0);
    const auto a = Middle(registry, home);
    const auto b = Middle(registry, partner);
    const Core::Fixed32 dx = Core::FixedMath::Abs(a.x - b.x) - kDeadZone;
    const Core::Fixed32 dy = Core::FixedMath::Abs(a.y - b.y) - kDeadZone;
    const Core::Fixed32 ax = dx.value > 0 ? dx : Core::Fixed32(0);
    const Core::Fixed32 ay = dy.value > 0 ? dy : Core::Fixed32(0);
    const Core::Fixed32 d = Core::FixedMath::Distance(ax, ay);
    const Core::Fixed32 size(grid.GetWidth() > 0 ? grid.GetWidth() : 1);
    return (d / size + kBase) * d * kRate + kFloor;
}

void Update(entt::registry& registry, const Physics::Grid& grid, int32_t dt_ms) {
    (void)dt_ms;
    auto traders = registry.view<Trader, Components::Transform>();
    for (auto unit : traders) {
        auto& trader = traders.get<Trader>(unit);
        if (!trader.active || !Combat::Alive(registry, unit)) continue;
        const R::UnitRules* rules = RulesOf(registry, unit);
        if (rules == nullptr) continue;
        const int site = TradesAt(*rules);
        const Core::PlayerId player = OwnerOf(registry, unit);
        const auto& at = traders.get<Components::Transform>(unit).position;

        // Its own Market or Dock gone: the nearest other of its own.
        if (!IsSite(registry, trader.home, site) || OwnerOf(registry, trader.home) != player) {
            trader.home = NearestOwn(registry, player, site, at);
            if (trader.home == entt::null) {
                // Nowhere to take the gold: it stops, and the gold is lost.
                Drop(registry, unit, trader);
                registry.remove<Components::Path>(unit);
                continue;
            }
        }
        // The partner gone, or become its own: back home to wait for orders,
        // bringing in what it carries.
        const bool partner_lost = !IsSite(registry, trader.partner, site) ||
                                  OwnerOf(registry, trader.partner) == player;
        const entt::entity target = trader.loaded || partner_lost ? trader.home : trader.partner;

        switch (ApproachSite(registry, grid, unit, target)) {
        case Approach::Walking:
            trader.attempts = 0;
            break;
        case Approach::Unreachable:
            if (++trader.attempts >= kMostAttempts) trader.active = false;
            break;
        case Approach::Arrived:
            trader.attempts = 0;
            if (target == trader.home) {
                if (trader.loaded) {
                    if (Players::Player* seat = Players::Find(registry, player)) {
                        Players::AddResource(*seat, R::Res::Gold, trader.gold);
                        Players::AddResource(*seat, kTradeProfit, trader.gold);
                        // Traded gold is collected gold too (ecs/Score).
                        Score::Collected(*seat, R::Res::Gold, trader.gold);
                    }
                    Events::Emit(registry, {.kind = Events::Kind::TradeDelivered,
                                            .subject = unit,
                                            .other = trader.home,
                                            .player = player,
                                            .at = at,
                                            .amount = (trader.gold * Core::Fixed32(100)).ToInt()});
                    trader.gold = Core::Fixed32(0);
                    trader.loaded = false;
                    Turn(registry, unit, trader.empty_unit);
                }
                // Out again -- unless there is nobody left to trade with.
                if (partner_lost) trader.active = false;
            } else {
                trader.gold = Pay(registry, grid, trader.home, trader.partner);
                trader.loaded = true;
                Turn(registry, unit, LoadedUnit(*rules, UnitIdOf(registry, unit)));
            }
            break;
        }
    }
}

} // namespace ECS::Trade
