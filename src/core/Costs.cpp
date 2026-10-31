#include "core/Costs.h"

#include <algorithm>

namespace Core::Costs {

namespace {

struct State {
    bool on = false;
    Totals totals;
    int64_t tick[kParts] = {};
    std::chrono::steady_clock::time_point tick_start{};
};
State g;

int64_t Micros(std::chrono::steady_clock::time_point from) {
    return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - from).count();
}

} // namespace

const char* Name(Part part) {
    static const char* const kNames[kParts] = {
        "orders",   "ai",        "navigation", "movement",   "workers", "trade",    "garrisons", "duties",
        "monks",    "relics",    "animals",    "combat",     "missiles", "mortality", "buildings", "walls",
        "production", "animation", "triggers", "victory",    "vision",  "fog",      "other",
    };
    const size_t i = static_cast<size_t>(part);
    return i < kParts ? kNames[i] : "?";
}

void Enable(bool on) { g.on = on; }

bool Enabled() { return g.on; }

const Totals& Read() { return g.totals; }

void Reset() { g.totals = Totals{}; }

void BeginTick() {
    if (!g.on) return;
    std::fill(std::begin(g.tick), std::end(g.tick), 0);
    g.tick_start = std::chrono::steady_clock::now();
}

void EndTick() {
    if (!g.on) return;
    const int64_t whole = Micros(g.tick_start);
    for (size_t i = 0; i < kParts; ++i) {
        g.totals.us[i] += g.tick[i];
        g.totals.worst[i] = std::max(g.totals.worst[i], g.tick[i]);
    }
    g.totals.worst_tick = std::max(g.totals.worst_tick, whole);
    ++g.totals.ticks;
}

Scope::Scope(Part part) : part_(part), on_(g.on) {
    if (on_) start_ = std::chrono::steady_clock::now();
}

Scope::~Scope() {
    if (on_) g.tick[static_cast<size_t>(part_)] += Micros(start_);
}

void Route(int expansions) {
    if (!g.on) return;
    ++g.totals.routes;
    g.totals.expansions += expansions;
}

} // namespace Core::Costs
