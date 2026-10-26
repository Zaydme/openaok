#include "audio/Voices.h"

#include "audio/SoundTable.h"
#include "core/Rules.h"
#include "ecs/Players.h"
#include "ecs/VillagerJobs.h"
#include "ecs/components/Animation.h"
#include "ecs/components/Building.h"
#include "ecs/components/Identity.h"
#include "ecs/components/Owner.h"
#include "ecs/components/Selectable.h"
#include "hal/Audio.h"

namespace Audio::Voices {

namespace {

int g_last_voice = -1;

Core::PlayerId OwnerOf(const entt::registry& registry, entt::entity entity) {
    const auto* owner = registry.try_get<ECS::Components::Owner>(entity);
    return owner != nullptr ? owner->player_id : Core::PLAYER_GAIA;
}

int CivOf(const entt::registry& registry, Core::PlayerId player) {
    const ECS::Players::Player* seat = ECS::Players::Find(registry, player);
    return seat != nullptr ? seat->civ : -1;
}

// Speaks a dat sound, unless the last line is still being spoken.
void Say(int sound_id, int civ) {
    if (sound_id < 0) return;
    if (g_last_voice >= 0 && HAL::Audio::Playing(g_last_voice)) return;
    const int file = SoundTable::Pick(sound_id, civ);
    if (file >= 0 && HAL::Audio::Play(file, 1.0f, 0.0f)) g_last_voice = file;
}

const Core::Rules::UnitRules* RulesOf(const entt::registry& registry, Core::PlayerId player,
                                      int unit_id) {
    return unit_id >= 0 ? ECS::Players::UnitOf(registry, player, unit_id) : nullptr;
}

} // namespace

void Selected(const entt::registry& registry, Core::PlayerId listener) {
    auto view = registry.view<ECS::Components::Selectable, ECS::Components::Identity>();
    for (auto entity : view) {
        if (!view.get<ECS::Components::Selectable>(entity).is_selected) continue;
        const Core::PlayerId owner = OwnerOf(registry, entity);
        if (owner != listener && owner != Core::PLAYER_GAIA) continue;
        const auto* rules = RulesOf(registry, owner, ECS::Villagers::DisplayUnit(registry, entity));
        if (rules == nullptr) continue;
        Say(rules->sounds.select, CivOf(registry, owner));
        return;
    }
}

void Acknowledge(const entt::registry& registry, entt::entity unit, Order order,
                 entt::entity target, Core::PlayerId listener) {
    if (!registry.valid(unit) || OwnerOf(registry, unit) != listener) return;
    const int civ = CivOf(registry, listener);
    const auto* identity = registry.try_get<ECS::Components::Identity>(unit);
    if (identity == nullptr) return;
    const auto* rules = RulesOf(registry, listener, identity->unit_id);
    if (rules == nullptr) return;

    if (order == Order::Move) {
        Say(rules->sounds.move, civ);
        return;
    }
    if (order == Order::Attack) {
        Say(rules->sounds.attack, civ);
        return;
    }
    // Work: the job unit's own acknowledgement, which the dat keeps in its
    // attack slot -- a lumberjack's "chop", a builder's hammering reply.
    const auto* anim = registry.try_get<ECS::Components::Animation>(unit);
    const bool female = anim != nullptr && anim->female;
    ECS::Components::VillagerJob job = ECS::Components::VillagerJob::Build;
    if (registry.valid(target) && !registry.all_of<ECS::Components::Building>(target)) {
        bool hunt = false;
        job = ECS::Villagers::JobForTarget(registry, target, hunt);
    }
    const auto* worker = RulesOf(registry, listener, ECS::Villagers::UnitId(job, female));
    Say(worker != nullptr ? worker->sounds.attack : rules->sounds.move, civ);
}

void Placed(const entt::registry& registry, int unit_id, Core::PlayerId listener) {
    const auto* rules = RulesOf(registry, listener, unit_id);
    if (rules == nullptr || rules->sounds.construction < 0) return;
    const int file = SoundTable::Pick(rules->sounds.construction, CivOf(registry, listener));
    if (file >= 0) HAL::Audio::Play(file, 1.0f, 0.0f);
}

void Hear(const entt::registry& registry, const ECS::Events::Event& event,
          Core::PlayerId listener) {
    if (event.kind != ECS::Events::Kind::Born || event.player != listener) return;
    const auto* rules = RulesOf(registry, listener, event.id);
    if (rules == nullptr || rules->sounds.train < 0) return;
    const int file = SoundTable::Pick(rules->sounds.train, CivOf(registry, listener));
    if (file >= 0) HAL::Audio::Play(file, 1.0f, 0.0f);
}

} // namespace Audio::Voices
