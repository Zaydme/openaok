#include "ecs/Mortality.h"

#include "ecs/Events.h"

#include <algorithm>
#include <vector>

#include "core/ConfigManager.h"
#include "core/Rules.h"
#include "ecs/Buildings.h"
#include "ecs/Players.h"
#include "ecs/Score.h"
#include "ecs/components/Animation.h"
#include "ecs/components/Building.h"
#include "ecs/components/Combat.h"
#include "ecs/components/Garrison.h"
#include "ecs/components/Health.h"
#include "ecs/components/Identity.h"
#include "ecs/components/Mortal.h"
#include "ecs/components/Owner.h"
#include "ecs/components/Path.h"
#include "ecs/components/Resource.h"
#include "ecs/components/Selectable.h"
#include "ecs/components/Sight.h"
#include "ecs/components/Sprite.h"
#include "ecs/components/Transform.h"
#include "ecs/components/Worker.h"

namespace ECS::Mortality {

namespace {

namespace R = Core::Rules;

// How long a unit with no dying graphic takes to die.
constexpr int32_t kDefaultDyingMs = 1000;

// The dat resource a dead unit stores as how long its body lies, in seconds.
constexpr int kCorpseDecayTime = 12;

Core::PlayerId OwnerOf(const entt::registry& registry, entt::entity e) {
    const auto* owner = registry.try_get<Components::Owner>(e);
    return owner != nullptr ? owner->player_id : Core::PLAYER_GAIA;
}

// Replaces a unit that has finished dying with its dead unit.
void LayDown(entt::registry& registry, entt::entity dead) {
    const auto* identity = registry.try_get<Components::Identity>(dead);
    const auto* at = registry.try_get<Components::Transform>(dead);
    if (identity == nullptr || at == nullptr) return;
    // The body is the dead unit of the table it played by.
    const Core::PlayerId owner = Players::RulesPlayer(registry, dead);
    const R::UnitRules* rules = Players::UnitOf(registry, owner, identity->unit_id);
    if (rules == nullptr || rules->dead_unit < 0) return;
    const R::UnitRules* remains = Players::UnitOf(registry, owner, rules->dead_unit);

    // The dead unit's standing graphic, from the rules -- which carry every
    // unit, dead ones included, where the art tables only carry the living.
    auto& cfg = Core::ConfigManager::Get();
    const R::UnitRules* drawn =
        Players::UnitOf(registry, owner, Players::ArtOf(registry, owner, rules->dead_unit));
    const auto art = cfg.GetGraphic(drawn != nullptr ? drawn->standing_graphic : -1);
    if (!art.Valid()) return;

    const entt::entity body = registry.create();
    registry.emplace<Components::Transform>(body, at->position, at->position);
    Components::Sprite sprite{};
    sprite.slp_id = art.slp_id;
    sprite.is_file_sprite = true;
    sprite.frames_per_angle = std::max(1, art.frame_count);
    sprite.still = true;   // lies still, facing the way it fell
    sprite.width = 0;
    sprite.height = 0;
    if (const auto* fell = registry.try_get<Components::Sprite>(dead)) {
        sprite.facing = fell->facing;
    }
    registry.emplace<Components::Sprite>(body, sprite);
    registry.emplace<Components::Owner>(body, owner);

    Components::Corpse corpse;
    if (remains != nullptr) {
        for (int i = 0; i < remains->storage_count; ++i) {
            if (remains->storage[i].resource == kCorpseDecayTime) {
                corpse.remaining_ms =
                    (remains->storage[i].amount * Core::Fixed32(1000)).ToInt();
            }
        }
    }

    // An animal's food stays with its carcass, which can be gathered and rots.
    if (const auto* food = registry.try_get<Components::Resource>(dead)) {
        if (food->current_amount > 0) {
            registry.emplace<Components::Resource>(body, *food);
            registry.emplace<Components::Identity>(body, rules->dead_unit);
            registry.emplace<Components::Selectable>(body,
                Components::Selectable{.is_selected = false, .radius = 1, .priority = 0});
            corpse.rot_per_second = rules->decay;
            corpse.remaining_ms = 0;
            // Whoever was after the animal goes on to the carcass.
            auto workers = registry.view<Components::Worker>();
            for (auto worker : workers) {
                auto& job = workers.get<Components::Worker>(worker);
                if (job.target_resource == dead) job.target_resource = body;
            }
        }
    }
    registry.emplace<Components::Corpse>(body, corpse);
}

} // namespace

void Kill(entt::registry& registry, Physics::Grid& grid, entt::entity victim) {
    if (!registry.valid(victim) || registry.all_of<Components::Dying>(victim)) return;

    if (registry.all_of<Components::Building>(victim)) {
        Buildings::Destroy(registry, grid, victim);
        return;
    }
    // Its worth leaves the economy score as it starts to die (ecs/Score).
    Score::Removed(registry, victim);

    if (auto* health = registry.try_get<Components::Health>(victim)) health->current_hp = 0;
    // A transport that sinks takes those aboard down with it: a ship has no
    // door to turn them out at, as a falling building does.
    if (const auto* aboard = registry.try_get<Components::Garrison>(victim)) {
        const std::vector<entt::entity> inside = aboard->inside;
        for (entt::entity unit : inside) {
            if (registry.valid(unit)) Kill(registry, grid, unit);
        }
        registry.remove<Components::Garrison>(victim);
    }
    {
        const auto* owner = registry.try_get<Components::Owner>(victim);
        const auto* identity = registry.try_get<Components::Identity>(victim);
        const auto* at = registry.try_get<Components::Transform>(victim);
        Events::Emit(registry, {.kind = Events::Kind::Died, .subject = victim,
                                .player = owner != nullptr ? owner->player_id : Core::PLAYER_GAIA,
                                .id = identity != nullptr ? identity->unit_id : -1,
                                .at = at != nullptr ? at->position : Components::Vector2D{}});
    }
    // Out of everything at once: its place in the population given back, and
    // not something to select or order, nor that fights or works.
    if (const auto* identity = registry.try_get<Components::Identity>(victim)) {
        Players::Contribute(registry, OwnerOf(registry, victim), identity->unit_id,
                            R::StorageMode::WhileAlive, -1);
    }
    registry.remove<Components::Path, Components::Combat, Components::Worker,
                    Components::Selectable, Components::Sight>(victim);

    int32_t dying_ms = kDefaultDyingMs;
    if (const auto* anim = registry.try_get<Components::Animation>(victim)) {
        const int die = static_cast<int>(Components::AnimState::Die);
        if (anim->slp[die] >= 0 && anim->frames[die] > 0) {
            dying_ms = anim->frames[die] * anim->frame_ms[die];
        }
    }
    registry.emplace<Components::Dying>(victim, dying_ms);
}

void Update(entt::registry& registry, Physics::Grid& /*grid*/, int32_t dt_ms) {
    std::vector<entt::entity> done;
    auto dying = registry.view<Components::Dying>();
    for (auto e : dying) {
        auto& d = dying.get<Components::Dying>(e);
        d.remaining_ms -= dt_ms;
        if (d.remaining_ms <= 0) done.push_back(e);
    }
    for (auto e : done) {
        LayDown(registry, e);
        registry.destroy(e);
    }

    // Bodies lie their time; carcasses rot until nothing is left.
    std::vector<entt::entity> gone;
    auto bodies = registry.view<Components::Corpse>();
    for (auto e : bodies) {
        auto& corpse = bodies.get<Components::Corpse>(e);
        if (auto* food = registry.try_get<Components::Resource>(e)) {
            corpse.rot = corpse.rot +
                         corpse.rot_per_second * Core::Fixed32(dt_ms) / Core::Fixed32(1000);
            const int lost = corpse.rot.ToInt();
            if (lost > 0) {
                corpse.rot = corpse.rot - Core::Fixed32(lost);
                food->current_amount -= lost;
            }
            if (food->current_amount <= 0) gone.push_back(e);
            continue;
        }
        corpse.remaining_ms -= dt_ms;
        if (corpse.remaining_ms <= 0) gone.push_back(e);
    }
    for (auto e : gone) {
        // A hunter still on its way to a rotted-away carcass has nothing to go to.
        auto workers = registry.view<Components::Worker>();
        for (auto worker : workers) {
            auto& job = workers.get<Components::Worker>(worker);
            if (job.target_resource == e) job.target_resource = entt::null;
        }
        registry.destroy(e);
    }
}

} // namespace ECS::Mortality
