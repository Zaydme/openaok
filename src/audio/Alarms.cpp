#include "audio/Alarms.h"

#include <vector>

#include "core/FixedMath.h"
#include "ecs/Combat.h"
#include "ecs/components/Building.h"
#include "ecs/components/Owner.h"
#include "ecs/components/Worker.h"
#include "hal/Audio.h"

namespace Audio::Alarms {

namespace {

// interfac.drs file ids.
constexpr int kConversionHorn = 50313;
constexpr int kMilitaryHorn = 50315;
constexpr int kTownBell = 50316;
constexpr int kWildAnimal = 50366;

// A battle's reach either side of where it began, and how long it lasts
// after the last blow: ten seconds of 16 ms ticks.
const Core::Fixed32 kBattleReach = Core::Fixed32(10);
constexpr uint64_t kBattleTicks = 10000 / 16;

struct Battle {
    ECS::Components::Vector2D centre{};
    uint64_t last = 0;
};

std::vector<Battle> g_battles;
// Alarms sounded and not yet taken by the minimap.
std::vector<Raised> g_raised;
size_t g_taken = 0;

Core::PlayerId OwnerOf(const entt::registry& registry, entt::entity entity) {
    const auto* owner = registry.try_get<ECS::Components::Owner>(entity);
    return owner != nullptr ? owner->player_id : Core::PLAYER_GAIA;
}

// The battle a blow at `at` belongs to, if one is still going.
Battle* BattleAt(const ECS::Components::Vector2D& at, uint64_t tick) {
    for (Battle& battle : g_battles) {
        if (tick - battle.last > kBattleTicks) continue;
        if (Core::FixedMath::Abs(at.x - battle.centre.x) <= kBattleReach &&
            Core::FixedMath::Abs(at.y - battle.centre.y) <= kBattleReach) {
            return &battle;
        }
    }
    return nullptr;
}

} // namespace

void Hear(const entt::registry& registry, const ECS::Events::Event& event,
          Core::PlayerId listener) {
    // A monk at work on one of the listener's units: the horn and the monk,
    // once per battle like any other alarm.
    if (event.kind == ECS::Events::Kind::Converting) {
        if (event.player != listener || BattleAt(event.at, event.tick) != nullptr) return;
        std::erase_if(g_battles, [&](const Battle& b) { return event.tick - b.last > kBattleTicks; });
        g_battles.push_back({event.at, event.tick});
        g_raised.push_back({event.subject, event.at, OwnerOf(registry, event.other), true});
        HAL::Audio::Play(kConversionHorn, 1.0f, 0.0f);
        return;
    }
    if (event.kind != ECS::Events::Kind::Struck) return;
    if (!registry.valid(event.subject) || !registry.valid(event.other)) return;

    const bool ours_struck = event.player == listener;
    const bool ours_striking = OwnerOf(registry, event.other) == listener;
    if (!ours_struck && !ours_striking) return;

    // Any blow either way keeps a battle going.
    if (Battle* battle = BattleAt(event.at, event.tick)) {
        battle->last = event.tick;
        return;
    }
    // Only being struck by a foe starts one: a player not on the listener's
    // side, or a Gaia animal that hunts. A boar turning on its hunter is not
    // an attack.
    const bool foe = OwnerOf(registry, event.other) == Core::PLAYER_GAIA
        ? ECS::Combat::Hostile(registry, event.other, event.subject)
        : !ECS::Combat::Friendly(registry, event.subject, event.other);
    if (!ours_struck || !foe) return;

    std::erase_if(g_battles, [&](const Battle& b) { return event.tick - b.last > kBattleTicks; });
    g_battles.push_back({event.at, event.tick});

    int alarm = kMilitaryHorn;
    if (OwnerOf(registry, event.other) == Core::PLAYER_GAIA) {
        alarm = kWildAnimal;
    } else if (registry.any_of<ECS::Components::Worker, ECS::Components::Building>(event.subject)) {
        alarm = kTownBell;
    }
    g_raised.push_back({event.subject, event.at, OwnerOf(registry, event.other), false});
    HAL::Audio::Play(alarm, 1.0f, 0.0f);
}

void Reset() {
    g_battles.clear();
    g_raised.clear();
    g_taken = 0;
}

bool TakeRaised(Raised& alarm) {
    if (g_taken >= g_raised.size()) {
        g_raised.clear();
        g_taken = 0;
        return false;
    }
    alarm = g_raised[g_taken++];
    return true;
}

} // namespace Audio::Alarms
