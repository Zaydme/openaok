#pragma once
#include <chrono>
#include <cstddef>
#include <cstdint>

// What each part of the simulation's tick costs, in microseconds of wall time,
// and how much searching for routes it did: measured only while switched on
// (--perf, the stress check). Nothing in the simulation reads it, so it never
// changes what happens.
namespace Core::Costs {

enum class Part : uint8_t {
    Orders,
    AI,
    Navigation,
    Movement,
    Workers,
    Trade,
    Garrisons,
    Duties,
    Monks,
    Relics,
    Animals,
    Combat,
    Missiles,
    Mortality,
    Buildings,
    Walls,
    Production,
    Animation,
    Triggers,
    Victory,
    Vision,
    Fog,
    Other,
    Count
};
constexpr size_t kParts = static_cast<size_t>(Part::Count);

const char* Name(Part part);

void Enable(bool on);
bool Enabled();

// Since the last Reset: each part's microseconds in all and in its worst
// tick, the worst whole tick, the ticks, and the routes searched with the
// nodes their searches expanded.
struct Totals {
    int64_t us[kParts] = {};
    int64_t worst[kParts] = {};
    int64_t worst_tick = 0;
    int64_t ticks = 0;
    int64_t routes = 0;
    int64_t expansions = 0;
};
const Totals& Read();
void Reset();

// A tick begun and ended: each part's share of it goes to the totals.
void BeginTick();
void EndTick();

// Counts its scope's time towards `part`, when costs are on.
class Scope {
public:
    explicit Scope(Part part);
    ~Scope();
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;

private:
    Part part_;
    bool on_;
    std::chrono::steady_clock::time_point start_;
};

// One route searched, and how many nodes its search expanded.
void Route(int expansions);

} // namespace Core::Costs
