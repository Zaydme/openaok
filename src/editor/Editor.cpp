#include "editor/Editor.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>

#include "audio/InterfaceSounds.h"
#include "core/ConfigManager.h"
#include "core/IsometricMath.h"
#include "ecs/Buildings.h"
#include "core/Strings.h"
#include "editor/EditorState.h"
#include "hal/Audio.h"
#include "hal/GraphicCache.h"
#include "hal/Pad.h"
#include "hal/TextInput.h"
#include "hal/Touch.h"
#include "hal/Window.h"
#include "rendering/EntityRenderer.h"
#include "rendering/FogRenderer.h"
#include "rendering/Renderer.h"
#include "rendering/Ground.h"
#include "rendering/TerrainRenderer.h"
#include "rendering/Text.h"
#include "rendering/UIRenderer.h"
#include "ecs/systems/AnimationSystem.h"
#include "screens/Screens.h"

namespace Editor {

namespace Text = Rendering::Text;
using Core::Strings::Get;
using Screens::Hit;

namespace {

// The editor's strings (LANGUAGE.DLL): the tabs in tab order, and the Menu
// button.
constexpr int kTabStrings[kTabCount] = {10010, 10011, 10012, 10013, 10018,
                                        10019, 10017, 10015, 10016, 10021};
constexpr int kMenuString = 10020;
// Each tab's help string, shown at the bottom of the map view under the
// pointer.
constexpr int kTabHelp[kTabCount] = {30500, 30501, 30502, 30503, 30508,
                                     30509, 30507, 30505, 30506, 30510};
// The help line: font 11, 4 in from the map view's sides and bottom, 50
// high.
constexpr int kHelpFont = 11;
constexpr int kHelpString = 4009;     // "?"

// The game font all the editor's controls use.
constexpr int kEditorFont = 10;

// How fast the view scrolls under the arrow keys or at the screen's edge,
// pixels a second.
constexpr float kScrollSpeed = 900.0f;
// How fast the right stick zooms pushed all the way, as a factor a second.
constexpr float kStickZoomPerSecond = 2.5f;
constexpr int kEdge = 2;

void Click() { HAL::Audio::Play(Audio::InterfaceSound::kButton2, 1.0f, 0.0f); }

// The menu: a 460 by 400 dialog centred on the screen, its buttons 400 by
// 40, 30 in.
struct MenuItem {
    int string;
    int y;
};
constexpr MenuItem kMenuItems[] = {
    {9283, 20},    // Quit to Main Menu
    {9281, 70},    // Save
    {9282, 120},   // Save As
    {9285, 170},   // Edit Scenario
    {9262, 220},   // Create Scenario
    {9286, 270},   // Test
    {9284, 340},   // Cancel
};
constexpr int kDialogW = 460;
constexpr int kDialogH = 400;

SDL_Rect DialogRect(const State& s) {
    return SDL_Rect{(s.w - kDialogW) / 2, (s.h - kDialogH) / 2, kDialogW, kDialogH};
}

void ClampCamera(State& s) {
    auto& grid = s.sim->GetGrid();
    const auto gw = Core::Fixed32(grid.GetWidth());
    const auto gh = Core::Fixed32(grid.GetHeight());
    const auto left = Core::IsometricMath::WorldToScreen(Core::Fixed32(0), gh);
    const auto right = Core::IsometricMath::WorldToScreen(gw, Core::Fixed32(0));
    const auto top = Core::IsometricMath::WorldToScreen(Core::Fixed32(0), Core::Fixed32(0));
    const auto bottom = Core::IsometricMath::WorldToScreen(gw, gh);
    const SDL_Rect& v = s.frame.view;
    auto& c = s.view;
    const float cx = (v.x + v.w / 2.0f) / c.camera_zoom;
    const float cy = (v.y + v.h / 2.0f) / c.camera_zoom;
    c.camera_x = std::clamp(c.camera_x, left.x - cx, right.x - cx);
    c.camera_y = std::clamp(c.camera_y, top.y - cy, bottom.y - cy);
    c.target_camera_x = c.camera_x;
    c.target_camera_y = c.camera_y;
}

void DrawWorld(State& s, SDL_Renderer* r) {
    auto& v = s.view;
    v.view_w = static_cast<float>(s.w);
    v.view_h = static_cast<float>(s.h);
    ClampCamera(s);
    v.target_camera_zoom = v.camera_zoom;
    // The whole screen's worth, at the view's zoom; the editor's frame goes
    // over all but the view.
    Rendering::SetZoom(r, v.camera_zoom);
    Rendering::TerrainRenderer::DrawWaterLayer(*s.sim, v, r);
    Rendering::TerrainRenderer::DrawTerrain(*s.sim, v, r);
    Rendering::EntityRenderer::DrawGroundLevel(*s.sim, v, r, 1.0f);
    Rendering::FogRenderer::Draw(*s.sim, v, r);
    Rendering::EntityRenderer::DrawStanding(v, r);
    Rendering::SetZoom(r, 1.0f);
}

// A point of the world on the screen, at the view's zoom.
SDL_FPoint OnScreen(const State& s, const SDL_FPoint& p) {
    return SDL_FPoint{(p.x - s.view.camera_x) * s.view.camera_zoom, (p.y - s.view.camera_y) * s.view.camera_zoom};
}

// The file's tile under a point of the world.
std::pair<int, int> FileTileAt(const State& s, float wx, float wy) {
    float fx = 0, fy = 0;
    FileOf(s, wx, wy, fx, fy);
    return {static_cast<int>(std::floor(fx)), static_cast<int>(std::floor(fy))};
}

// The brush's tiles outlined under the cursor (Terrain tab), or the tile the
// next thing goes on (Units tab).
void DrawBrush(State& s, SDL_Renderer* r) {
    if (s.dialog != State::Dialog::None) return;
    float wx = 0, wy = 0;
    if (!WorldAt(s, s.ui.mx, s.ui.my, wx, wy)) return;
    int size = 0;
    if (s.tab == kTerrainTab && (s.brush == kBrushTerrain || s.brush == kBrushElevation || s.brush == kBrushErase)) {
        size = s.brush_size * 2 - 1;
    } else {
        return;
    }
    const int tx = static_cast<int>(std::floor(wx + 0.5f));
    const int ty = static_cast<int>(std::floor(wy + 0.5f));
    const float half = size / 2.0f;
    auto at = [&](float x, float y) {
        const auto p = Core::IsometricMath::WorldToScreen(Core::Fixed32(x), Core::Fixed32(y));
        return OnScreen(s, p);
    };
    const float x0 = tx - half, y0 = ty - half, x1 = tx + half, y1 = ty + half;
    const SDL_FPoint q[5] = {at(x0, y0), at(x1, y0), at(x1, y1), at(x0, y1), at(x0, y0)};
    SDL_RenderSetClipRect(r, &s.frame.view);
    SDL_SetRenderDrawColor(r, 255, 255, 255, 255);
    SDL_RenderDrawLinesF(r, q, 5);
    SDL_RenderSetClipRect(r, nullptr);
}

// Map Copy: the area chosen outlined (Copy Mode), or where the copy would go
// under the pointer (Paint Mode).
void DrawCopy(State& s, SDL_Renderer* r) {
    if (s.dialog != State::Dialog::None || s.tab != kTerrainTab || s.brush != kBrushCopy) return;
    int x0 = 0, y0 = 0, x1 = -1, y1 = -1;
    if (s.copy_paint) {
        float wx = 0, wy = 0;
        if (!WorldAt(s, s.ui.mx, s.ui.my, wx, wy) || s.copied.w <= 0) return;
        const auto [fx, fy] = FileTileAt(s, wx, wy);
        x0 = fx - s.copied.w / 2;
        y0 = fy - s.copied.h / 2;
        x1 = x0 + s.copied.w - 1;
        y1 = y0 + s.copied.h - 1;
    } else if (s.has_area) {
        x0 = std::min(s.area_x0, s.area_x1);
        x1 = std::max(s.area_x0, s.area_x1);
        y0 = std::min(s.area_y0, s.area_y1);
        y1 = std::max(s.area_y0, s.area_y1);
    } else {
        return;
    }
    // The file's corners, as points of the world.
    auto at = [&](int fx, int fy) {
        const auto w = WorldOf(s, Core::Fixed32(fx), Core::Fixed32(fy));
        const auto p = Core::IsometricMath::WorldToScreen(w.x, w.y);
        return OnScreen(s, p);
    };
    const SDL_FPoint q[5] = {at(x0, y0), at(x1 + 1, y0), at(x1 + 1, y1 + 1), at(x0, y1 + 1), at(x0, y0)};
    SDL_RenderSetClipRect(r, &s.frame.view);
    SDL_SetRenderDrawColor(r, 255, 255, 255, 255);
    SDL_RenderDrawLinesF(r, q, 5);
    SDL_RenderSetClipRect(r, nullptr);
}

// What Place will put down, under the pointer: a building's ground outlined,
// a unit or an object as it will stand, translucent.
void DrawPlacing(State& s, SDL_Renderer* r) {
    if (s.dialog != State::Dialog::None || s.tab != kUnitsTab || s.tool != kPlace) return;
    float wx = 0, wy = 0;
    Core::Fixed32 fx, fy;
    ECS::Components::Vector2D world;
    int footprint = 0;
    if (!WorldAt(s, s.ui.mx, s.ui.my, wx, wy) || !PlaceAt(s, wx, wy, fx, fy, world, footprint)) return;
    const int unit_id = s.unit_ids[static_cast<size_t>(s.unit_choice - 1)];
    const Core::Fixed32 ex = world.x, ey = world.y;
    auto at = [&](float x, float y) {
        const auto p = Core::IsometricMath::WorldToScreen(Core::Fixed32(x), Core::Fixed32(y));
        return OnScreen(s, p);
    };
    SDL_RenderSetClipRect(r, &s.frame.view);
    if (footprint > 0) {
        const auto b = ECS::Buildings::FootprintBounds(
            {Core::Fixed32(Physics::TileOf(ex)), Core::Fixed32(Physics::TileOf(ey))}, unit_id);
        const float x0 = b.min_x.ToFloat(), y0 = b.min_y.ToFloat(), x1 = b.max_x.ToFloat(), y1 = b.max_y.ToFloat();
        const SDL_FPoint q[5] = {at(x0, y0), at(x1, y0), at(x1, y1), at(x0, y1), at(x0, y0)};
        SDL_SetRenderDrawColor(r, 255, 255, 255, 255);
        SDL_RenderDrawLinesF(r, q, 5);
    } else {
        const auto art = Core::ConfigManager::Get().GetUnitGraphicById(unit_id, "idle_graphic0");
        auto& graphics = HAL::GraphicCache::Get();
        const HAL::GraphicAtlas* atlas = art.Valid() ? graphics.GetAtlas(static_cast<uint32_t>(art.slp_id)) : nullptr;
        if (atlas != nullptr && atlas->base != nullptr && !atlas->frames.empty()) {
            const HAL::GraphicFrame& f = atlas->frames[0];
            const SDL_FPoint p = at(ex.ToFloat(), ey.ToFloat());
            const float z = s.view.camera_zoom;
            const SDL_FRect dst = {p.x - f.hotspot_x * z, p.y - f.hotspot_y * z, f.rect.w * z, f.rect.h * z};
            for (SDL_Texture* layer : {atlas->base, graphics.GetPlayerLayer(static_cast<uint32_t>(art.slp_id), s.unit_player)}) {
                if (layer == nullptr) continue;
                SDL_SetTextureAlphaMod(layer, 160);
                SDL_RenderCopyF(r, layer, &f.rect, &dst);
                SDL_SetTextureAlphaMod(layer, 255);
            }
        }
    }
    SDL_RenderSetClipRect(r, nullptr);
}

void DrawChrome(State& s, SDL_Renderer* r) {
    const Frame& f = s.frame;
    const SDL_Rect& v = f.view;
    // The screen's background everywhere but the map view.
    const SDL_Rect around[4] = {
        {0, 0, s.w, v.y},
        {0, v.y, v.x, v.h},
        {v.x + v.w, v.y, s.w - (v.x + v.w), v.h},
        {0, v.y + v.h, s.w, s.h - (v.y + v.h)},
    };
    for (const SDL_Rect& a : around) {
        if (a.w > 0 && a.h > 0) Screens::DrawBackgroundIn(r, s.theme, a);
    }
}

// What a choice that leaves the scenario does, once any changes are saved
// or let go.
void Perform(State& s, State::After after) {
    s.after = State::After::None;
    s.dialog = State::Dialog::None;
    switch (after) {
    case State::After::Exit:
        s.request.kind = Request::Kind::Exit;
        break;
    case State::After::New:
        s.doc = NewFile();
        s.path.clear();
        s.dirty = false;
        BuildWorld(s, false);
        LoadFields(s);
        break;
    case State::After::Open:
        ShowOpenList();
        break;
    default:
        break;
    }
}

// "Do you want to save your changes to this scenario?" (9287) before a
// choice that leaves it, when it has changed.
void Leave(State& s, State::After after) {
    if (!s.dirty) {
        Perform(s, after);
        return;
    }
    s.after = after;
    s.dialog = State::Dialog::Confirm;
}

void DrawConfirm(State& s, Ui& ui) {
    const SDL_Rect d = {(s.w - 460) / 2, (s.h - 160) / 2, 460, 160};
    if (Drawing(ui)) {
        Screens::DrawBackgroundIn(ui.r, s.theme, d);
        Screens::DrawBevel(ui.r, s.theme, d, false);
        Screens::DrawWrapped(ui.r, ui.font, d.x + 20, d.y + 20, d.w - 40, Get(9287), s.theme.text,
                             s.theme.text_shadow);
    }
    if (Button(ui, SDL_Rect{d.x + 20, d.y + 100, 130, 40}, Get(4003))) {
        Click();
        if (s.path.empty()) {
            // Nowhere yet: Save As first, then the choice.
            s.dialog = State::Dialog::SaveAs;
            s.save_name.clear();
            ui.focus = &s.save_name;
        } else if (SaveTo(s, s.path)) {
            Perform(s, s.after);
        }
    }
    if (Button(ui, SDL_Rect{d.x + 165, d.y + 100, 130, 40}, Get(4004))) {
        Click();
        Perform(s, s.after);
    }
    if (Button(ui, SDL_Rect{d.x + 310, d.y + 100, 130, 40}, Get(4002))) {
        Click();
        s.after = State::After::None;
        s.dialog = State::Dialog::None;
    }
}

void DrawMenuDialog(State& s, Ui& ui) {
    const SDL_Rect d = DialogRect(s);
    if (Drawing(ui)) {
        Screens::DrawBackgroundIn(ui.r, s.theme, d);
        Screens::DrawBevel(ui.r, s.theme, d, false);
    }
    for (const MenuItem& item : kMenuItems) {
        const SDL_Rect b = {d.x + 30, d.y + item.y, 400, 40};
        const bool enabled = item.string != 9281 || !s.path.empty();
        if (Button(ui, b, Get(item.string), enabled)) {
            Click();
            switch (item.string) {
            case 9283:   // Quit to Main Menu
                Leave(s, State::After::Exit);
                break;
            case 9281:   // Save
                s.dialog = State::Dialog::None;
                if (SaveTo(s, s.path)) Notice(s, Get(1104));
                break;
            case 9282:   // Save As
                s.dialog = State::Dialog::SaveAs;
                s.save_name = std::filesystem::path(s.path).stem().string();
                ui.focus = &s.save_name;
                break;
            case 9285:   // Edit Scenario: the scenarios to open
                Leave(s, State::After::Open);
                break;
            case 9262:   // Create Scenario
                Leave(s, State::After::New);
                break;
            case 9286: {  // Test
                s.dialog = State::Dialog::None;
                const std::string test = ScenarioDir() + "editor-test.scn";
                if (SaveTo(s, test, false)) {
                    Core::GameSetup setup = Core::DefaultSetup();
                    std::string why;
                    if (Scenario::SetupFor(test, -1, setup, &why)) {
                        setup.difficulty = s.test_difficulty;
                        s.request.kind = Request::Kind::Test;
                        s.request.setup = setup;
                    } else {
                        Notice(s, why);
                    }
                }
                break;
            }
            default:     // Cancel
                s.dialog = State::Dialog::None;
                break;
            }
        }
    }
}

void DrawSaveAs(State& s, Ui& ui) {
    const SDL_Rect d = {(s.w - 460) / 2, (s.h - 160) / 2, 460, 160};
    if (Drawing(ui)) {
        Screens::DrawBackgroundIn(ui.r, s.theme, d);
        Screens::DrawBevel(ui.r, s.theme, d, false);
    }
    Label(ui, SDL_Rect{d.x + 20, d.y + 16, 420, 22}, Get(9412));
    Field(ui, SDL_Rect{d.x + 20, d.y + 46, 420, 22}, s.save_name, 64);
    const bool ok = Button(ui, SDL_Rect{d.x + 20, d.y + 100, 200, 40}, Get(4001));
    const bool enter = !Drawing(ui) && ui.event != nullptr && ui.event->type == SDL_KEYDOWN &&
                       (ui.event->key.keysym.sym == SDLK_RETURN || ui.event->key.keysym.sym == SDLK_KP_ENTER);
    if (ok || enter) {
        if (s.save_name.empty()) {
            Notice(s, Get(9415));
        } else {
            Click();
            const bool saved = SaveTo(s, ScenarioDir() + s.save_name + ".scn");
            if (saved) Notice(s, Get(1104));
            s.dialog = State::Dialog::None;
            if (saved && s.after != State::After::None) Perform(s, s.after);
        }
    }
    if (Button(ui, SDL_Rect{d.x + 240, d.y + 100, 200, 40}, Get(4002))) {
        Click();
        s.dialog = State::Dialog::None;
    }
}

void DrawOpen(State& s, Ui& ui) {
    const SDL_Rect d = DialogRect(s);
    if (Drawing(ui)) {
        Screens::DrawBackgroundIn(ui.r, s.theme, d);
        Screens::DrawBevel(ui.r, s.theme, d, false);
    }
    Label(ui, SDL_Rect{d.x + 20, d.y + 14, 420, 22}, Get(9422));
    List(ui, SDL_Rect{d.x + 20, d.y + 40, 420, 280}, s.open_names, s.open_choice, s.open_scroll);
    if (Button(ui, SDL_Rect{d.x + 20, d.y + 340, 200, 40}, Get(4001), s.open_choice >= 0)) {
        Click();
        Scenario::File file;
        std::string why;
        const std::string path = s.open_paths[static_cast<size_t>(s.open_choice)];
        if (Scenario::Load(path, -1, file, &why)) {
            s.doc = std::move(file);
            s.path = path;
            s.dirty = false;
            BuildWorld(s, false);
            LoadFields(s);
            s.dialog = State::Dialog::None;
        } else {
            Notice(s, Get(9423));
        }
    }
    if (Button(ui, SDL_Rect{d.x + 240, d.y + 340, 200, 40}, Get(4002))) {
        Click();
        s.dialog = State::Dialog::None;
    }
}

// Everything on the screen, in one pass that either draws or handles.
void Run(State& s, Ui& ui) {
    const Frame& f = s.frame;
    if (s.dialog == State::Dialog::None || Drawing(ui)) {
        for (int i = 0; i < kTabCount; ++i) {
            Help(ui, f.tabs[i], kTabHelp[i]);
            if (Button(ui, f.tabs[i], Get(kTabStrings[i]), true, s.tab == i + 1)) {
                Click();
                s.tab = i + 1;
                ui.focus = nullptr;
                Layout(s);
                LoadFields(s);
            }
        }
        Help(ui, f.menu, 30640);
        if (Button(ui, f.menu, Get(kMenuString))) {
            Click();
            s.dialog = State::Dialog::Menu;
        }
        Panel(s, ui);
        if (s.tab == kTriggersTab) {
            TriggerColumn(s, ui);
            TriggerPanel(s, ui);
        }
        Button(ui, f.help, Get(kHelpString));
    }
    if (s.dialog == State::Dialog::Menu) DrawMenuDialog(s, ui);
    else if (s.dialog == State::Dialog::SaveAs) DrawSaveAs(s, ui);
    else if (s.dialog == State::Dialog::Open) DrawOpen(s, ui);
    else if (s.dialog == State::Dialog::Confirm) DrawConfirm(s, ui);
}

void PaintAt(State& s, int x, int y) {
    float wx = 0, wy = 0;
    if (!WorldAt(s, x, y, wx, wy)) return;
    const int tx = static_cast<int>(std::floor(wx + 0.5f));
    const int ty = static_cast<int>(std::floor(wy + 0.5f));
    if (tx == s.last_paint_x && ty == s.last_paint_y) return;
    s.last_paint_x = tx;
    s.last_paint_y = ty;
    Paint(s, tx, ty);
}

// A left click in the map view, by what the tab is doing.
void ClickView(State& s, int x, int y) {
    float wx = 0, wy = 0;
    if (!WorldAt(s, x, y, wx, wy)) return;
    if (s.tab == kTriggersTab && PickInView(s, wx, wy)) return;
    if (s.tab == kTerrainTab && s.brush == kBrushCopy) {
        const auto [fx, fy] = FileTileAt(s, wx, wy);
        if (s.copy_paint) {
            LayCopy(s, fx, fy);
        } else {
            // The area to copy, from this corner to where the button comes up.
            s.choosing_area = true;
            s.has_area = true;
            s.area_x0 = s.area_x1 = fx;
            s.area_y0 = s.area_y1 = fy;
        }
        return;
    }
    if (s.tab == kTerrainTab && s.brush == kBrushCliffs) {
        CliffDown(s, static_cast<int>(std::floor(wx + 0.5f)), static_cast<int>(std::floor(wy + 0.5f)), false);
        return;
    }
    if (s.tab == kTerrainTab) {
        s.painting = true;
        s.last_paint_x = s.last_paint_y = -1;
        PaintAt(s, x, y);
        return;
    }
    if (s.tab != kUnitsTab) return;
    switch (s.tool) {
    case kPlace: Place(s, wx, wy); break;
    case kDelete: Delete(s, ThingAt(s, wx, wy)); break;
    case kRotate: Rotate(s, ThingAt(s, wx, wy)); break;
    case kMove: s.moving = ThingAt(s, wx, wy); break;
    case kSelect: s.selected = ThingAt(s, wx, wy); break;
    default: break;
    }
}

} // namespace

State& S() {
    static State state;
    return state;
}

std::string ScenarioDir() { return Screens::ScenariosDir(); }

void Notice(State& s, const std::string& text) {
    s.notice = text;
    s.notice_until = SDL_GetTicks() + 4000;
}

void Layout(State& s) {
    Frame& f = s.frame;
    const int W = s.w, H = s.h;
    // The tabs: two rows of five, 109 by 22, each 3 right of the last and
    // the second row under the first.
    for (int i = 0; i < kTabCount; ++i) {
        f.tabs[i] = SDL_Rect{2 + (i % 5) * 112, 2 + (i / 5) * 24, 109, 22};
    }
    // Menu: 58 by 40, 3 in from the right, 5 down. Help: 20 square, 5 in
    // from the bottom right.
    f.menu = SDL_Rect{W - 3 - 58, 5, 58, 40};
    f.help = SDL_Rect{W - 5 - 20, H - 5 - 20, 20, 20};
    // The mini-map: 230 by 135 in the bottom right, 2 off the bottom.
    // Players, Options, Messages and Cinematics hide it; the panel beside it
    // then takes the width.
    f.minimap_shown = !(s.tab == kPlayersTab || s.tab == kOptionsTab || s.tab == kMessagesTab ||
                        s.tab == kCinematicsTab);
    f.minimap = SDL_Rect{W - 230, H - 2 - 135, 230, 135};
    // The panel: 150 high, 2 in from the left and bottom, 2 short of the
    // mini-map.
    const int panel_right = f.minimap_shown ? f.minimap.x - 3 : W - 3;
    f.panel = SDL_Rect{2, H - 3 - 149, panel_right - 2 + 1, 150};
    // The Units tab's four list buttons over the panel's left end, two rows
    // of two.
    const int heroes_y = f.panel.y - 2 - 22;
    const int units_y = heroes_y - 2 - 22;
    f.unit_lists[0] = SDL_Rect{1, units_y, 113, 23};                  // Units
    f.unit_lists[1] = SDL_Rect{115, units_y - 1, 114, 23};            // Buildings
    f.unit_lists[2] = SDL_Rect{1, heroes_y, 113, 23};                 // Heroes
    f.unit_lists[3] = SDL_Rect{115, heroes_y - 1, 114, 23};           // Other
    // Its list down the left, 250 wide, from under the Menu button to the
    // buttons.
    const int under_menu = f.menu.y + f.menu.h + 6;
    f.unit_list = SDL_Rect{0, under_menu, 250, units_y - 2 - under_menu + 1};
    // The Triggers tab's column.
    const int list_h = std::max(100, (H - 190 - 66) / 3);
    f.trig_label = SDL_Rect{0, f.menu.y + f.menu.h + 2, 225, 22};
    f.trig_list = SDL_Rect{0, f.trig_label.y + 22, 200, list_h};
    f.trig_up = SDL_Rect{200, f.trig_list.y, 25, list_h / 2};
    f.trig_down = SDL_Rect{200, f.trig_up.y + f.trig_up.h + 1, 25, list_h / 2};
    const int buttons_y = f.trig_list.y + list_h + 1;
    for (int i = 0; i < 3; ++i) f.trig_buttons[i] = SDL_Rect{i * 75, buttons_y, 75, 22};
    f.ce_label = SDL_Rect{0, buttons_y + 22 + 5, 225, 22};
    f.ce_list = SDL_Rect{0, f.ce_label.y + 22, 200, list_h};
    f.ce_up = SDL_Rect{200, f.ce_list.y, 25, list_h / 2};
    f.ce_down = SDL_Rect{200, f.ce_up.y + f.ce_up.h + 1, 25, list_h / 2};
    const int delete_y = f.panel.y - 3 - 21;
    f.ce_buttons[2] = SDL_Rect{1, delete_y, 225, 22};
    f.ce_buttons[0] = SDL_Rect{1, delete_y - 2 - 21, 112, 22};
    f.ce_buttons[1] = SDL_Rect{114, delete_y - 2 - 21, 112, 22};
    // The map view: from under the Menu button to over the panel, the whole
    // width but for the Units tab's list or the Triggers tab's column.
    int view_left = 1;
    if (s.tab == kUnitsTab) view_left = f.unit_list.x + f.unit_list.w + 1;
    if (s.tab == kTriggersTab) view_left = f.trig_label.x + f.trig_label.w + 1;
    const int view_top = f.menu.y + f.menu.h + 6;
    const int view_bottom = f.panel.y - 3 - 1;
    f.view = SDL_Rect{view_left, view_top, W - view_left, view_bottom - view_top + 1};
}

bool WorldAt(State& s, int x, int y, float& wx, float& wy) {
    if (!s.sim || !Hit(s.frame.view, x, y)) return false;
    const auto wc = Rendering::Ground::Pick(s.sim->GetGrid(), s.view.camera_x + x / s.view.camera_zoom,
                                            s.view.camera_y + y / s.view.camera_zoom);
    wx = wc.first;
    wy = wc.second;
    const auto& grid = s.sim->GetGrid();
    return wx > -0.5f && wy > -0.5f && wx < grid.GetWidth() - 0.5f && wy < grid.GetHeight() - 0.5f;
}

// Zooms the view at once by `factor`, keeping the screen point (x, y) where
// it is.
void Zoom(State& s, float factor, float x, float y) {
    s.view.target_camera_x = s.view.camera_x;
    s.view.target_camera_y = s.view.camera_y;
    s.view.target_camera_zoom = s.view.camera_zoom;
    s.view.ZoomBy(factor, x, y, true);
}

void CentreOn(State& s, float wx, float wy) {
    const auto p = Core::IsometricMath::WorldToScreen(Core::Fixed32(static_cast<int>(wx)),
                                                      Core::Fixed32(static_cast<int>(wy)));
    s.view.camera_x = p.x - (s.frame.view.x + s.frame.view.w / 2.0f) / s.view.camera_zoom;
    s.view.camera_y = p.y - (s.frame.view.y + s.frame.view.h / 2.0f) / s.view.camera_zoom;
    s.view.target_camera_x = s.view.camera_x;
    s.view.target_camera_y = s.view.camera_y;
}

bool Open(const std::string& path, int screen_w, int screen_h, std::string* error) {
    State& s = S();
    Scenario::File file;
    if (path.empty()) {
        file = NewFile();
    } else if (!Scenario::Load(path, -1, file, error)) {
        return false;
    }
    s = State{};
    s.open = true;
    s.w = screen_w;
    s.h = screen_h;
    s.doc = std::move(file);
    s.path = path;
    s.theme = Screens::MakeTheme(Screens::Sin::kEditor, s.w, s.h);
    s.ui.theme = &s.theme;
    s.ui.font = Text::GameFont(kEditorFont);
    s.ui.screen_h = s.h;
    Layout(s);
    BuildWorld(s, false);
    LoadFields(s);
    HAL::TextInput::Want(HAL::TextInput::Kind::Fields);
    return true;
}

void Resume(int screen_w, int screen_h) {
    State& s = S();
    s.w = screen_w;
    s.h = screen_h;
    s.theme = Screens::MakeTheme(Screens::Sin::kEditor, s.w, s.h);
    s.ui.theme = &s.theme;
    s.ui.screen_h = s.h;
    s.request = Request{};
    Layout(s);
    BuildWorld(s, true);
}

bool IsOpen() { return S().open; }

void ShowTab(int tab) {
    State& s = S();
    if (tab < 1 || tab > kTabCount) return;
    s.tab = tab;
    Layout(s);
    LoadFields(s);
}

void LookAt(int x, int y) { CentreOn(S(), static_cast<float>(x), static_cast<float>(y)); }

void ShowOpenList() {
    State& s = S();
    s.dialog = State::Dialog::Open;
    s.open_paths.clear();
    s.open_names.clear();
    for (const std::string& dir : {ScenarioDir(), std::string("../assets/original/Scenario/"),
                                   std::string("../original_game_1999/GOODIES/")}) {
        for (const std::string& p : Screens::FilesIn(dir, {".scn", ".scx"})) {
            s.open_paths.push_back(p);
            s.open_names.push_back(std::filesystem::path(p).stem().string());
        }
    }
    s.open_choice = s.open_paths.empty() ? -1 : 0;
    s.open_scroll = 0;
}

void HandleEvent(const SDL_Event& event) {
    State& s = S();
    if (!s.open) return;
    if (event.type == SDL_QUIT) {
        s.request.kind = Request::Kind::Quit;
        return;
    }
    if (event.type == SDL_MOUSEMOTION) {
        s.ui.mx = event.motion.x;
        s.ui.my = event.motion.y;
        if (s.painting) PaintAt(s, s.ui.mx, s.ui.my);
        if (s.laying_cliffs || s.erasing_cliffs) {
            float wx = 0, wy = 0;
            if (WorldAt(s, s.ui.mx, s.ui.my, wx, wy)) {
                CliffDrag(s, static_cast<int>(std::floor(wx + 0.5f)), static_cast<int>(std::floor(wy + 0.5f)));
            }
        }
        if (s.moving != entt::null) {
            float wx = 0, wy = 0;
            if (WorldAt(s, s.ui.mx, s.ui.my, wx, wy)) MoveTo(s, s.moving, wx, wy);
        }
        if (s.choosing_area) {
            float wx = 0, wy = 0;
            if (WorldAt(s, s.ui.mx, s.ui.my, wx, wy)) {
                const auto [fx, fy] = FileTileAt(s, wx, wy);
                s.area_x1 = std::clamp(fx, 0, std::max(0, s.doc.width - 1));
                s.area_y1 = std::clamp(fy, 0, std::max(0, s.doc.height - 1));
            }
        }
        return;
    }
    if (event.type == SDL_MOUSEBUTTONDOWN || event.type == SDL_MOUSEBUTTONUP) {
        s.ui.mx = event.button.x;
        s.ui.my = event.button.y;
        s.ui.held = event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT;
    }
    if (event.type == SDL_MOUSEBUTTONUP && event.button.button == SDL_BUTTON_LEFT) {
        s.painting = false;
        s.moving = entt::null;
        s.choosing_area = false;
        if (s.laying_cliffs) CliffUp(s);
        return;
    }
    if (event.type == SDL_MOUSEBUTTONUP && event.button.button == SDL_BUTTON_RIGHT) {
        if (s.erasing_cliffs) CliffUp(s);
        return;
    }
    // An open drop-down takes the next click whatever it hits.
    if (event.type == SDL_MOUSEBUTTONDOWN && s.ui.open_drop != nullptr) {
        ClickOpenDrop(s.ui, event.button.x, event.button.y, nullptr);
        if (s.ui.pending_key == nullptr) return;
    }
    if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE) {
        if (s.ui.open_drop != nullptr) s.ui.open_drop = nullptr;
        else if (s.dialog != State::Dialog::None) s.dialog = State::Dialog::None;
        else if (s.pick != Pick::None) s.pick = Pick::None;
        else s.dialog = State::Dialog::Menu;
        return;
    }
    // The controls, through the same pass that draws them.
    s.ui.r = nullptr;
    s.ui.event = &event;
    s.ui.taken = false;
    Run(s, s.ui);
    s.ui.event = nullptr;
    if (s.ui.taken || s.dialog != State::Dialog::None) return;

    if (event.type == SDL_MOUSEBUTTONDOWN) {
        const int x = event.button.x, y = event.button.y;
        // The mini-map moves the view.
        if (s.frame.minimap_shown && Hit(s.frame.minimap, x, y) && s.sim) {
            const auto& grid = s.sim->GetGrid();
            const SDL_FRect well = {static_cast<float>(s.frame.minimap.x), static_cast<float>(s.frame.minimap.y),
                                    static_cast<float>(s.frame.minimap.w), static_cast<float>(s.frame.minimap.h)};
            float wx = 0, wy = 0;
            if (Rendering::UIRenderer::MinimapUnproject(well, static_cast<float>(x), static_cast<float>(y),
                                                        grid.GetWidth(), grid.GetHeight(), wx, wy)) {
                CentreOn(s, wx, wy);
            }
            return;
        }
        if (event.button.button == SDL_BUTTON_LEFT) {
            s.ui.focus = nullptr;
            ClickView(s, x, y);
        } else if (event.button.button == SDL_BUTTON_RIGHT) {
            s.pick = Pick::None;
            s.selected = entt::null;
            // The Cliffs brush rubs cliffs out under the right button.
            float wx = 0, wy = 0;
            if (s.tab == kTerrainTab && s.brush == kBrushCliffs && WorldAt(s, x, y, wx, wy)) {
                CliffDown(s, static_cast<int>(std::floor(wx + 0.5f)), static_cast<int>(std::floor(wy + 0.5f)), true);
            }
        }
    }
    if (event.type == SDL_KEYDOWN && s.ui.focus == nullptr) {
        if (event.key.keysym.sym == SDLK_DELETE && s.selected != entt::null) {
            Delete(s, s.selected);
            s.selected = entt::null;
        }
        // = and - zoom about the middle of the view, as in the game.
        const SDL_Keycode key = event.key.keysym.sym;
        if ((event.key.keysym.mod & (KMOD_CTRL | KMOD_ALT)) == 0 &&
            (key == SDLK_EQUALS || key == SDLK_PLUS || key == SDLK_MINUS)) {
            Zoom(s, key == SDLK_MINUS ? 1.0f / Rendering::Renderer::kZoomStep : Rendering::Renderer::kZoomStep,
                 s.frame.view.x + s.frame.view.w / 2.0f, s.frame.view.y + s.frame.view.h / 2.0f);
        }
    }
    // The wheel over the map, where no list took it, zooms about the pointer.
    if (event.type == SDL_MOUSEWHEEL && event.wheel.y != 0 && Hit(s.frame.view, s.ui.mx, s.ui.my)) {
        Zoom(s, event.wheel.y > 0 ? Rendering::Renderer::kZoomStep : 1.0f / Rendering::Renderer::kZoomStep,
             static_cast<float>(s.ui.mx), static_cast<float>(s.ui.my));
    }
}

void Update(float dt) {
    State& s = S();
    if (!s.open || !s.sim) return;
    // Only the animation runs (idle units, flags flying), never the
    // simulation's tick.
    ECS::Systems::AnimationSystem::Update(s.sim->GetRegistry(),
                                          static_cast<int32_t>(std::clamp(dt, 0.0f, 0.25f) * 1000.0f));
    if (s.dialog != State::Dialog::None) return;
    float dx = 0, dy = 0;
    const Uint8* keys = SDL_GetKeyboardState(nullptr);
    if (s.ui.focus == nullptr) {
        if (keys[SDL_SCANCODE_LEFT]) dx -= 1;
        if (keys[SDL_SCANCODE_RIGHT]) dx += 1;
        if (keys[SDL_SCANCODE_UP]) dy -= 1;
        if (keys[SDL_SCANCODE_DOWN]) dy += 1;
    }
    // Edge scrolling wants a mouse; on a touch screen two fingers drag the
    // map instead (hal/Touch).
    if (s.ui.mx >= 0 && s.ui.my >= 0 && SDL_GetMouseFocus() != nullptr && HAL::Touch::MouseAttached()) {
        if (s.ui.mx <= kEdge) dx -= 1;
        if (s.ui.mx >= s.w - 1 - kEdge) dx += 1;
        if (s.ui.my <= kEdge) dy -= 1;
        if (s.ui.my >= s.h - 1 - kEdge) dy += 1;
    }
    float stick_x = 0.0f, stick_y = 0.0f;
    HAL::Pad::Scroll(stick_x, stick_y);
    dx += stick_x;
    dy += stick_y;
    float drag_x = 0.0f, drag_y = 0.0f;
    HAL::Touch::TakePan(drag_x, drag_y);
    s.view.camera_x += (dx * kScrollSpeed * dt - drag_x) / s.view.camera_zoom;
    s.view.camera_y += (dy * kScrollSpeed * dt - drag_y) / s.view.camera_zoom;
    // A pinch zooms about the fingers, and the right stick with ZL held about
    // the middle of the view.
    float pinch = 1.0f, pinch_x = 0.0f, pinch_y = 0.0f;
    HAL::Touch::TakeZoom(pinch, pinch_x, pinch_y);
    if (pinch != 1.0f) {
        int gx = s.w / 2, gy = s.h / 2;
        if (const HAL::Window* window = HAL::MainWindow()) {
            window->ToGame(static_cast<int>(pinch_x), static_cast<int>(pinch_y), gx, gy);
        }
        Zoom(s, pinch, static_cast<float>(gx), static_cast<float>(gy));
    }
    if (const float lean = HAL::Pad::Zoom(); lean != 0.0f) {
        Zoom(s, std::pow(kStickZoomPerSecond, lean * dt), s.frame.view.x + s.frame.view.w / 2.0f,
             s.frame.view.y + s.frame.view.h / 2.0f);
    }
}

void Draw(SDL_Renderer* r, Rendering::Renderer& renderer) {
    (void)renderer;
    State& s = S();
    if (!s.open) return;
    if (s.sim) {
        DrawWorld(s, r);
        DrawBrush(s, r);
        DrawPlacing(s, r);
        DrawCopy(s, r);
    }
    DrawChrome(s, r);
    if (s.sim && s.frame.minimap_shown) {
        const SDL_FRect well = {static_cast<float>(s.frame.minimap.x), static_cast<float>(s.frame.minimap.y),
                                static_cast<float>(s.frame.minimap.w), static_cast<float>(s.frame.minimap.h)};
        const SDL_FRect seen = {static_cast<float>(s.frame.view.x), static_cast<float>(s.frame.view.y),
                                static_cast<float>(s.frame.view.w), static_cast<float>(s.frame.view.h)};
        Rendering::UIRenderer::DrawMinimapIn(*s.sim, s.view, r, well, seen);
    }
    s.ui.r = r;
    s.ui.event = nullptr;
    s.ui.hover_help = -1;
    Run(s, s.ui);
    // The help of the control under the pointer, at the bottom of the map
    // view.
    if (s.ui.hover_help >= 0 && s.dialog == State::Dialog::None && s.ui.open_drop == nullptr) {
        const SDL_Rect& v = s.frame.view;
        const int font = Text::GameFont(kHelpFont);
        Screens::DrawWrapped(r, font, v.x + 4 + 5, v.y + v.h - 4 - 50, v.w - 8 - 10, Get(s.ui.hover_help),
                             s.theme.text, s.theme.text_shadow);
    }
    DrawOpenDrop(s.ui);
    s.ui.r = nullptr;
    // News across the top of the view.
    if (!s.notice.empty() && SDL_GetTicks() < s.notice_until) {
        const int w = Text::Width(s.ui.font, s.notice);
        Text::DrawShadowed(r, s.ui.font, s.frame.view.x + (s.frame.view.w - w) / 2, s.frame.view.y + 8,
                           s.notice, s.theme.text, s.theme.text_shadow);
    }
}

Request TakeRequest() {
    State& s = S();
    Request r = s.request;
    s.request = Request{};
    if (r.kind == Request::Kind::Exit || r.kind == Request::Kind::Quit) {
        s.open = false;
        s.sim.reset();
        s.refs.clear();
        HAL::TextInput::Want(HAL::TextInput::Kind::None);
    }
    return r;
}

} // namespace Editor
