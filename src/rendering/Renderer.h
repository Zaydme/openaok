#pragma once
#include <SDL2/SDL.h>
#include <cstdint>
#include <string>
#include "ecs/Simulation.h"
#include "hal/Window.h"

namespace Rendering {

class Renderer {
public:
    bool Init(HAL::Window& window);
    // Draws the world and the interface into the window's frame; whatever
    // goes over them (a dialog) comes next, then Finish puts the cursor on top
    // and shows the frame.
    void Render(ECS::Simulation& sim, HAL::Window& window, float alpha);
    void Finish(ECS::Simulation& sim, HAL::Window& window);

    // Decodes the art of everything in the world before the first frame, so
    // none is decoded as it first comes into view.
    static void Warm(ECS::Simulation& sim);

    // Camera state
    float camera_x = 0.0f;
    float camera_y = 0.0f;
    float target_camera_x = 0.0f;
    float target_camera_y = 0.0f;
    float camera_zoom = 1.0f;
    float target_camera_zoom = 1.0f;
    float water_phase = 0.0f;
    
    // Dynamic viewport size (updated every frame from the window)
    float view_w = 1600.0f;
    float view_h = 900.0f;
    
    // FPS tracking; shown only with --fps.
    int current_fps = 0;
    bool show_fps = false;

    // What the last frame's drawing cost, in microseconds of this thread's
    // time, stage by stage (--perf). The GPU's own work is not in them: SDL
    // hands it the frame at Finish.
    struct Cost {
        int64_t terrain = 0;     // the water and the terrain
        int64_t ground = 0;      // what lies on the ground, sorted and drawn
        int64_t fog = 0;
        int64_t standing = 0;    // what stands over the fog
        int64_t interface = 0;   // the bars, the panel, the messages
        int64_t finish = 0;      // the cursor, and the frame handed over and shown
    };
    Cost cost;

    // Outline every blocked tile (--show-grid), to check the simulation's
    // ground against the art standing on it.
    bool show_walkability = false;

    // A message across the top of the view: a network game waiting for its
    // players, or stopped out of sync. Empty for none.
    std::string banner;

    // How far the view may zoom out and in, and the step a wheel notch or a
    // key press takes. The original has no zoom; it is ours.
    static constexpr float kZoomOut = 0.4f;
    static constexpr float kZoomIn = 1.25f;
    static constexpr float kZoomStep = 1.1f;

    // Zooms by `factor` (above 1 in, below out), keeping the frame point
    // (x, y) where it is, within kZoomOut..kZoomIn: eased there, or at once
    // for a pinch or a stick that zooms as it is held.
    void ZoomBy(float factor, float x, float y, bool at_once);
};

// Draws at `zoom` times over the whole of the target being drawn to: the
// scale, and a viewport that covers the target at it, which SDL 2 and SDL 3
// (sdl2-compat) both read so.
void SetZoom(SDL_Renderer* sdl_r, float zoom);

} // namespace Rendering
