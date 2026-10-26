#include "audio/WorldSounds.h"

#include <unordered_map>

#include "audio/SoundTable.h"
#include "core/Media.h"
#include "ecs/Players.h"
#include "ecs/Vision.h"
#include "ecs/components/Animation.h"
#include "ecs/components/Owner.h"
#include "ecs/components/Sprite.h"
#include "ecs/components/Transform.h"
#include "hal/Audio.h"

namespace Audio::WorldSounds {

namespace {

// What an entity was showing when last heard.
struct Seen {
    int graphic = -1;
    int frame = 0;
};

std::unordered_map<entt::entity, Seen> g_seen;
std::unordered_map<entt::entity, Seen> g_next;

int CivOf(const entt::registry& registry, entt::entity entity) {
    const auto* owner = registry.try_get<ECS::Components::Owner>(entity);
    if (owner == nullptr) return -1;
    const ECS::Players::Player* player = ECS::Players::Find(registry, owner->player_id);
    return player != nullptr ? player->civ : -1;
}

// Plays a dat sound from a place in the world. The same file already playing
// is not started again on top of itself: twenty lumberjacks chop as a steady
// rhythm, not as one deafening crack.
void Sound(int sound_id, int civ, const Placement& placement) {
    const int file = SoundTable::Pick(sound_id, civ);
    if (file < 0 || HAL::Audio::Playing(file)) return;
    HAL::Audio::Play(file, placement.volume, placement.pan);
}

// Every frame sound in (from, to], stepping forward round a looping cycle of
// `frames`.
void FrameSounds(int graphic, int facing, int from, int to, int frames, int civ,
                 const Placement& placement) {
    const auto& angles = Core::Media::FrameSounds(graphic);
    if (angles.empty() || frames <= 0) return;
    const Core::Media::AngleSounds& angle =
        angles[static_cast<size_t>(facing) % angles.size()];
    // Frames passed since last seen, at most one whole cycle.
    int steps = (to - from + frames) % frames;
    for (int s = 1; s <= steps; ++s) {
        const int frame = (from + s) % frames;
        for (const auto& cue : angle.sounds) {
            if (cue.sound >= 0 && cue.frame == frame) Sound(cue.sound, civ, placement);
        }
    }
}

} // namespace

void Update(const entt::registry& registry, const Listener& listener,
            Core::PlayerId viewer) {
    g_next.clear();
    auto view = registry.view<ECS::Components::Animation, ECS::Components::Sprite,
                              ECS::Components::Transform>();
    for (auto entity : view) {
        const auto& at = view.get<ECS::Components::Transform>(entity).position;
        const Placement placement = Place(listener, at.x, at.y);
        if (placement.volume <= 0.0f) continue;
        if (!ECS::Vision::Shows(registry, viewer, entity)) continue;

        const auto& anim = view.get<ECS::Components::Animation>(entity);
        const auto& sprite = view.get<ECS::Components::Sprite>(entity);
        const Seen now{anim.graphic[static_cast<int>(anim.shown)], sprite.current_frame};
        g_next[entity] = now;

        auto before = g_seen.find(entity);
        if (before == g_seen.end() || now.graphic < 0) continue;
        const int civ = CivOf(registry, entity);
        if (before->second.graphic != now.graphic) {
            // A new graphic: its own sound, and whatever its frames up to the
            // current one carry.
            const int start = Core::Media::GraphicSound(now.graphic);
            if (start >= 0) Sound(start, civ, placement);
            FrameSounds(now.graphic, static_cast<int>(sprite.facing), -1, now.frame,
                        sprite.frames_per_angle, civ, placement);
        } else if (before->second.frame != now.frame) {
            FrameSounds(now.graphic, static_cast<int>(sprite.facing), before->second.frame,
                        now.frame, sprite.frames_per_angle, civ, placement);
        }
    }
    g_seen.swap(g_next);
}

void Reset() {
    g_seen.clear();
    g_next.clear();
}

} // namespace Audio::WorldSounds
