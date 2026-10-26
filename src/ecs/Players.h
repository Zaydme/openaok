#pragma once
#include <cstdint>
#include <vector>

#include <entt/entt.hpp>

#include "core/FixedPoint.h"
#include "core/GameSetup.h"
#include "core/Rules.h"
#include "core/Types.h"

// Everything that belongs to a player rather than to a unit: its resources,
// its own copy of the unit rules, and where it stands with each technology.
//
// Resources are not just the four stockpiles: the same table holds population
// headroom, the current age, the food a new farm holds. Every technology effect
// changes either that table or the player's copy of the unit rules (a Feudal
// Age Town Center is the player's slot 109 holding the numbers and art of unit
// 71). Nothing here is shared between players.
//
// The table lives in the registry's context, so it is simulation state like
// any component: deterministic, and gone with the world it belongs to.
namespace ECS::Players {

enum class TechState : uint8_t {
    Locked,        // not researched; whether it is available is its requirements
    Researching,   // queued at a building, paid for
    Done,
    Disabled,      // the civ's tech tree, or another effect, forbids it
};

// A player's view of one technology. Cost and time are per player because
// effects change them -- a civ bonus can make every tech cheaper.
struct TechStatus {
    TechState state = TechState::Locked;
    Core::Rules::Cost costs[Core::Rules::kMaxCosts];
    int cost_count = 0;
    int time = 0;
};

struct Player {
    bool active = false;
    Core::PlayerId id = 0;
    int civ = 0;
    Core::TeamId team = 0;
    // By the dat's resource id: 0 food, 1 wood, 2 stone, 3 gold, 4 headroom...
    std::vector<Core::Fixed32> resources;
    // The player's own unit rules, by unit id. Starts as a copy of the dat's
    // and is changed by effects.
    std::vector<Core::Rules::UnitRules> units;
    // Whose art each slot wears: its own id until an upgrade replaces it.
    std::vector<int> art;
    std::vector<TechStatus> techs;
};

struct Table {
    Player players[Core::kMaxPlayers];
    int population_limit = 75;
};

// Builds every seat's table from the dat and the lobby's choices: the civ's
// starting resources and enabled units, the starting stockpile, and the tech
// statuses before any are researched. Technologies are applied afterwards by
// the techs module.
void Setup(entt::registry& registry, const Core::GameSetup& setup);

// The player's table, or null if there is no such seat.
Player* Find(entt::registry& registry, Core::PlayerId player);
const Player* Find(const entt::registry& registry, Core::PlayerId player);

// The player's current rules for a unit. Falls back to the dat's own when the
// player has no table -- a world built without a lobby, or a map object.
const Core::Rules::UnitRules* UnitOf(const entt::registry& registry,
                                     Core::PlayerId player, int unit_id);

// Whose art the player's slot for `unit_id` wears now.
int ArtOf(const entt::registry& registry, Core::PlayerId player, int unit_id);

// The player whose unit table `entity` plays by: its owner's, or -- converted
// -- the side that made it (Components::RulesOf).
Core::PlayerId RulesPlayer(const entt::registry& registry, entt::entity entity);

// `entity`'s rules for `unit_id` (its own identity, or a job it is at), from
// the table it plays by.
const Core::Rules::UnitRules* RulesFor(const entt::registry& registry, entt::entity entity,
                                       int unit_id);

// The player's population limit from the lobby.
int PopulationLimit(const entt::registry& registry);

// A resource by its dat id. Zero for one the player does not have.
Core::Fixed32 Resource(const Player& player, int dat_resource);
void AddResource(Player& player, int dat_resource, Core::Fixed32 amount);
void SetResource(Player& player, int dat_resource, Core::Fixed32 amount);

// The whole units of a stockpile, as the interface prints it.
int Stock(const Player& player, Core::ResourceType type);
void AddStock(Player& player, Core::ResourceType type, int amount);

// Whether the player holds every paid slot of a price.
bool CanPay(const Player& player, const Core::Rules::Cost* costs, int count);

// Deducts (sign -1) or refunds (sign +1) the paid slots of a price.
void Pay(Player& player, const Core::Rules::Cost* costs, int count, int sign);

// Applies what a unit contributes to its owner's resources while it exists --
// a House's headroom, a villager's place in the population. `modes` is a mask
// of the storage modes to apply (Rules::StorageMode), `sign` +1 when the unit
// comes into being and -1 when it goes.
void Contribute(entt::registry& registry, Core::PlayerId player, int unit_id,
                uint8_t modes, int sign);

} // namespace ECS::Players
