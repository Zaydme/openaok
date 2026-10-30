#include "ecs/Units.h"

#include <algorithm>
#include <string>

#include "core/ConfigManager.h"
#include "core/FixedMath.h"
#include "core/Rules.h"
#include "ecs/Combat.h"
#include "ecs/Players.h"
#include "ecs/Score.h"
#include "ecs/components/Animation.h"
#include "ecs/components/Combat.h"
#include "ecs/components/Monk.h"
#include "ecs/components/Health.h"
#include "ecs/components/Identity.h"
#include "ecs/components/Owner.h"
#include "ecs/components/Selectable.h"
#include "ecs/components/Sight.h"
#include "ecs/components/Sprite.h"
#include "ecs/components/Worker.h"
#include "ecs/systems/AnimationSystem.h"

namespace ECS::Units {

namespace {
// What a unit falls back to when the dat leaves a figure out -- a hit point
// total or a sight of zero, which nothing the player can make has.
constexpr int kFallbackHitPoints = 25;
constexpr int kFallbackSight = 5;
} // namespace

entt::entity Spawn(entt::registry& registry, int unit_id, Core::PlayerId player,
                   Components::Vector2D at) {
    auto& cfg = Core::ConfigManager::Get();
    // The owner's own rules for the unit, which technologies may have changed
    // -- a villager trained after Loom comes out with Loom's hit points -- and
    // its art, which an upgrade may have replaced.
    const Core::Rules::UnitRules* rules = Players::UnitOf(registry, player, unit_id);
    const std::string dat_name =
        cfg.GetUnitDatName(Players::ArtOf(registry, player, unit_id));
    if (dat_name.empty() || rules == nullptr) return entt::null;

    const entt::entity unit = registry.create();
    registry.emplace<Components::Transform>(unit, at, at);

    auto& sprite = registry.emplace<Components::Sprite>(unit);
    sprite.is_file_sprite = true;
    sprite.heading = Core::FixedMath::HeadingOf(Core::Direction::SouthEast);
    sprite.width = 0;   // read off the graphic on first draw
    sprite.height = 0;

    // Every animation is its own graphic, with its own frame timing from the
    // dat; the animation system takes it from here.
    const bool villager = rules->cls == kVillagerClass;
    Components::Animation anim{};
    Systems::AnimationSystem::LoadSet(anim, dat_name);
    if (villager) {
        // A villager changes graphic set with its job, which needs to know
        // which of the two villager units -- male or female -- it is.
        anim.female = dat_name.size() > 1 && dat_name[1] == 'F';
        anim.job = Components::VillagerJob::Base;
    }
    registry.emplace<Components::Animation>(unit, anim);

    const int hp = rules->hp > 0 ? rules->hp : kFallbackHitPoints;
    const int sight = rules->los.ToInt() > 0 ? rules->los.ToInt() : kFallbackSight;
    registry.emplace<Components::Identity>(unit, unit_id);
    registry.emplace<Components::Selectable>(unit,
        Components::Selectable{.is_selected = false, .radius = 1, .priority = 1});
    registry.emplace<Components::Health>(unit, hp, hp);
    registry.emplace<Components::Sight>(unit, sight);
    registry.emplace<Components::Owner>(unit, player);
    // A villager works; so does anything else whose tasks gather -- a fishing
    // ship -- by its own rules rather than a villager job's.
    bool gathers = false;
    for (int t = 0; t < rules->task_count; ++t) {
        if (rules->tasks[t].action == Core::Rules::TaskAction::Gather) gathers = true;
    }
    if (villager) {
        registry.emplace<Components::Worker>(unit);
    } else if (gathers) {
        auto& worker = registry.emplace<Components::Worker>(unit);
        worker.gatherer = unit_id;
        if (rules->carry > 0) worker.max_carry = rules->carry;
    }
    // Anything with an attack in its rules can fight -- villagers too, which
    // punch, and as hunters throw spears.
    if (rules->attack_count > 0 && rules->reload.value > 0) {
        registry.emplace<Components::Combat>(unit);
        Combat::FirstLook(registry, unit);
    }
    // Anything whose tasks convert or heal is a monk, with its faith.
    for (int t = 0; t < rules->task_count; ++t) {
        if (rules->tasks[t].action == 104 || rules->tasks[t].action == 105) {
            registry.emplace<Components::Monk>(unit);
            break;
        }
    }

    // Its place in its owner's population, and anything else it contributes
    // while it lives.
    Players::Contribute(registry, player, unit_id,
                        Core::Rules::StorageMode::Permanent |
                            Core::Rules::StorageMode::WhileAlive,
                        +1);
    // Its worth, in the economy score (ecs/Score).
    Score::UnitCreated(registry, player, unit_id);
    return unit;
}

void Become(entt::registry& registry, entt::entity unit, int unit_id) {
    auto* identity = registry.try_get<Components::Identity>(unit);
    const auto* owner = registry.try_get<Components::Owner>(unit);
    if (identity == nullptr || owner == nullptr || identity->unit_id == unit_id) return;
    const Core::PlayerId player = owner->player_id;
    // By the table it plays by: a converted unit goes on with its maker's.
    const Core::PlayerId maker = Players::RulesPlayer(registry, unit);
    const Core::Rules::UnitRules* rules = Players::UnitOf(registry, maker, unit_id);
    const std::string dat_name =
        Core::ConfigManager::Get().GetUnitDatName(Players::ArtOf(registry, maker, unit_id));
    if (rules == nullptr || dat_name.empty()) return;

    Players::Contribute(registry, player, identity->unit_id,
                        Core::Rules::StorageMode::WhileAlive, -1);
    identity->unit_id = unit_id;
    Players::Contribute(registry, player, unit_id, Core::Rules::StorageMode::WhileAlive, +1);

    if (auto* anim = registry.try_get<Components::Animation>(unit)) {
        Systems::AnimationSystem::LoadSet(*anim, dat_name);
        anim->needs_apply = true;
    }
    if (auto* health = registry.try_get<Components::Health>(unit)) {
        const int max_hp = rules->hp > 0 ? rules->hp : kFallbackHitPoints;
        const int was = std::max(1, health->max_hp);
        health->current_hp = std::max(1, health->current_hp * max_hp / was);
        health->max_hp = max_hp;
    }
    if (auto* sight = registry.try_get<Components::Sight>(unit)) {
        sight->radius = rules->los.ToInt() > 0 ? rules->los.ToInt() : kFallbackSight;
    }
}

} // namespace ECS::Units
