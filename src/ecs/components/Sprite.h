#pragma once
#include <string>
#include "core/Types.h"

namespace ECS::Components {

// A drawable graphic. Sprites reference the game's art by SLP id and frame;
// the renderer resolves those through the atlas cache, which also supplies the
// hotspot that anchors the frame on its tile.
struct Sprite {
    // Which graphic to draw. -1 means the entity has no art yet and renders as
    // a flat placeholder rectangle using `color` and `width`/`height`.
    int slp_id = -1;
    int slp_frame = 0;

    // Companion shadow graphic, resolved once from the shadow map; shadows are
    // SLPs of their own. Setting shadow_resolved with shadow_slp still -1 opts
    // a sprite out, which is what building pieces do: their shadows are
    // explicit composition pieces.
    int shadow_slp = -1;
    bool shadow_resolved = false;

    // This sprite *is* a shadow: draw slp_id's shadow layer in the shadow pass
    // rather than its colour art.
    bool is_shadow = false;

    // Optional sub-rectangle of the frame to draw, for art holding several
    // separate pieces in one image -- a building's shadow graphic covers both
    // side tents with a gap between them, and each half is positioned on its
    // own. sub_w == 0 means the whole frame.
    int sub_x = 0;
    int sub_y = 0;
    int sub_w = 0;
    int sub_h = 0;

    // Placeholder size, and the size the animation code reasons about.
    int width = 32;
    int height = 32;

    // Animation state
    int current_frame = 0;
    // Holds its first frame rather than cycling: a body lying where it fell,
    // a missile in flight.
    bool still = false;
    int animation_speed = 100; // ms per frame
    int animation_timer = 0;

    Core::Direction facing = Core::Direction::South;

    // How far above its ground position the graphic is drawn, in screen
    // pixels: a missile in flight. Its ground track is what the simulation
    // reasons about; this is presentation.
    int lift_px = 0;

    // Draw order within a single tile. A building is several graphics stacked
    // back-to-front (back piece, body, centre, front piece) so units can walk
    // between them; depth alone cannot order pieces that share a tile.
    int sort_bias = 0;


    // Fallback colour for entities with no art yet.
    uint32_t color = 0xFFFFFFFF; // ARGB

    // A building under construction steps through this many stages as its
    // health rises, instead of animating on a timer.
    int construction_frames = 0;
    // The frame its stages start at: a wall's construction graphic keeps four
    // stages for each of its five pieces, and a site shows its own piece's.
    int construction_offset = 0;

    // Multi-frame graphics pick a frame from facing plus animation step; static
    // ones just use slp_frame as given.
    int frames_per_angle = 1;    // e.g. 15 for a villager's walk cycle
    bool is_file_sprite = false; // true = frame is chosen from facing/animation
    bool flip_h = false;         // true when facing NE/E/SE (mirrored)
};

} // namespace ECS::Components
