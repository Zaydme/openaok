#pragma once
#include <cstdint>

// Type definitions shared by the whole engine. No dependencies.

namespace Core {

// === Player / Team ===
using PlayerId = uint8_t;
using TeamId   = uint8_t;
constexpr PlayerId PLAYER_NONE = 0;
constexpr PlayerId PLAYER_GAIA = 0; // Gaia (nature) is always player 0
// The seat a machine plays unless a network game gives it another.
constexpr PlayerId PLAYER_LOCAL = 1;

// The player sitting at this machine: whose interface is drawn and whose
// orders the mouse gives. Seat 1 alone at the keyboard; whichever seat the host
// assigned in a network game. The interface asks this -- the simulation never
// does, since every machine runs the same simulation for everyone.
PlayerId LocalPlayer();
void SetLocalPlayer(PlayerId player);

// === Resources (engine order; the dat's ids differ, see Rules::DatResource) ===
enum class ResourceType : uint8_t {
    Food  = 0,
    Wood  = 1,
    Gold  = 2,
    Stone = 3,
    COUNT = 4
};

// === Terrain ===
enum class TerrainType : uint8_t {
    DeepWater,
    MediumWater,
    ShallowWater,
    Swamp,
    Beach,
    Sand,
    Dirt1,
    Dirt2,
    Dirt3,
    Grass1,
    Grass2,
    Grass3,
    Grass4,
    Forest,
    // A farm's ground: planted, harvested out, and the three stages it passes
    // through while the farm is being built.
    Farm,
    FarmDead,
    FarmBuild1,
    FarmBuild2,
    FarmBuild3,
    // The bare ground a Town Center, tower or wonder is laid on.
    Foundation,
    Stone1,
    Stone2,
    Ice,
    COUNT
};

// === Unit facing (8 directions, clockwise from South, the SLP angle order) ===
enum class Direction : uint8_t {
    South = 0,
    SouthWest,
    West,
    NorthWest,
    North,
    NorthEast,
    East,
    SouthEast,
    COUNT
};

// === Building types ===
enum class BuildingType : uint8_t {
    TownCenter = 0,
    LumberCamp,
    MiningCamp,
    Mill,
    Barracks,
    House,
    Farm,
    COUNT
};

// === The lobby's map visibility: fog as normal, explored, or all visible ===
enum class Reveal : uint8_t { Normal, Explored, AllVisible };

// === Formations ===
enum class Formation : uint8_t { Line, Staggered, Box, Flank };

// === Entity states for FSMs ===
enum class UnitState : uint8_t {
    Idle = 0,
    Moving,
    Gathering,
    Returning,
    Building,
    Attacking,
    Dying,
    // Mending something of its own side's: a building, a ship, a trebuchet.
    Repairing,
    COUNT
};

} // namespace Core
