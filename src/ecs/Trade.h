#pragma once
#include <entt/entt.hpp>

#include "core/FixedPoint.h"
#include "core/Rules.h"
#include "physics/Grid.h"

// Trade routes: trade carts between Markets, trade cogs between Docks.
//
// Who trades where is the dat's: a unit trades if its tasks hold a Trade task
// (action 111), and the task names the building it trades at -- the trade
// cart Market 84, the cog Dock 45. A trader carries goods to a foreign one,
// picks up a load of gold there, and returns to its own to deposit it, back
// and forth along the route. Any player's will do, ally, neutral or enemy,
// but never one of its own, and gold goes home only to its own.
//
// A full trade cart is the dat's own unit: the cart's trade task names the
// full cart's walk (graphic 1145) as what it wears with the load, and that is
// TCARTF's walk -- so a cart taking on gold becomes TCARTF, and dies as one,
// and turns back on handing the gold in. A cog's task names nothing, and it
// stays as it is.
//
// The gold a load pays:
//
//     gold = (d / size + 0.3) * d * 0.46 + 0.5
//     d = sqrt(max(0, dx - 4)^2 + max(0, dy - 4)^2)
//
// with dx and dy the distances between the two buildings' middles along the
// map's axes, in tiles, and size the map's width in tiles. Not rounded. Carts
// and cogs are paid the same.
//
// When the partner is destroyed the trader goes back to its own and waits;
// when its own is, it takes the nearest other of its own to its gold, and
// with none left it stops and the gold is lost.
namespace ECS::Trade {

// The building the unit's rules trade at (a dat unit id), or -1.
int TradesAt(const Core::Rules::UnitRules& rules);

// Whether `unit` may trade with `partner`: it trades at that kind of
// building, the building is finished and someone else's, and the unit's
// owner has one of its own to bring the gold home to.
bool CanTrade(const entt::registry& registry, entt::entity unit, entt::entity partner);

// Puts the unit on the route to `partner`. A load it already carries is
// taken home first. False, changing nothing, unless it CanTrade.
bool Start(entt::registry& registry, const Physics::Grid& grid, entt::entity unit,
           entt::entity partner);

// Takes the unit off its route -- another order. It keeps any gold it
// carries.
void Stop(entt::registry& registry, entt::entity unit);

// The unit changes hands: the route and the load are forgotten, and a full
// cart is an empty one again.
void Forget(entt::registry& registry, entt::entity unit);

// What a load between these two buildings pays, by the formula above.
Core::Fixed32 Pay(const entt::registry& registry, const Physics::Grid& grid,
                  entt::entity home, entt::entity partner);

// Every trader on its route, once a tick.
void Update(entt::registry& registry, const Physics::Grid& grid, int32_t dt_ms);

} // namespace ECS::Trade
