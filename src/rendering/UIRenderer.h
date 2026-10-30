#pragma once
#include <SDL2/SDL.h>
#include <string>
#include "core/Types.h"
#include "ecs/Simulation.h"

namespace Rendering {

class Renderer;

// The battlefield interface: the panel art for the screen size and
// civilisation, the stockpiles and the age, the buttons round the top bar and
// the mini-map, the command grid, the selected object or group, and the
// rollover help.
//
// Every screen rectangle the interface uses is produced by a function here, and
// the input code hit-tests against those same functions.
class UIRenderer {
public:
    // The interface's two parts, each drawn at its own size (the Options' Top
    // bar size and Panel size): the top bar, and the bottom panel with the
    // mini-map. A part is laid out for a frame
    // `w` wide, narrower than the real one (rendering/HudLayout.h carries it
    // out to that width's edges), and scaled up to the real one from the
    // frame's top-left corner, or for the panel its bottom-left. At 100%, or
    // in a frame with no room to spare, a part is the frame itself.
    //
    // What takes a width and a point -- BarButtonAt, MinimapButtonAt,
    // MinimapRect, PointOverHud, the rects BuildCommands and its kind hand
    // back -- answers in a part's terms: map the point into it first.
    struct HudSpace {
        int w = 0;
        int h = 0;
        float scale = 1.0f;
        bool bottom = false;   // scaled from the frame's bottom edge
        // A point of the frame, in the part's terms.
        void ToSpace(int fx, int fy, int& x, int& y) const;
        // A row of the part's, in the frame's.
        float FrameY(float y) const { return bottom ? h - (h - y) * scale : y * scale; }
    };
    static HudSpace TopSpace(int win_w, int win_h);
    static HudSpace PanelSpace(int win_w, int win_h);
    // Whether a point of the frame is on either part of the interface.
    static bool OverInterface(int win_w, int win_h, int x, int y);
    // The Options' Text size, in percent: the text over the world.
    static int TextPercent();

    // The whole interface over the world: the top bar, the panel with the
    // mini-map, and the rollover help, each at its size.
    static void DrawInterface(ECS::Simulation& sim, Renderer& renderer, SDL_Renderer* sdl_r,
                              int win_w, int win_h);
    static void DrawMinimap(ECS::Simulation& sim, Renderer& renderer,
                            SDL_Renderer* sdl_r, int win_w, int win_h);
    // The mini-map's picture alone -- ground, fog, contents, flares and the
    // frame of the view -- in `well`, for the view that shows `view` (screen
    // pixels) of the world from the renderer's camera. The battlefield puts
    // its buttons round it; the scenario editor draws it in its own corner.
    static void DrawMinimapIn(ECS::Simulation& sim, Renderer& renderer, SDL_Renderer* sdl_r,
                              const SDL_FRect& well, const SDL_FRect& view);
    // The mouse pointer: which cursor depends on what the selection would do
    // to what is under it.
    static void DrawCursor(ECS::Simulation& sim, Renderer& renderer, SDL_Renderer* sdl_r);
    static void DrawFPS(SDL_Renderer* sdl_r, int fps, int win_w);
    // The rollover help in the lower-left corner of the view: what the button
    // under the cursor does, or what a right-click would do in the world.
    static void DrawHelpText(ECS::Simulation& sim, SDL_Renderer* sdl_r, int win_w, int win_h);

    // True when the point is over panel art rather than the world, so a click
    // there does not also reach into the map behind it. Decided by the art's
    // own pixels: the world shows through wherever the frame is transparent.
    static bool PointOverHud(int win_w, int win_h, int x, int y);

    // Where the interface art reserves the minimap: the diamond's bounding box.
    static SDL_FRect MinimapRect(int win_w, int win_h);

    // The single projection between world tiles and the minimap, shared by the
    // drawing and the hit test.
    static SDL_FPoint MinimapProject(const SDL_FRect& well,
                                     float wx, float wy, int grid_w, int grid_h);
    // Inverse. False when the point is outside the map diamond.
    static bool MinimapUnproject(const SDL_FRect& well, float px, float py,
                                 int grid_w, int grid_h, float& wx, float& wy);

    // One button of the command grid. What a button offers, which slot it sits
    // in and what it costs come from the game data; only the list of buildings
    // that can be placed is the engine's own.
    enum class CommandKind {
        Produce,      // build a building or train a unit
        Research,     // research a technology
        SwitchPage,   // move between the economic and military build menus
        Delete,       // delete the selected building or unit
        Stance,       // change how the selected units fight
        Formation,    // choose the formation the selected group marches in
        Stop,         // stop what the selected units are doing
        Ungarrison,   // turn everyone out of the selected building
        TownBell,     // ring the Town Center's bell, or call everyone back
        Patrol,       // the selected units patrol to a point, chosen next
        Guard,        // the selected units guard a unit, chosen next
        Follow,       // the selected units follow a unit, chosen next
        Garrison,     // the selected units garrison in a building, chosen next
        Repair,       // the selected villagers repair a building, chosen next
        Sell,         // sell 100 of a commodity at the selected Market
        Buy,          // buy 100 of a commodity there
        LockGate,     // lock the selected gate (choice 1) or unlock it (0)
        Pack,         // pack the selected trebuchets (choice 1) or unpack them (0)
        Treason,      // Treason at the selected Castle, in a Regicide game
        Convert,      // the selected Monks convert a unit or building, chosen next
        Heal,         // the selected Monks heal a unit, chosen next
        Unload,       // the selected transport unloads where the player points next
        GatherPoint,  // the selected building's gather point, chosen next
        RemoveGatherPoint,   // the selected building's gather point taken away
        CancelOrder,  // the order waiting for its target, or the building being placed, given up
        BackToWork,   // the villagers inside the selected building sent back to work
        Caption,      // words in a slot, not a button: the Market's "Sell:"
    };
    struct Command {
        CommandKind kind = CommandKind::Produce;
        // Its place in the grid, 1 to 15 along the rows.
        int slot = -1;
        int unit_id = -1;
        // For a Produce button: whether it places a building, from a
        // villager's build menu, rather than training a unit.
        bool places_building = false;
        // For a Research button: the dat tech id.
        int tech_id = -1;
        // For a Stance or Formation button: which one. For a Sell or Buy
        // button: the dat resource.
        int choice = -1;
        // A number the button shows on its face -- a Market button's price --
        // or -1.
        int label = -1;
        // For a Caption: the LANGUAGE.DLL string it shows in place of a
        // picture.
        int caption = -1;
        // The sheet and frame to draw. For a produced unit these come from its
        // icon; for a page switch, from the command icons.
        uint32_t icon_slp = 0;
        int icon_frame = -1;
        // For a page switch, the page it moves to.
        int target_page = 0;
        // The string the hotkey help names it by, or -1.
        int name_string = -1;
        // The hotkey that presses it: an item of one of the hotkey groups
        // (Core::Hotkeys), or -1 for a button no key reaches.
        int hotkey_group = -1;
        int hotkey_item = -1;
        SDL_FRect rect{0.0f, 0.0f, 0.0f, 0.0f};
        bool affordable = false;
        bool enabled = false;
    };
    static constexpr int kCommandSlots = 15;   // a 5x3 grid

    // What clicking a button asks the world to do. Page switches are the
    // interface's own business and never reach here.
    enum class Action { None, PlaceBuilding, DeleteBuilding, TrainUnit, Research,
                        SetStance, Stop, Ungarrison, TownBell, Patrol, Guard, Follow,
                        Garrison, Repair, Sell, Buy, LockGate, Pack, Treason, Convert,
                        Heal, Unload, GatherPoint, RemoveGatherPoint, CancelOrder,
                        BackToWork };

    // The formation the player last chose; a group move marches in it.
    static Core::Formation CurrentFormation();

    // Acts on a button the user clicked, handling anything internal to the
    // interface and reporting the rest back.
    static Action ActivateCommand(const Command& command);

    // Opens a villager's build menu for the current selection, as clicking
    // its button would: kInterfaceEconomic or kInterfaceMilitary.
    static void OpenBuildMenu(ECS::Simulation& sim, int page);

    // The building the panel is describing, or null.
    static entt::entity SelectedBuilding(ECS::Simulation& sim);

    // Why a train, research or build button cannot be used now, as a
    // LANGUAGE.DLL string id ("Not enough wood.", "You need to build more
    // houses."), or -1 when it can.
    static int Refusal(ECS::Simulation& sim, const Command& command);
    static constexpr int kNeedHouses = 3005;

    // A player's name as the interface writes it: the lobby's, or a computer
    // player's leader name from LANGUAGE.DLL.
    static std::string PlayerName(const entt::registry& registry, Core::PlayerId id);

    // Fills `out` with the commands the current selection offers, and returns
    // how many. Drawing and clicking both go through this, so a button is
    // always where it looks like it is.
    static int BuildCommands(ECS::Simulation& sim, int win_w, int win_h,
                             Command* out, int max_out);

    // One item in the selected building's queue, as drawn: units of one kind
    // queued one after another share a button, with their count on it.
    struct QueueSlot {
        int slot = 0;          // index into the building's queue of the last one
        int id = -1;           // dat unit id, or tech id when `tech`
        int count = 1;
        bool tech = false;
        SDL_FRect rect{0.0f, 0.0f, 0.0f, 0.0f};
    };
    static constexpr int kMaxQueueSlots = 15;

    // Fills `out` with the selected building's queue and returns how many.
    // Drawing and clicking both go through this -- clicking a queued unit
    // cancels it -- so an icon is always where it looks like it is.
    static int BuildQueueSlots(ECS::Simulation& sim, int win_w, int win_h,
                               QueueSlot* out, int max_out);

    // One card of a selected group, and the unit it stands for.
    struct SelectionCard {
        entt::entity unit = entt::null;
        SDL_Rect rect{0, 0, 0, 0};
    };
    static constexpr int kMaxSelectionCards = 60;
    // The cards a group selection shows, for drawing and for clicking.
    static int BuildSelectionCards(ECS::Simulation& sim, int win_w, int win_h,
                                   SelectionCard* out, int max_out);

    // The top bar's buttons and the mini-map's: which one is under a point,
    // or -1. The mini-map's are only those currently shown.
    static int BarButtonAt(int win_w, int x, int y);
    static int MinimapButtonAt(int win_w, int x, int y);
    // A mini-map button was clicked: toggles, modes. Returns true when it
    // was the idle villager button, whose work belongs to input.
    static bool ActivateMinimapButton(int button);

    // Advanced commands: the military tasks, the stances, the formations and
    // the mini-map's modes stay hidden until they are turned on.
    static bool AdvancedCommands();
    // Whether the game's Help is on: the rollover help, and the
    // notifications' lines (rendering/Alerts).
    static bool HelpShown();
    // The mini-map's mode: 0 normal, 1 combat, 2 economic.
    static int MinimapMode();

    // A line of help for the lower-left corner, set by whoever knows what the
    // cursor is over in the world; cleared every frame.
    static void SetWorldHint(int string_id);

    // A top bar button was clicked: the screen it opens is the screens'
    // business, which collects the request here. -1 when there is none.
    static void RequestDialog(int bar_button);
    static int TakeDialogRequest();

    // A new game is starting: nothing of the last one's interface carries
    // over -- its mini-map, its command page.
    static void NewGame();

    // The map view: between the top bar and the panel. The time panels and
    // the scores are placed over it.
    static SDL_Rect ViewRect(int win_w, int win_h);
    // The game clock (F11) and the Wonder and relic countdowns: the clock a
    // 300x20 box 4 pixels into the view's top-left, "HH:MM:SS (speed)"; each
    // countdown a 170x20 box 4 pixels in from the view's right, 24 down and 20
    // apart, right-aligned in its holder's colour ("Wonder: %d Years").
    static void ToggleClock();
    static void SetGameSpeed(int percent);
    static void DrawTimePanels(ECS::Simulation& sim, SDL_Renderer* sdl_r, int win_w, int win_h);
    // The scores (F4, or the mini-map's statistics button) at the view's
    // lower right.
    static void DrawScores(ECS::Simulation& sim, SDL_Renderer* sdl_r, int win_w, int win_h);
    // A player's colour for text: a pure colour for each player colour, not
    // the sprites' ramp.
    static SDL_Color PlayerTextColour(Core::PlayerId player);
};

} // namespace Rendering
