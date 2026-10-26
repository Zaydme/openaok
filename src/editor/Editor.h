#pragma once
#include <SDL2/SDL.h>
#include <string>

#include "core/GameSetup.h"

namespace Rendering {
class Renderer;
}

// The scenario editor screen, drawn in the scr5 screen style (interfac.drs
// 50055: SCR5B tiled, the 50500 palette, the btn5 button art).
//
// The editor edits a scenario file (scenario/Scenario.h) and shows it as a
// world built the way a scenario game builds one (ecs/ScenarioWorld), which it
// never runs: nothing here calls the simulation's tick. What it saves is the
// file, written by scenario/Writer.
namespace Editor {

// What the editor asks the application to do.
struct Request {
    enum class Kind {
        None,
        Exit,   // back to the main menu
        Test,   // play the scenario now; `setup` is its game
        Quit,   // the window was closed
    };
    Kind kind = Kind::None;
    Core::GameSetup setup;
};

// Opens the editor on a new scenario (`path` empty) or on the scenario at
// `path`, at the screen size given. False when the file cannot be read.
bool Open(const std::string& path, int screen_w, int screen_h, std::string* error = nullptr);
// Back from a test game: the same scenario, as it was left.
void Resume(int screen_w, int screen_h);
bool IsOpen();

// One input event, in the screen's pixels.
void HandleEvent(const SDL_Event& event);
// Edge scrolling and held keys, once a frame.
void Update(float dt);
// The whole screen: the world in the map view, the editor around it.
void Draw(SDL_Renderer* r, Rendering::Renderer& renderer);
Request TakeRequest();

// Which panel shows, 1 Map ... 10 Triggers, and the view's centre tile.
void ShowTab(int tab);
void LookAt(int x, int y);
// The list of scenarios to open, as the main menu's Edit Scenario shows it.
void ShowOpenList();

// Where scenarios are kept: assets/original/Scenario, beside the saved
// games' folder. Ends in a separator.
std::string ScenarioDir();

} // namespace Editor
