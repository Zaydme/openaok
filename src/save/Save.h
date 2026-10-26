#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "core/GameSetup.h"
#include "ecs/Simulation.h"

// Saved games.
//
// A save is the whole simulation: the lobby it was started from, the tick it
// stands at, every entity with its id, version and place in every component
// pool -- so views walk the restored world in the order they walked the
// saved one, which the simulation's determinism rests on -- the registry's
// context (the players' rules and stockpiles, the fog, the victory clock, the
// computer players, the random stream) and the grid's tiles. Loading builds a
// world from the saved lobby, as starting the game did, then puts all of it
// back and checks the world's digest against the one taken when it was saved.
//
// The bytes are this build's own: components go out as they lie in memory,
// behind a fingerprint of their sizes that a different build refuses.
namespace Save {

bool Write(const ECS::Simulation& sim, std::vector<uint8_t>& out);
// Replaces `sim` with the saved world. False, with `sim` unusable, when the
// bytes are not a save from this build or the restored world's digest is not
// the saved one.
bool Read(ECS::Simulation& sim, const std::vector<uint8_t>& in, std::string* error = nullptr);

bool WriteFile(const ECS::Simulation& sim, const std::string& path);

// The lobby a saved game was started from, without loading the world: what a
// restored network game is hosted with.
bool ReadSetup(const std::vector<uint8_t>& in, Core::GameSetup& setup);
// A file's bytes, whole; false if it cannot be read.
bool ReadBytes(const std::string& path, std::vector<uint8_t>& out);
bool ReadFile(ECS::Simulation& sim, const std::string& path, std::string* error = nullptr);

// The component pools and context entries in `registry` that a save would
// not carry, by name; empty when it carries everything. The verify harness
// fails on any, so a new component cannot slip past the save unnoticed.
std::vector<std::string> Uncovered(const entt::registry& registry);

} // namespace Save
