#include "AnimationSystem.h"

#include <algorithm>
#include "ecs/Felling.h"
#include "ecs/components/Building.h"
#include "ecs/components/Animation.h"
#include "ecs/components/Combat.h"
#include "ecs/components/Health.h"
#include "ecs/components/Monk.h"
#include "ecs/components/Mortal.h"
#include "ecs/components/PartOf.h"
#include "ecs/components/Path.h"
#include "ecs/components/Sprite.h"
#include "ecs/components/SpritePieces.h"
#include "ecs/components/Worker.h"
#include "core/ConfigManager.h"
#include "core/Media.h"
#include "ecs/VillagerJobs.h"
#include <string>

namespace ECS::Systems {

namespace {

// A composite graphic's pieces, from the dat's deltas: each another graphic,
// or -- a delta naming no graphic -- the composite's own art, at its offset.
// A galley's standing graphic is a hull, sails and a wake and has no art of
// its own; a fishing ship's names art missing from the data beside its own,
// which is what it then shows. Resolved here, on the change of graphic, so
// drawing it only indexes atlases.
void ResolvePieces(entt::registry& registry, entt::entity entity, int graphic) {
    const auto& deltas = Core::Media::Deltas(graphic);
    if (deltas.empty()) {
        registry.remove<Components::SpritePieces>(entity);
        return;
    }
    auto& cfg = Core::ConfigManager::Get();
    Components::SpritePieces pieces;
    int order = 0;
    for (const auto& delta : deltas) {
        if (pieces.count >= Components::SpritePieces::kMax) break;
        const auto info = cfg.GetGraphic(delta.graphic >= 0 ? delta.graphic : graphic);
        ++order;
        if (!info.Valid() || info.angle_count <= 0) continue;
        Components::SpritePiece& piece = pieces.piece[pieces.count++];
        piece.slp_id = info.slp_id;
        piece.frames_per_angle = info.frame_count > 0 ? info.frame_count : 1;
        piece.angles = info.angle_count;
        piece.offset_x = delta.offset_x;
        piece.offset_y = delta.offset_y;
        piece.bias = info.layer * Components::SpritePieces::kMax + order;
    }
    registry.emplace_or_replace<Components::SpritePieces>(entity, pieces);
}

} // namespace

void AnimationSystem::LoadSet(Components::Animation& anim,
                              const std::string& unit_name) {
    static const char* kFields[] = {
        "idle_graphic0", "walking_graphic0", "attack_graphic", "dying_graphic",
        "work_graphic", "carry_graphic"
    };
    static_assert(sizeof(kFields) / sizeof(kFields[0]) ==
                      static_cast<size_t>(Components::AnimState::COUNT),
                  "one dat field per animation state");
    auto& cfg = Core::ConfigManager::Get();
    for (int a = 0; a < static_cast<int>(Components::AnimState::COUNT); ++a) {
        const auto gfx = cfg.GetUnitGraphic(unit_name, kFields[a]);
        anim.graphic[a] = gfx.slp_id >= 0 ? gfx.graphic_id : -1;
        anim.slp[a] = gfx.slp_id;
        anim.frames[a] = gfx.frame_count;
        anim.angles[a] = std::max(1, gfx.angle_count);
        anim.frame_ms[a] = cfg.GetFrameMs(gfx.graphic_id, 80);
    }
}

void AnimationSystem::Update(entt::registry& registry, int32_t dt_ms) {
    // 0. A villager changing job changes graphic set entirely.
    auto worker_view = registry.view<Components::Animation, Components::Worker>();
    for (auto entity : worker_view) {
        auto& anim = worker_view.get<Components::Animation>(entity);
        if (anim.job == Components::VillagerJob::None) continue;
        const auto wanted =
            Villagers::JobFor(worker_view.get<Components::Worker>(entity));
        if (wanted == anim.job) continue;
        anim.job = wanted;
        LoadSet(anim, Villagers::DatName(wanted, anim.female));
        anim.needs_apply = true;
    }

    // 1. Choose the animation each entity should be playing.
    auto anim_view = registry.view<Components::Animation, Components::Sprite>();
    const auto& dying_pool = registry.storage<Components::Dying>();
    const auto& combat_pool = registry.storage<Components::Combat>();
    const auto& monk_pool = registry.storage<Components::Monk>();
    const auto& path_pool = registry.storage<Components::Path>();
    const auto& worker_pool = registry.storage<Components::Worker>();
    for (auto entity : anim_view) {
        auto& anim = anim_view.get<Components::Animation>(entity);
        auto& sprite = anim_view.get<Components::Sprite>(entity);

        // Only ever ask for a state this graphic set actually has: a
        // villager's build set has no attack graphic at all.
        auto available = [&anim](Components::AnimState state) {
            const int index = static_cast<int>(state);
            return anim.slp[index] >= 0 && anim.frames[index] > 0;
        };

        Components::AnimState wanted = Components::AnimState::Idle;
        if (dying_pool.contains(entity) &&
            available(Components::AnimState::Die)) {
            wanted = Components::AnimState::Die;
        }
        if (wanted != Components::AnimState::Die) {
            // The attack graphic plays for each swing and only then; between
            // swings, waiting on the reload, a unit stands.
            if (combat_pool.contains(entity)) {
                if (combat_pool.get(entity).swing_ms > 0 && available(Components::AnimState::Attack)) {
                    wanted = Components::AnimState::Attack;
                }
            }
            // A monk at a conversion or a heal shows its chanting graphic,
            // which the dat keeps in its attack slot.
            if (monk_pool.contains(entity)) {
                if (monk_pool.get(entity).chanting && available(Components::AnimState::Attack)) {
                    wanted = Components::AnimState::Attack;
                }
            }
            if (wanted == Components::AnimState::Idle &&
                available(Components::AnimState::Walk)) {
                if (path_pool.contains(entity)) {
                    const auto& path = path_pool.get(entity);
                    if (path.current_waypoint_index < path.waypoints.size()) {
                        wanted = Components::AnimState::Walk;
                    }
                }
            }
            if (wanted != Components::AnimState::Attack) {
                if (worker_pool.contains(entity)) {
                    const auto* worker = &worker_pool.get(entity);
                    // Walking home with a load is its own graphic: a
                    // lumberjack shoulders the logs rather than walking
                    // empty-handed.
                    if (wanted == Components::AnimState::Walk &&
                        available(Components::AnimState::Carry)) {
                        bool loaded = false;
                        for (int r = 0;
                             r < static_cast<int>(Core::ResourceType::COUNT); ++r) {
                            if (worker->carried[r] > 0) { loaded = true; break; }
                        }
                        if (loaded) wanted = Components::AnimState::Carry;
                    }
                    // Doing the job wins over standing and over walking: a
                    // villager that has arrived and started swinging is not
                    // walking any more even if a stale route is still attached.
                    // Felling a standing tree is the felling stroke, the
                    // lumberjack's attack graphic; the trunk on the ground
                    // is the work graphic.
                    if (worker->state == Core::UnitState::Gathering &&
                        Felling::Standing(registry, worker->target_resource) &&
                        available(Components::AnimState::Attack)) {
                        wanted = Components::AnimState::Attack;
                    } else if (worker->working && available(Components::AnimState::Work)) {
                        wanted = Components::AnimState::Work;
                    }
                }
            }
        }

        if (!anim.needs_apply && wanted == anim.state) {
            continue;
        }

        int idx = static_cast<int>(wanted);
        // Fall back to idle when a unit has no graphic for the state it wants.
        if (anim.slp[idx] < 0 || anim.frames[idx] <= 0) {
            idx = static_cast<int>(Components::AnimState::Idle);
        }

        anim.state = wanted;
        anim.needs_apply = false;
        if (anim.slp[idx] < 0 || anim.frames[idx] <= 0) {
            continue;
        }

        anim.shown = static_cast<Components::AnimState>(idx);
        sprite.slp_id = anim.slp[idx];
        sprite.is_file_sprite = true;
        sprite.frames_per_angle = anim.frames[idx];
        sprite.angles = anim.angles[idx];
        // Standing in its walking graphic -- a packed trebuchet has no other
        // -- it holds that graphic's first frame.
        const int walk = static_cast<int>(Components::AnimState::Walk);
        sprite.still = anim.shown == Components::AnimState::Idle && anim.slp[walk] == anim.slp[idx];
        sprite.animation_speed = anim.frame_ms[idx];
        sprite.current_frame = 0;
        sprite.animation_timer = 0;
        const auto sequence = Core::ConfigManager::Get().GetSequence(anim.graphic[idx]);
        sprite.replay_ms = sequence.replay_ms;
        sprite.once = (sequence.flags & Core::ConfigManager::kSequenceOnce) != 0;
        // A randomized sequence starts after a rest of its own length, so a
        // crowd standing about does not all move at once.
        sprite.replay_left = 0;
        if ((sequence.flags & Core::ConfigManager::kSequenceRandomized) != 0 && sequence.replay_ms > 0) {
            sprite.replay_left = static_cast<int>(
                (entt::to_integral(entity) * 2654435761u) % static_cast<uint32_t>(sequence.replay_ms));
        }
        // A different graphic has a different companion shadow.
        sprite.shadow_resolved = false;
        ResolvePieces(registry, entity, anim.graphic[idx]);
    }

    // 2. Buildings under construction step through their stages by progress
    //    rather than by time.
    //    Progress, not hit points: a site that is attacked loses hit points
    //    without its scaffolding coming back down.
    auto build_view = registry.view<Components::Sprite, Components::Building>();
    for (auto entity : build_view) {
        auto& sprite = build_view.get<Components::Sprite>(entity);
        if (sprite.construction_frames <= 0) continue;
        const auto& site = build_view.get<Components::Building>(entity);
        const int stage = (site.progress * Core::Fixed32(sprite.construction_frames)).ToInt();
        sprite.slp_frame = sprite.construction_offset +
                           std::clamp(stage, 0, sprite.construction_frames - 1);
    }
    auto layers = registry.view<Components::Sprite, Components::PartOf>();
    for (auto entity : layers) {
        auto& sprite = layers.get<Components::Sprite>(entity);
        if (!sprite.site_layer) continue;
        const auto* site = registry.try_get<Components::Sprite>(layers.get<Components::PartOf>(entity).building);
        if (site != nullptr) sprite.slp_frame = site->slp_frame;
    }

    // 3. Advance every animating sprite, straight down the storage's packed
    //    array: each on its own.
    for (auto& sprite : registry.storage<Components::Sprite>()) {
        if (sprite.construction_frames > 0 || sprite.site_layer) continue;

        if (!sprite.still && sprite.frames_per_angle > 1 && sprite.animation_speed > 0) {
            // Resting between plays, on the first frame.
            if (sprite.replay_left > 0) {
                sprite.replay_left -= dt_ms;
                if (sprite.replay_left > 0) continue;
                sprite.replay_left = 0;
                sprite.animation_timer = 0;
            }
            sprite.animation_timer += dt_ms;
            if (sprite.animation_timer >= sprite.animation_speed) {
                const int frames_to_advance = sprite.animation_timer / sprite.animation_speed;
                sprite.animation_timer %= sprite.animation_speed;
                const int next = sprite.current_frame + frames_to_advance;
                if (next < sprite.frames_per_angle) {
                    sprite.current_frame = next;
                } else if (sprite.once) {
                    sprite.current_frame = sprite.frames_per_angle - 1;
                    sprite.still = true;
                } else if (sprite.replay_ms > 0) {
                    sprite.current_frame = 0;
                    sprite.replay_left = sprite.replay_ms;
                    sprite.animation_timer = 0;
                } else {
                    sprite.current_frame = next % sprite.frames_per_angle;
                }
            }
        }
    }
}

} // namespace ECS::Systems
