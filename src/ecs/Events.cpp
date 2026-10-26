#include "ecs/Events.h"

#include <algorithm>

namespace ECS::Events {

namespace {

// How long an event is kept: five seconds of ticks, far more than a frame
// ever spans, so a reader never misses one by being slow.
constexpr uint64_t kKeepTicks = 300;

} // namespace

void Setup(entt::registry& registry) {
    registry.ctx().insert_or_assign(Log{});
}

void BeginTick(entt::registry& registry) {
    Log* log = registry.ctx().find<Log>();
    if (log == nullptr) return;
    ++log->tick;
    if (log->tick <= kKeepTicks) return;
    const uint64_t oldest = log->tick - kKeepTicks;
    const auto stale = std::find_if(log->events.begin(), log->events.end(),
                                    [oldest](const Event& e) { return e.tick >= oldest; });
    log->events.erase(log->events.begin(), stale);
}

void Emit(entt::registry& registry, Event event) {
    Log* log = registry.ctx().find<Log>();
    if (log == nullptr) return;
    event.tick = log->tick;
    log->events.push_back(event);
}

const Log* Read(const entt::registry& registry) {
    return registry.ctx().find<Log>();
}

} // namespace ECS::Events
