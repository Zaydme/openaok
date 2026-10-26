#pragma once
#include <cstdint>

namespace ECS::Components {

// The dat stores every animation of a unit as its own graphic, and therefore
// its own SLP: standing, walking, attacking and dying are separate sprite sets
// rather than frame ranges inside one sheet. Switching animation means
// repointing the Sprite at a different graphic.
enum class AnimState : uint8_t {
    Idle = 0,
    Walk,
    Attack,
    Die,
    // The job being done: a builder's hammering, a lumberjack's axe swing.
    // Not the unit's standing graphic: it comes from the dat's task list, a
    // table parallel to the unit table.
    Work,
    // The walk home with a load: a lumberjack shouldering logs. Its own
    // graphic in the dat's task list, not a variation on the ordinary walk.
    Carry,
    COUNT
};

// A villager's whole graphic set changes with its job: the dat carries a
// separate unit per job and sex (VMLUM, VFGLD, ...), not just a different
// animation. None means the entity is not a villager and never re-resolves.
enum class VillagerJob : uint8_t {
    None = 0, Base, Build, Farm, Forage, Gold, Hunt, Lumber, Mine, Shepherd, Fish,
    // The repairer (VMREP / VFREP): a hammer of its own, and its own name.
    Repair
};

struct Animation {
    // Parallel arrays indexed by AnimState: the dat graphic, its SLP, its
    // frames per angle, and how long one frame lasts. The graphic id is what
    // the dat hangs a graphic's sounds on.
    int graphic[static_cast<int>(AnimState::COUNT)] = {-1, -1, -1, -1, -1, -1};
    int slp[static_cast<int>(AnimState::COUNT)] = {-1, -1, -1, -1, -1, -1};
    int frames[static_cast<int>(AnimState::COUNT)] = {0, 0, 0, 0, 0, 0};
    int frame_ms[static_cast<int>(AnimState::COUNT)] = {80, 80, 80, 80, 80, 80};

    AnimState state = AnimState::Idle;
    // The state actually on the Sprite: `state` falls back to Idle's art when
    // a set has no graphic for it, and this is the slot whose art is shown.
    AnimState shown = AnimState::Idle;
    VillagerJob job = VillagerJob::None;   // applied job, for change detection
    bool female = false;
    // Forces the first Update to push the idle set onto the Sprite, so spawn
    // code does not have to duplicate that wiring.
    bool needs_apply = true;
};

} // namespace ECS::Components
