#pragma once
#include <vector>

#include <entt/entt.hpp>

#include "core/Types.h"

// What a computer player can build and train, found in the data by what each
// thing does: a house is the cheapest building a villager can put up that
// raises the population cap; a lumber camp is where wood may be dropped off; a
// barracks is somewhere fighting units are trained.
namespace AI::Catalog {

// The unit villagers are, and the building they are trained at.
int Villager(const entt::registry& registry, Core::PlayerId player);

// Whether the player's villagers can put `unit_id` up now.
bool Buildable(const entt::registry& registry, Core::PlayerId player, int unit_id);

// The cheapest buildable building that raises the population cap; -1 if none.
int House(const entt::registry& registry, Core::PlayerId player);

// The cheapest buildable building, not the Town Center, that takes `resource`.
int DropSite(const entt::registry& registry, Core::PlayerId player, Core::ResourceType resource);

// The buildable building that is itself a food source: the farm.
int Farm(const entt::registry& registry, Core::PlayerId player);

// Every buildable building that trains fighting units, cheapest first.
std::vector<int> MilitaryBuildings(const entt::registry& registry, Core::PlayerId player);

// The strongest fighting unit the player can train at `building_id` now, by
// hit points times attack -- of those it can pay for this moment, if
// `affordable`; -1 if none.
int BestUnitAt(const entt::registry& registry, Core::PlayerId player, int building_id,
               bool affordable = false);

// Whether `unit_id`'s price includes any of `resource`.
bool Costs(const entt::registry& registry, Core::PlayerId player, int unit_id,
           Core::ResourceType resource);

// What it costs in all, the four stockpiles summed.
int TotalCost(const entt::registry& registry, Core::PlayerId player, int unit_id);

} // namespace AI::Catalog
