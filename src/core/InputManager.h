#pragma once
#include <SDL2/SDL.h>
#include <algorithm>
#include <cstdlib>
#include "ecs/Simulation.h"
#include "rendering/UIRenderer.h"

namespace Rendering { class Renderer; }

namespace Core {

class InputManager {
public:
    static InputManager& Get() {
        static InputManager instance;
        return instance;
    }

    void ProcessEvent(const SDL_Event& event, ECS::Simulation& sim, Rendering::Renderer& renderer);

    // The box-select rectangle in window pixels, or false when not dragging.
    // In window pixels so it draws unscaled at any zoom.
    bool GetSelectionRect(SDL_Rect& out) const {
        if (!is_dragging) return false;
        out.x = std::min(drag_screen_x, current_mouse_x);
        out.y = std::min(drag_screen_y, current_mouse_y);
        out.w = std::abs(current_mouse_x - drag_screen_x);
        out.h = std::abs(current_mouse_y - drag_screen_y);
        return out.w > 2 && out.h > 2;
    }
    void Update(Rendering::Renderer& renderer, float dt_seconds, int win_w, int win_h);
    // Once a frame with the world at hand: the cursor and the help.
    void Hover(ECS::Simulation& sim, Rendering::Renderer& renderer) { UpdateIntent(sim, renderer); }

    // The building waiting to be placed, so the renderer can show where it
    // would go. False when nothing is being placed.
    bool PlacingBuilding(int& unit_id) const {
        if (!placing_building) return false;
        unit_id = building_to_place;
        return true;
    }

    // A wall being dragged out from the build menu: the tile the drag began
    // on. False when no wall is being dragged.
    bool WallDrag(int& from_x, int& from_y) const {
        if (!wall_drag) return false;
        from_x = wall_from_x;
        from_y = wall_from_y;
        return true;
    }

    // What an order would do to what is under the cursor, which picks the
    // cursor: a sword over an enemy, an axe over a tree.
    enum class Intent : uint8_t {
        None,
        Attack,     // strike it
        Work,       // a villager's or a fishing ship's job: gather, build, mend, drop off
        Monk,       // heal it, convert it, pick up a relic
        Garrison,   // go inside
        Board,      // board a transport
        Place,      // a building being placed; the Repair button's target
        Patrol, Follow, Guard,
        Signal,     // a signal to the allies
        Unload,     // where a transport puts its load ashore
        GatherPoint,   // a building's gather point
    };
    Intent CursorIntent() const { return intent; }
    // Whether the player is choosing a building's gather point: the panel
    // then offers only to remove it, or to give up.
    bool ChoosingGatherPoint() const { return pending_order == PendingOrder::GatherPoint; }

    // The mouse, in the game's own pixels.
    int MouseX() const { return current_mouse_x; }
    int MouseY() const { return current_mouse_y; }

private:
    InputManager() = default;
    ~InputManager() = default;

    // Centre the camera on the world position the given window pixel maps to
    // inside the minimap well. Used by the click and the drag that follows it,
    // so holding the button scrubs the view.
    void MinimapJump(Rendering::Renderer& renderer, ECS::Simulation& sim, int mx, int my);

    bool is_dragging = false;
    bool minimap_dragging = false;
    int drag_start_x = 0;
    int drag_start_y = 0;
    int drag_screen_x = 0;   // same drag, in unscaled window pixels
    int drag_screen_y = 0;
    
    // -1 until the first mouse event: (0,0) sits inside the edge-scroll
    // margin.
    int current_mouse_x = -1;
    int current_mouse_y = -1;
    // Edge scrolling only applies while the cursor is over the window.
    bool mouse_in_window = false;

    bool pan_left = false;
    bool pan_right = false;
    bool pan_up = false;
    bool pan_down = false;
    // The zoom key held, its repeats zooming on; none when the key pressed is
    // a hotkey's.
    SDL_Keycode zoom_key = SDLK_UNKNOWN;

    // Building placement: the build button's dat unit id.
    bool placing_building = false;
    int building_to_place = -1;
    // A wall is placed by a drag: pressed on its first tile, released on its
    // last, and laid along the line between by one order.
    bool wall_drag = false;
    int wall_from_x = 0;
    int wall_from_y = 0;

    // A standing order waiting for its target: the next click in the world
    // names the point to patrol to, or the unit to guard or follow.
    enum class PendingOrder : uint8_t { None, Patrol, Guard, Follow, Garrison, Repair, Flare,
                                        Convert, Heal, Unload, GatherPoint };
    PendingOrder pending_order = PendingOrder::None;
    // Gives the pending order at world point (x, y) to the player's selected
    // units, and clears it.
    void GivePendingOrder(ECS::Simulation& sim, float x, float y);
    
    int last_win_w = 1024;
    int last_win_h = 768;

    // What is under the cursor, worked out each frame for the cursor and the
    // rollover help.
    Intent intent = Intent::None;
    void UpdateIntent(ECS::Simulation& sim, Rendering::Renderer& renderer);
    // Where the idle villager button last left off, so each press finds the
    // next one.
    entt::entity last_idle = entt::null;
    void NextIdleVillager(ECS::Simulation& sim, Rendering::Renderer& renderer);

    // Does what pressing a command button does. `times` is how many a
    // training button queues: five with Shift held.
    void RunCommand(ECS::Simulation& sim, const Rendering::UIRenderer::Command& cmd, int times);
    // A bound key: a command button's, a control group's, or one of the game
    // commands the world view answers. True when it was taken.
    bool HandleHotkey(const SDL_KeyboardEvent& key, ECS::Simulation& sim,
                      Rendering::Renderer& renderer);
    // A mouse button or wheel turn bound as a key (Core::Hotkeys::MouseKey).
    bool HandleMouseKey(int mouse_key, ECS::Simulation& sim, Rendering::Renderer& renderer);
    // The game commands both answer.
    bool GameCommand(int item, ECS::Simulation& sim, Rendering::Renderer& renderer);
    // Control groups: Ctrl and a digit makes one of the selection, the digit
    // selects it, and Shift with the digit adds it to the selection.
    void AssignGroup(ECS::Simulation& sim, int group);
    void SelectGroup(ECS::Simulation& sim, int group, bool add, Rendering::Renderer& renderer);
    int last_group = 0;
    uint32_t last_group_at = 0;
    static constexpr uint32_t kDoubleTapMs = 500;
    // The most the player may select at once.
    static constexpr int kMaxSelection = 40;
    // Centres the view on a world position.
    void LookAt(Rendering::Renderer& renderer, float wx, float wy);
    // Selects and looks at the next of the player's buildings of a kind, by
    // its dat name, round from the last one each press.
    void GoToBuilding(ECS::Simulation& sim, Rendering::Renderer& renderer, const char* name);
    entt::entity last_visited = entt::null;
    // The next idle military unit, as the idle villager button finds villagers.
    void NextIdleMilitary(ECS::Simulation& sim, Rendering::Renderer& renderer);
    entt::entity last_idle_military = entt::null;
};

} // namespace Core
