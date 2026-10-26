#pragma once
#include <vector>

#include <entt/entt.hpp>

#include "core/Types.h"
#include "ecs/components/Transform.h"

// What a computer player has, taken fresh each time it thinks.
//
// The rules' facts about the player itself and the executor's managers
// (ai/Economy, ai/Military ...) all decide from this one snapshot --
// villagers and what they are on, buildings finished and going up and
// whether each is busy, soldiers and ships, the stockpile and the population.
// Orders given during a pass change the world at once; the actions that give
// them keep the snapshot in step (a building queued is busy, a foundation
// placed is counted).
namespace AI {

struct Census {
    Core::PlayerId player = 0;
    int age = 0;
    int stock[4] = {0, 0, 0, 0};       // by Core::ResourceType
    int population = 0;
    int capacity = 0;
    int population_limit = 0;

    // The building villagers are trained at, and the middle of it: home.
    entt::entity town_center = entt::null;
    ECS::Components::Vector2D home{};

    std::vector<entt::entity> villagers;
    std::vector<entt::entity> idle_villagers;
    int gatherers[4] = {0, 0, 0, 0};   // villagers on each resource
    int builders = 0;

    // Fishing ships: units that work as themselves rather than as villagers.
    std::vector<entt::entity> fishing_ships;
    std::vector<entt::entity> idle_fishing_ships;
    // Trade carts and cogs.
    std::vector<entt::entity> trade;

    // Everything that fights, the scout aside; the land part of it, scout
    // included; the warships; transports.
    std::vector<entt::entity> military;
    std::vector<entt::entity> idle_military;
    std::vector<entt::entity> soldiers;
    std::vector<entt::entity> boats;
    std::vector<entt::entity> transports;
    entt::entity scout = entt::null;

    // Herd animals the player has taken (sheep): food that walks.
    std::vector<entt::entity> herd;

    struct Site {
        entt::entity entity = entt::null;
        int unit_id = -1;
        bool complete = false;
        bool idle = false;     // nothing in its queue
    };
    std::vector<Site> buildings;
    int idle_farms = 0;        // finished farms nobody is working

    // Buildings of `unit_id`: finished only, or going up as well.
    int Count(int unit_id, bool finished_only = false) const;
    // A finished building of `unit_id`, or null.
    entt::entity Any(int unit_id) const;
    // How many builders are at, or on their way to, `site`.
    int BuildersOn(const entt::registry& registry, entt::entity site) const;
    // The civilian population: villagers, fishing ships, trade units.
    int Civilians() const {
        return static_cast<int>(villagers.size() + fishing_ships.size() + trade.size());
    }
};

Census Take(const entt::registry& registry, Core::PlayerId player);

// Drops what has left the world from a census taken earlier in the pass: the
// computer player's own orders -- a deletion, a resignation -- are carried
// out as they are given, and take things out of it.
void Prune(const entt::registry& registry, Census& census);

} // namespace AI
