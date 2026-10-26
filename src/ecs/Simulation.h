#pragma once
#include <entt/entt.hpp>
#include <cstdint>
#include <utility>
#include <vector>

#include "core/GameSetup.h"
#include "physics/Grid.h"

namespace Scenario {
struct File;
}

namespace ECS {

class Simulation {
public:
    // Builds the world the lobby described: the players' tables, the map from
    // its script, each player's starting units, and the technologies every
    // player starts with.
    void Init(const Core::GameSetup& setup = Core::DefaultSetup());
    // The scenario editor's world: `file`'s, built as a scenario game builds
    // its own, from a file in memory rather than one on disk. The editor never
    // runs it (Update is never called on it).
    void InitFromFile(const Core::GameSetup& setup, const Scenario::File& file);
    void Update(int32_t dt_ms);
    void CalculateVision();
    
    entt::registry& GetRegistry() { return registry; }
    Physics::Grid& GetGrid() { return grid; }
    const Core::GameSetup& Setup() const { return setup; }
    const entt::registry& GetRegistry() const { return registry; }
    const Physics::Grid& GetGrid() const { return grid; }
    // Ticks run since Init.
    uint64_t Tick() const { return current_tick; }

    // For the save module: a world built by Init from the saved lobby, its
    // registry then replaced wholesale and its clock set back.
    void ReplaceRegistry(entt::registry&& saved) { registry = std::move(saved); }
    void RestoreTick(uint64_t tick) { current_tick = tick; }

private:
    // The map, units, stances and triggers of the lobby's scenario file, in
    // place of a random map's (ecs/ScenarioWorld.cpp).
    void InitScenario();

    entt::registry registry;
    Physics::Grid grid{120, 120}; // the "Tiny" map size
    // The file InitFromFile builds from, for the length of that call.
    const Scenario::File* from_file = nullptr;
    uint64_t current_tick = 0;
    Core::GameSetup setup;
    
public:
    std::vector<std::pair<int, int>> player_starts;
};

} // namespace ECS
