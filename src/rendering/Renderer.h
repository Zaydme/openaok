#pragma once
#include <SDL2/SDL.h>
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

    // Outline every blocked tile (--show-grid), to check the simulation's
    // ground against the art standing on it.
    bool show_walkability = false;

    // A message across the top of the view: a network game waiting for its
    // players, or stopped out of sync. Empty for none.
    std::string banner;
};

} // namespace Rendering
