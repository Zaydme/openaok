#include "ecs/Flares.h"

#include <algorithm>

#include "core/ConfigManager.h"
#include "core/Rules.h"
#include "ecs/Diplomacy.h"
#include "ecs/Events.h"
#include "ecs/Players.h"
#include "ecs/Victory.h"

namespace ECS::Flares {

namespace {

// The dat's "corpse decay time": what a flare holds and spends.
constexpr int kDecayTime = 12;
constexpr int32_t kTickMs = 16;

uint16_t Bit(Core::PlayerId player) {
    return static_cast<uint16_t>(1u << player);
}

// The FLARE unit's lifetime: what it holds of the decay time, over the rate
// it decays.
int32_t Lifetime() {
    const Core::Rules::UnitRules* flare =
        Core::Rules::Unit(Core::ConfigManager::Get().GetUnitIdByName("FLARE"));
    if (flare == nullptr || flare->decay.value <= 0) return Log{}.lifetime_ms;
    for (int i = 0; i < flare->storage_count; ++i) {
        if (flare->storage[i].resource != kDecayTime) continue;
        return (flare->storage[i].amount * Core::Fixed32(1000) / flare->decay).ToInt();
    }
    return Log{}.lifetime_ms;
}

} // namespace

void Setup(entt::registry& registry) {
    Log log;
    log.lifetime_ms = Lifetime();
    registry.ctx().insert_or_assign(log);
}

bool Send(entt::registry& registry, Core::PlayerId from, const Components::Vector2D& at) {
    Log* log = registry.ctx().find<Log>();
    if (log == nullptr || from == Core::PLAYER_GAIA || from >= Core::kMaxPlayers) return false;
    if (Players::Find(registry, from) == nullptr) return false;
    if (const Victory::Outcome* outcome = Victory::Read(registry);
        outcome != nullptr && outcome->Defeated(from)) {
        return false;
    }
    const Events::Log* events = Events::Read(registry);
    const uint64_t now = events != nullptr ? events->tick : 0;

    Flare flare;
    flare.tick = now;
    flare.from = from;
    flare.at = at;
    flare.to = Bit(from);
    for (int p = 1; p < Core::kMaxPlayers; ++p) {
        const auto other = static_cast<Core::PlayerId>(p);
        if (other == from || Players::Find(registry, other) == nullptr) continue;
        if (Diplomacy::StanceOf(registry, from, other) == Diplomacy::Stance::Ally) {
            flare.to = static_cast<uint16_t>(flare.to | Bit(other));
        }
    }
    // Spent flares go as new ones come.
    std::erase_if(log->flares, [&](const Flare& f) { return !Showing(*log, f, now); });
    log->flares.push_back(flare);
    return true;
}

const Log* Read(const entt::registry& registry) {
    return registry.ctx().find<Log>();
}

bool Showing(const Log& log, const Flare& flare, uint64_t tick) {
    return tick >= flare.tick &&
           static_cast<int64_t>(tick - flare.tick) * kTickMs < log.lifetime_ms;
}

} // namespace ECS::Flares
