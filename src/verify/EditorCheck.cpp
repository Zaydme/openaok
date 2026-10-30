#include "verify/Harness.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <unistd.h>
#include <vector>

#include "core/Cliffs.h"
#include "core/ConfigManager.h"
#include "core/Rules.h"
#include "ecs/Buildings.h"
#include "ecs/ScenarioWorld.h"
#include "ecs/Triggers.h"
#include "ecs/components/Identity.h"
#include "ecs/components/Owner.h"
#include "ecs/components/Sprite.h"
#include "ecs/components/Transform.h"
#include "editor/EditorState.h"
#include "hal/GraphicCache.h"
#include "scenario/Campaign.h"
#include "scenario/Scenario.h"

namespace Verify {

namespace {

const char* const kDemo = "../original_game_1999/GOODIES/DEMO.SCN";
const char* const kCampaigns[] = {
    "../assets/original/Campaign/cam1.cpn", "../assets/original/Campaign/cam2.cpn",
    "../assets/original/Campaign/cam3.cpn", "../assets/original/Campaign/cam4.cpn",
    "../assets/original/Campaign/cam8.cpn",
};

struct Verdict {
    std::string why;
    bool ok() const { return why.empty(); }
    void Fail(const std::string& reason) {
        if (why.empty()) why = reason;
    }
    void Expect(bool holds, const std::string& reason) {
        if (!holds) Fail(reason);
    }
};

// Which part of two files differs first, by name, or empty when none does.
std::string Difference(const Scenario::File& a, const Scenario::File& b) {
    if (a.version != b.version || a.player_data_version != b.player_data_version ||
        a.trigger_version != b.trigger_version) {
        return "the versions";
    }
    if (a.save_time != b.save_time || a.header_instructions != b.header_instructions ||
        a.enabled_players != b.enabled_players) {
        return "the plain header";
    }
    if (a.next_unit_id != b.next_unit_id || a.original_name != b.original_name) return "the next unit id or name";
    for (int t = 0; t < Scenario::kTextCount; ++t) {
        if (!(a.texts[t] == b.texts[t])) return "text " + std::to_string(t);
    }
    if (a.pregame_cinematic != b.pregame_cinematic || a.victory_cinematic != b.victory_cinematic ||
        a.loss_cinematic != b.loss_cinematic || a.background != b.background) {
        return "the cinematics";
    }
    if (!(a.bitmap == b.bitmap)) return "the instructions map";
    for (int p = 0; p < Core::kMaxPlayers; ++p) {
        const Scenario::Player& x = a.players[p];
        const Scenario::Player& y = b.players[p];
        const std::string who = "player " + std::to_string(p) + "'s ";
        if (x.units.size() != y.units.size()) return who + "unit count";
        for (size_t u = 0; u < x.units.size(); ++u) {
            if (!(x.units[u] == y.units[u])) return who + "unit " + std::to_string(x.units[u].id);
        }
        if (x.name != y.name || x.name_string != y.name_string) return who + "name";
        if (x.active != y.active || x.human != y.human || x.civ != y.civ) return who + "seat";
        if (x.food != y.food || x.wood != y.wood || x.gold != y.gold || x.stone != y.stone) {
            return who + "stockpiles";
        }
        if (x.starting_age != y.starting_age || x.colour != y.colour) return who + "age or colour";
        for (int q = 0; q < Core::kMaxPlayers; ++q) {
            if (x.stance[q] != y.stance[q]) return who + "stance";
        }
        if (x.allied_victory != y.allied_victory || x.camera_x != y.camera_x || x.camera_y != y.camera_y) {
            return who + "allied victory or camera";
        }
        if (x.disabled_techs != y.disabled_techs || x.disabled_units != y.disabled_units ||
            x.disabled_buildings != y.disabled_buildings) {
            return who + "disables";
        }
        if (!(x == y)) return who + "AI";
    }
    if (!(a.victory == b.victory) || a.all_techs != b.all_techs) return "the victory";
    if (a.camera_x != b.camera_x || a.camera_y != b.camera_y) return "the view";
    if (a.width != b.width || a.height != b.height || a.terrain != b.terrain) return "the terrain";
    if (a.elevation != b.elevation) return "the elevation";
    if (a.objectives_state != b.objectives_state || a.triggers.size() != b.triggers.size()) {
        return "the trigger count";
    }
    for (size_t t = 0; t < a.triggers.size(); ++t) {
        if (!(a.triggers[t] == b.triggers[t])) return "trigger " + std::to_string(t);
    }
    if (a.trigger_order != b.trigger_order) return "the triggers' order";
    if (!(a.kept == b.kept)) {
        for (int s = 0; s < Scenario::kFileSlots; ++s) {
            if (!(a.kept.slots[s] == b.kept.slots[s])) return "slot " + std::to_string(s) + " of the kept";
        }
        for (int p = 0; p < Scenario::kPlayers; ++p) {
            if (!(a.kept.settings[p] == b.kept.settings[p])) {
                return "player " + std::to_string(p + 1) + "'s kept settings";
            }
        }
        return "the kept header";
    }
    if (!(a == b)) return "something else";
    return {};
}

// Where two byte strings part, or -1.
long Parting(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b) {
    const size_t n = a.size() < b.size() ? a.size() : b.size();
    for (size_t i = 0; i < n; ++i) {
        if (a[i] != b[i]) return static_cast<long>(i);
    }
    return a.size() == b.size() ? -1 : static_cast<long>(n);
}

bool ReadBytes(const char* path, int entry, std::vector<uint8_t>& out, std::string& error) {
    if (entry >= 0) return Scenario::ReadCampaignScenario(path, entry, out, &error);
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        error = "cannot open";
        return false;
    }
    out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return true;
}

// One file of the disc: read, written, read again -- the same file, field by
// field and, inflated, byte by byte.
void RoundTrip(Verdict& v, const std::vector<uint8_t>& original, const std::string& name, int& identical,
               size_t& packed, size_t& written_size) {
    Scenario::File first;
    std::string error;
    if (!Scenario::Read(original, first, &error)) {
        v.Fail(name + " unread: " + error);
        return;
    }
    const std::vector<uint8_t> written = Scenario::Write(first);
    Scenario::File second;
    if (!Scenario::Read(written, second, &error)) {
        v.Fail(name + " written is unreadable: " + error);
        return;
    }
    const std::string differs = Difference(first, second);
    if (!differs.empty()) {
        v.Fail(name + " written and read again differs in " + differs);
        return;
    }
    std::vector<uint8_t> before, after;
    if (!Scenario::Expand(original, before, &error) || !Scenario::Expand(written, after, &error)) {
        v.Fail(name + " does not inflate: " + error);
        return;
    }
    const long at = Parting(before, after);
    if (at >= 0) {
        v.Fail(name + " written is not the file read: they part at byte " + std::to_string(at) + " of " +
               std::to_string(before.size()) + " (" + std::to_string(after.size()) + " written)");
        return;
    }
    ++identical;
    packed += original.size();
    written_size += written.size();
}

// What the editor does to a file -- a name in another alphabet's letters, a
// text, a stockpile, a stance, a disabled technology, a unit placed and one
// turned, a trigger with an effect naming units -- survives a write and a
// read, and the comparison sees a change when there is one.
void Edits(Verdict& v, const std::vector<uint8_t>& demo) {
    Scenario::File file;
    std::string error;
    if (!Scenario::Read(demo, file, &error)) {
        v.Fail("DEMO.SCN unread for editing: " + error);
        return;
    }
    {
        // The comparison is not blind: one unit moved by a hair is noticed.
        Scenario::File moved = file;
        moved.players[1].units[0].x = moved.players[1].units[0].x + Core::Fixed32(1) / Core::Fixed32(64);
        Scenario::File back;
        Scenario::Read(Scenario::Write(moved), back, &error);
        v.Expect(!Difference(file, back).empty(), "a unit moved went unnoticed by the comparison");
        v.Expect(back.players[1].units[0].x == moved.players[1].units[0].x,
                 "a unit moved by 1/64 of a tile came back elsewhere");
    }
    Scenario::File edited = file;
    edited.players[2].name = "Bretagne \xC3\xA0 l'\xC3\xA9t\xC3\xA9";   // "à l'été"
    edited.texts[Scenario::kHints].text = "Build \xE2\x80\x9C" "walls\xE2\x80\x9D";   // curly quotes
    edited.players[1].food = 1234;
    edited.players[1].stance[2] = 1;
    edited.players[3].disabled_techs.push_back(101);
    edited.players[3].starting_age = 2;
    Scenario::Unit placed;
    placed.x = Core::Fixed32(20) + Core::Fixed32(1) / Core::Fixed32(2);
    placed.y = Core::Fixed32(30) + Core::Fixed32(1) / Core::Fixed32(2);
    placed.id = static_cast<int32_t>(edited.next_unit_id++);
    placed.unit_id = 83;   // a villager
    placed.state = 2;
    placed.rotation = Core::Fixed32(3);
    edited.players[2].units.push_back(placed);
    edited.players[0].units[5].rotation = Core::Fixed32(1) / Core::Fixed32(8);
    Scenario::Trigger trigger;
    trigger.name = "Made in the editor";
    Scenario::Condition timer;
    timer.type = Scenario::ConditionType::Timer;
    timer.timer = 30;
    trigger.conditions.push_back(timer);
    Scenario::Effect task;
    task.type = Scenario::EffectType::TaskObject;
    task.player = 2;
    task.location = {40, 41};
    task.units = {placed.id, edited.players[2].units[0].id};
    task.message = "Go";
    trigger.effects.push_back(task);
    edited.triggers.push_back(trigger);

    Scenario::File back;
    if (!Scenario::Read(Scenario::Write(edited), back, &error)) {
        v.Fail("an edited DEMO.SCN written is unreadable: " + error);
        return;
    }
    v.Expect(back.players[2].name == edited.players[2].name, "an accented name did not survive");
    v.Expect(back.texts[Scenario::kHints].text == edited.texts[Scenario::kHints].text,
             "curly quotes in a text did not survive");
    v.Expect(back.players[1].food == 1234 && back.players[1].stance[2] == 1, "a stockpile or stance was lost");
    v.Expect(back.players[3].disabled_techs == edited.players[3].disabled_techs &&
                 back.players[3].starting_age == 2,
             "a disabled technology or an age was lost");
    v.Expect(!back.players[2].units.empty() && back.players[2].units.back().x == placed.x &&
                 back.players[2].units.back().y == placed.y && back.players[2].units.back().unit_id == 83 &&
                 back.players[2].units.back().rotation == Core::Fixed32(3) &&
                 back.players[2].units.back().id == placed.id && back.next_unit_id == edited.next_unit_id,
             "a placed unit came back wrong");
    v.Expect(back.players[0].units[5].rotation == Core::Fixed32(1) / Core::Fixed32(8), "a turned unit was lost");
    v.Expect(!back.triggers.empty() && back.triggers.back().name == "Made in the editor" &&
                 back.triggers.back().conditions.size() == 1 && back.triggers.back().conditions[0].timer == 30 &&
                 back.triggers.back().effects.size() == 1 && back.triggers.back().effects[0].units == task.units &&
                 back.triggers.back().effects[0].location == task.location &&
                 back.triggers.back().effects[0].message == "Go" &&
                 back.triggers.back().effects[0].selected_count == 2 &&
                 back.trigger_order.size() == back.triggers.size(),
             "a trigger made in the editor came back wrong");
    // Everything not edited is as it was.
    v.Expect(back.players[4] == file.players[4] && back.triggers[2] == file.triggers[2] &&
                 back.terrain == file.terrain,
             "an edit disturbed what was not edited");
}

// A scenario made from nothing, as the editor's New makes one: written with
// the editor's defaults for everything the game does not play, and read back.
void NewFile(Verdict& v) {
    Scenario::File file;
    file.version = "1.18";
    file.player_data_version = 120;
    file.trigger_version = 16;
    file.width = 72;
    file.height = 72;
    file.terrain.assign(72 * 72, 0);
    file.elevation.assign(72 * 72, 0);
    file.elevation[10 * 72 + 10] = 2;
    file.enabled_players = 2;
    file.next_unit_id = 1;
    for (int p = 1; p <= Scenario::kPlayers; ++p) {
        file.players[p].active = p <= 2;
        file.players[p].human = p == 1;
        file.players[p].civ = 1;
        file.players[p].colour = p - 1;
        file.players[p].food = file.players[p].wood = file.players[p].gold = file.players[p].stone = 200;
        file.players[p].starting_age = 0;
        for (int q = 1; q <= Scenario::kPlayers; ++q) file.players[p].stance[q] = q == p ? 0 : 3;
    }
    Scenario::Unit tc;
    tc.x = Core::Fixed32(20);
    tc.y = Core::Fixed32(20);
    tc.id = 0;
    tc.unit_id = 109;
    tc.state = 2;
    file.players[1].units.push_back(tc);
    std::string error;
    Scenario::File back;
    if (!Scenario::Read(Scenario::Write(file), back, &error)) {
        v.Fail("a new scenario written is unreadable: " + error);
        return;
    }
    v.Expect(back.width == 72 && back.elevation[10 * 72 + 10] == 2 && back.players[1].units.size() == 1 &&
                 back.players[1].units[0].unit_id == 109 && back.players[2].active && !back.players[3].active &&
                 back.players[1].stance[2] == 3 && back.version == "1.18" && back.player_data_version == 120 &&
                 back.trigger_version == 16,
             "a new scenario came back wrong");
}


// The cliff pieces of a world, by the cell of the 3x3 cliff lattice each
// stands in: its links (core/Cliffs), from its drawing.
std::map<std::pair<int, int>, std::array<int8_t, 4>> CliffCells(ECS::Simulation& sim) {
    namespace C = ECS::Components;
    std::map<std::pair<int, int>, std::array<int8_t, 4>> cells;
    auto& grid = sim.GetGrid();
    auto all = sim.GetRegistry().view<const C::Identity, const C::Transform, const C::Sprite>();
    for (auto e : all) {
        const int unit = all.get<const C::Identity>(e).unit_id;
        const Core::Cliffs::Piece* piece = Core::Cliffs::PieceDrawn(unit, all.get<const C::Sprite>(e).slp_frame);
        if (unit < 264 || unit > 273 || piece == nullptr) continue;
        const auto& p = all.get<const C::Transform>(e).position;
        const auto [x2, y2] = Core::Cliffs::OriginalHalves(grid, p.x, p.y);
        cells[{x2 / 6, y2 / 6}] = piece->links;
    }
    return cells;
}

// The editor's own edits, made through its document code as its controls
// make them, on DEMO.SCN's world: a villager and a Town Center placed, the
// villager turned and deleted, water and forest painted, a hill raised,
// cliffs dragged out and rubbed out; saved, read, built as a game's world as
// Test builds it; and a random map generated and saved. Each edit must reach
// both the file, in the file's frame, and the world, in the grid's -- a
// quarter turn from the file's, as a scenario game turns it -- and whatever
// stands at a point of the editor's world stands there in the Test game.
void Editing(Verdict& v) {
    namespace C = ECS::Components;
    using Core::Fixed32;
    const Fixed32 half = Fixed32(1) / Fixed32(2);
    Editor::State s;
    std::string error;
    if (!Scenario::Load(kDemo, -1, s.doc, &error)) {
        v.Fail("DEMO.SCN unread for the editor: " + error);
        return;
    }
    Editor::Layout(s);
    Editor::BuildWorld(s, false);
    auto& reg = s.sim->GetRegistry();
    size_t units = 0;
    for (const auto& p : s.doc.players) units += p.units.size();
    v.Expect(s.refs.size() == units, "the editor's world holds " + std::to_string(s.refs.size()) +
                                         " of DEMO.SCN's " + std::to_string(units) + " things");
    const int w = s.doc.width;
    auto file_index = [&](int gx, int gy) {
        const auto [fx, fy] = Editor::FileTile(s, gx, gy);
        return static_cast<size_t>(fy) * static_cast<size_t>(w) + static_cast<size_t>(fx);
    };
    // The turn, both ways: the file's tile (0, 0) is the grid's (0, w - 1),
    // and a point of the world comes back to itself through the file.
    float fx = 0, fy = 0;
    Editor::FileOf(s, 12.25f, 40.5f, fx, fy);
    const auto back_there = Editor::WorldOf(s, Fixed32(fx), Fixed32(fy));
    v.Expect(Editor::GridTile(s, 0, 0) == std::make_pair(0, w - 1) &&
                 Editor::FileTile(s, 0, w - 1) == std::make_pair(0, 0) &&
                 std::abs(back_there.x.ToFloat() - 12.25f) < 0.001f && std::abs(back_there.y.ToFloat() - 40.5f) < 0.001f,
             "the editor's turn between the file and the world is not ScenarioWorld's");
    // The grid's tile in the middle of the map view, as the file names it.
    auto view_tile = [&](int& gx, int& gy) {
        float wx = 0, wy = 0;
        const SDL_Rect& view = s.frame.view;
        if (!Editor::WorldAt(s, view.x + view.w / 2, view.y + view.h / 2, wx, wy)) return false;
        gx = static_cast<int>(std::lround(wx));
        gy = static_cast<int>(std::lround(wy));
        return true;
    };
    // It opens on the view the file was saved with.
    int view_x = -1, view_y = -1;
    const bool viewed = view_tile(view_x, view_y);
    const auto [view_fx, view_fy] = Editor::FileTile(s, view_x, view_y);
    v.Expect(viewed && std::abs(view_fx - s.doc.camera_x) <= 1 && std::abs(view_fy - s.doc.camera_y) <= 1,
             "the editor does not open on the view DEMO.SCN was saved with");

    // A unit from the Units list, for player 2, where the click lands: the
    // middle of the tile, in the file's frame.
    s.category = Editor::kUnitsList;
    Editor::FillUnitList(s);
    auto choose = [&](const char* name) {
        const int id = Core::ConfigManager::Get().GetUnitIdByName(name);
        for (size_t i = 0; i < s.unit_ids.size(); ++i) {
            if (s.unit_ids[i] == id) {
                s.unit_choice = static_cast<int>(i) + 1;
                return id;
            }
        }
        return -1;
    };
    const int villager = choose("VMBAS");
    v.Expect(villager >= 0, "the Units list has no villager");
    // Open grass for two, and room for a Town Center, found on the map.
    const auto& grid = s.sim->GetGrid();
    const int tc = Core::ConfigManager::Get().GetUnitIdByName("RTWC");
    int vx = -1, vy = -1, ux = -1, uy = -1, tx = -1, ty = -1;
    for (int y = 10; y < grid.GetHeight() - 10 && (ux < 0 || tx < 0); ++y) {
        for (int x = 10; x < grid.GetWidth() - 10 && (ux < 0 || tx < 0); ++x) {
            const bool open = grid.IsWalkable(x, y) && s.doc.terrain[file_index(x, y)] == 0;
            if (vx < 0 && open) {
                vx = x;
                vy = y;
            } else if (ux < 0 && open && (x != vx || y != vy)) {
                ux = x;
                uy = y;
            } else if (tx < 0 && x > ux + 6 && ux >= 0 && ECS::Buildings::CanPlace(grid, tc, x, y)) {
                tx = x;
                ty = y;
            }
        }
    }
    s.unit_player = 2;
    const uint32_t before = s.doc.next_unit_id;
    // A click a little off the tile's middle.
    Editor::Place(s, static_cast<float>(vx) + 0.2f, static_cast<float>(vy) - 0.3f);
    const auto& p2 = s.doc.players[2].units;
    const auto [vfx, vfy] = Editor::FileTile(s, vx, vy);
    const bool placed = vx >= 0 && !p2.empty() && p2.back().unit_id == villager &&
                        p2.back().id == static_cast<int32_t>(before) && p2.back().x == Fixed32(vfx) + half &&
                        p2.back().y == Fixed32(vfy) + half;
    v.Expect(placed, "a placed villager is not in the file in the middle of the file's tile");
    entt::entity placed_entity = entt::null;
    for (const auto& [key, ref] : s.refs) {
        if (ref.id == static_cast<int32_t>(before)) placed_entity = static_cast<entt::entity>(key);
    }
    v.Expect(placed_entity != entt::null && reg.valid(placed_entity) &&
                 reg.get<C::Transform>(placed_entity).position.x == Fixed32(vx) &&
                 reg.get<C::Transform>(placed_entity).position.y == Fixed32(vy) &&
                 reg.get<C::Owner>(placed_entity).player_id == 2,
             "a placed villager is not in the world on the tile clicked, player 2's");
    // Turned: an eighth of a turn more.
    if (placed_entity != entt::null) {
        Editor::Rotate(s, placed_entity);
        v.Expect(std::abs(s.doc.players[2].units.back().rotation.ToFloat() - 0.785398f) < 0.001f,
                 "a villager turned is not an eighth of a turn round");
    }
    // A second, to stay.
    Editor::Place(s, static_cast<float>(ux) - 0.4f, static_cast<float>(uy) + 0.4f);
    const int32_t kept_id = !p2.empty() && p2.back().unit_id == villager ? p2.back().id : -1;
    v.Expect(kept_id > static_cast<int32_t>(before), "a second villager was not placed");

    // A Town Center -- four tiles, so on the corner nearest -- from the
    // Buildings list.
    s.category = Editor::kBuildingsList;
    Editor::FillUnitList(s);
    v.Expect(choose("RTWC") == tc, "the Buildings list has no Town Center");
    Editor::Place(s, static_cast<float>(tx) - 0.7f, static_cast<float>(ty) - 0.7f);
    const bool tc_file = tx >= 0 && !p2.empty() && p2.back().unit_id == tc && p2.back().x == Fixed32(w - ty) &&
                         p2.back().y == Fixed32(tx);
    v.Expect(tc_file, "a Town Center placed is not in the file on the file's corner");
    const int32_t tc_id = tc_file ? p2.back().id : -1;
    bool tc_world = false;
    for (const auto& [key, ref] : s.refs) {
        const auto e = static_cast<entt::entity>(key);
        if (ref.id == tc_id && reg.valid(e)) {
            const auto& at = reg.get<C::Transform>(e).position;
            tc_world = at.x == Fixed32(tx) && at.y == Fixed32(ty);
        }
    }
    v.Expect(tc_world, "a Town Center placed is not in the world on the tile clicked");

    // The villager deleted: gone from both.
    const size_t things = s.refs.size();
    for (const auto& [key, ref] : s.refs) {
        if (ref.id == static_cast<int32_t>(before)) placed_entity = static_cast<entt::entity>(key);
    }
    Editor::Delete(s, placed_entity);
    v.Expect(s.doc.FindUnit(static_cast<int32_t>(before)) == nullptr && !reg.valid(placed_entity) &&
                 s.refs.size() == things - 1,
             "a deleted villager is still in the file or the world");

    // Water painted with the Small brush, three tiles across.
    s.brush = Editor::kBrushTerrain;
    s.brush_size = 2;
    s.terrain_choice = 3;   // Water 1
    Editor::Paint(s, 30, 30);
    bool water = true;
    for (int y = 29; y <= 31; ++y) {
        for (int x = 29; x <= 31; ++x) {
            water = water && s.doc.terrain[file_index(x, y)] == 1 &&
                    s.sim->GetGrid().GetTile(x, y).terrain == ECS::ScenarioWorld::TerrainOf(1);
        }
    }
    v.Expect(water && s.doc.terrain[file_index(30, 28)] != 1,
             "water painted did not cover three tiles across in the file and the world");
    // Forest painted grows its tree, one a tile, in the middle.
    s.brush_size = 1;
    s.terrain_choice = 2;   // Forest
    Editor::Paint(s, 20, 20);
    bool tree = false;
    const auto [tfx, tfy] = Editor::FileTile(s, 20, 20);
    for (const auto& u : s.doc.players[0].units) {
        tree = tree || (u.unit_id == 411 && u.x == Fixed32(tfx) + half && u.y == Fixed32(tfy) + half);
    }
    v.Expect(tree && !s.sim->GetGrid().IsWalkable(20, 20), "forest painted grew no tree");
    // A hill: level 3 on one tile, the ground round it sloping down a level
    // a tile.
    s.brush = Editor::kBrushElevation;
    s.elevation_choice = 3;
    Editor::Paint(s, 100, 100);
    bool slopes = s.doc.elevation[file_index(100, 100)] == 3;
    for (int y = 0; y + 1 < s.doc.height && slopes; ++y) {
        for (int x = 0; x + 1 < s.doc.width; ++x) {
            const int e = s.doc.elevation[static_cast<size_t>(y * s.doc.width + x)];
            const int r = s.doc.elevation[static_cast<size_t>(y * s.doc.width + x + 1)];
            const int d = s.doc.elevation[static_cast<size_t>((y + 1) * s.doc.width + x)];
            if (std::abs(e - r) > 1 || std::abs(e - d) > 1) slopes = false;
        }
    }
    v.Expect(slopes && s.sim->GetGrid().GetTile(100, 100).elevation == 3 &&
                 s.sim->GetGrid().GetTile(101, 100).elevation == s.doc.elevation[file_index(101, 100)] &&
                 s.sim->GetGrid().GetTile(100, 99).elevation == s.doc.elevation[file_index(100, 99)],
             "a hill raised is not level 3 with slopes of one");

    // Cliffs, dragged out a cell of the 3x3 cliff lattice at a time, in the
    // file's frame. One east three cells and south two -- faces all along, a
    // face corner -- and one east two and north two, whose turn shows the lip
    // and keeps it. The drags are given as the file's tiles the pointer is
    // over.
    s.brush = Editor::kBrushCliffs;
    auto press = [&](int x, int y, bool erase) {
        const auto [gx, gy] = Editor::GridTile(s, x, y);
        Editor::CliffDown(s, gx, gy, erase);
    };
    auto drag = [&](int x, int y) {
        const auto [gx, gy] = Editor::GridTile(s, x, y);
        Editor::CliffDrag(s, gx, gy);
    };
    const auto cliffs_before = CliffCells(*s.sim);
    const size_t gaia_before = s.doc.players[0].units.size();
    press(31, 31, false);     // cell (10, 10)
    drag(40, 31);             // on to (13, 10)
    drag(40, 37);             // and down to (13, 12)
    Editor::CliffUp(s);
    press(61, 31, false);     // cell (20, 10)
    drag(67, 31);             // on to (22, 10)
    drag(67, 25);             // and up to (22, 8)
    Editor::CliffUp(s);
    using L = std::array<int8_t, 4>;
    const std::map<std::pair<int, int>, L> laid = {
        {{10, 10}, {1, 0, 0, 0}},  {{11, 10}, {1, 0, 1, 0}}, {{12, 10}, {1, 0, 1, 0}}, {{13, 10}, {0, 1, 1, 0}},
        {{13, 11}, {0, 1, 0, 1}},  {{13, 12}, {0, 0, 0, 1}}, {{20, 10}, {1, 0, 0, 0}}, {{21, 10}, {1, 0, 1, 0}},
        {{22, 10}, {0, 0, 1, -1}}, {{22, 9}, {0, -1, 0, -1}}, {{22, 8}, {0, -1, 0, 0}},
    };
    auto cells = CliffCells(*s.sim);
    for (const auto& c : cliffs_before) cells.erase(c.first);
    for (const auto& c : laid) v.Expect(cliffs_before.count(c.first) == 0, "DEMO.SCN has a cliff where the check lays one");
    v.Expect(cells == laid && s.doc.players[0].units.size() == gaia_before + laid.size(),
             "cliffs dragged out are not the original's pieces (" + std::to_string(cells.size()) + " of 11 laid)");
    if (const char* wrong = CliffsJoin(*s.sim)) v.Fail(std::string("cliffs laid in the editor: ") + wrong);
    // In the file, a piece stands at its cell's corner plus its own offset, in
    // half tiles (the corner piece of cell (13, 10) at 39 + 1.5 both ways),
    // with its rotation equal to its frame, as on every cliff of the shipped
    // scenarios (Mongols_scn2's 218).
    bool corner_placed = false;
    for (const auto& u : s.doc.players[0].units) {
        corner_placed = corner_placed || (u.unit_id == 264 && u.frame == 15 && u.rotation == Fixed32(15) &&
                                          u.state == 2 && u.x == Fixed32(39) + Fixed32(3) * half &&
                                          u.y == Fixed32(30) + Fixed32(3) * half);
    }
    v.Expect(corner_placed, "a cliff corner laid is not in the file where the original stands it");
    Scenario::File mongols;
    int disc_cliffs = 0, unlike = 0;
    if (Scenario::Load(kCampaigns[2], 1, mongols, &error)) {
        for (const auto& u : mongols.players[0].units) {
            if (u.unit_id < 264 || u.unit_id > 273) continue;
            ++disc_cliffs;
            unlike += u.rotation == Fixed32(u.frame) && u.state == 2 ? 0 : 1;
        }
    }
    v.Expect(disc_cliffs == 218 && unlike == 0, "Mongols_scn2's cliffs are not as the editor writes its own");
    // A cliff's tiles are blocked in the world.
    const auto [cgx, cgy] = Editor::GridTile(s, 37, 31);
    v.Expect(!s.sim->GetGrid().IsWalkable(cgx, cgy), "a cliff laid does not block its ground");
    // Pressed on the first's end, a drag goes on from it, and the end becomes a
    // corner.
    press(40, 37, false);
    drag(46, 37);             // on to (15, 12)
    Editor::CliffUp(s);
    cells = CliffCells(*s.sim);
    v.Expect(cells[{13, 12}] == L{1, 0, 0, 1} && cells[{14, 12}] == L{1, 0, 1, 0} && cells[{15, 12}] == L{0, 0, 1, 0},
             "a cliff pressed on at its end did not go on from it");
    // Dragged north across the second's straight: that cell turns to the new
    // cliff and the second is cut there -- its west end gone, its corner an
    // end.
    press(64, 37, false);     // cell (21, 12)
    drag(64, 25);             // up to (21, 8)
    Editor::CliffUp(s);
    cells = CliffCells(*s.sim);
    v.Expect(cells[{21, 12}] == L{0, 0, 0, 1} && cells[{21, 11}] == L{0, 1, 0, 1} && cells[{21, 10}] == L{0, 1, 0, 1} &&
                 cells[{21, 9}] == L{0, 1, 0, 1} && cells[{21, 8}] == L{0, 1, 0, 0} && cells.count({20, 10}) == 0 &&
                 cells[{22, 10}] == L{0, 0, 0, -1} && cells[{22, 9}] == L{0, -1, 0, -1},
             "a cliff dragged across another did not cut it as the original does");
    if (const char* wrong = CliffsJoin(*s.sim)) v.Fail(std::string("cliffs crossed in the editor: ") + wrong);
    // Its middle rubbed out with the right button: the pieces either side
    // end there.
    press(37, 31, true);      // cell (12, 10)
    Editor::CliffUp(s);
    cells = CliffCells(*s.sim);
    v.Expect(cells.count({12, 10}) == 0 && cells[{11, 10}] == L{0, 0, 1, 0} && cells[{13, 10}] == L{0, 1, 0, 0},
             "a cliff rubbed out did not end the pieces either side");
    if (const char* wrong = CliffsJoin(*s.sim)) v.Fail(std::string("cliffs rubbed out in the editor: ") + wrong);
    v.Expect(s.sim->GetGrid().IsWalkable(Editor::GridTile(s, 37, 31).first, Editor::GridTile(s, 37, 31).second),
             "a cliff rubbed out still blocks its ground");
    const size_t pieces = CliffCells(*s.sim).size();

    // Saved, read back and saved again: the same file. The view left
    // somewhere else first, to be the one the scenario opens on.
    // This run's own: another checkout's gate may be running at once.
    const std::string path =
        (std::filesystem::temp_directory_path() / ("aoe-editor-check-" + std::to_string(::getpid()) + ".scn")).string();
    Editor::CentreOn(s, 40.0f, 70.0f);
    int saved_x = -1, saved_y = -1;
    v.Expect(view_tile(saved_x, saved_y), "the map view shows no ground");
    v.Expect(Editor::SaveTo(s, path), "the editor could not save " + path);
    Scenario::File back, again;
    if (!Scenario::Load(path, -1, back, &error)) {
        v.Fail("the editor's scenario is unreadable: " + error);
    } else {
        const std::vector<uint8_t> first = Scenario::Write(back);
        Scenario::Read(first, again, &error);
        v.Expect(Difference(back, again).empty(), "the editor's scenario saved twice differs");
        v.Expect(back.FindUnit(tc_id) != nullptr && back.terrain[file_index(30, 30)] == 1 &&
                     back.elevation[file_index(100, 100)] == 3,
                 "the editor's scenario lost an edit in saving");
    }
    // Test: the game the menu's Test starts, built from the saved file.
    // Whatever stands at a point of the editor's world stands at that point
    // in the game: the villager where it was clicked, the Town Center, the
    // water, the hill, the cliffs, and every thing of DEMO.SCN's.
    Core::GameSetup setup = Core::DefaultSetup();
    if (Scenario::SetupFor(path, -1, setup, &error)) {
        ECS::Simulation game;
        game.Init(setup);
        auto& game_reg = game.GetRegistry();
        bool found = false;
        auto view = game_reg.view<C::Identity, C::Transform, C::Owner>();
        for (auto e : view) {
            if (view.get<C::Identity>(e).unit_id == tc && view.get<C::Owner>(e).player_id == 2 &&
                view.get<C::Transform>(e).position.x == Fixed32(tx) &&
                view.get<C::Transform>(e).position.y == Fixed32(ty)) {
                found = true;
            }
        }
        v.Expect(found && game.GetGrid().GetTile(30, 30).terrain == ECS::ScenarioWorld::TerrainOf(1) &&
                     game.GetGrid().GetTile(100, 100).elevation == 3,
                 "the Test game lacks the editor's Town Center, its water or its hill where the editor showed them");
        v.Expect(setup.players[1].active && !game.player_starts.empty() &&
                     std::abs(game.player_starts[0].first - saved_x) <= 1 &&
                     std::abs(game.player_starts[0].second - saved_y) <= 1,
                 "the Test game does not open on the view the editor was left on");
        const entt::entity kept = ECS::Triggers::EntityOf(game_reg, kept_id);
        v.Expect(kept != entt::null && game_reg.get<C::Transform>(kept).position.x == Fixed32(ux) &&
                     game_reg.get<C::Transform>(kept).position.y == Fixed32(uy),
                 "a villager placed in the editor does not stand where it was clicked in the Test game");
        // Every thing of the editor's world, cliffs included, stands at the
        // same point in the Test game.
        auto& shown = s.sim->GetRegistry();
        int compared = 0, moved = 0;
        for (const auto& [key, ref] : s.refs) {
            const auto e = static_cast<entt::entity>(key);
            const entt::entity there = ECS::Triggers::EntityOf(game_reg, ref.id);
            if (!shown.valid(e) || there == entt::null || !shown.all_of<C::Transform>(e) ||
                !game_reg.all_of<C::Transform>(there)) {
                continue;
            }
            ++compared;
            const auto& a = shown.get<C::Transform>(e).position;
            const auto& b = game_reg.get<C::Transform>(there).position;
            moved += a.x == b.x && a.y == b.y ? 0 : 1;
        }
        v.Expect(compared > 100 && moved == 0, std::to_string(moved) + " of " + std::to_string(compared) +
                                                   " things stand elsewhere in the Test game than in the editor");
        v.Expect(CliffCells(game).size() == pieces, "the Test game lacks the editor's cliffs");
        if (const char* wrong = CliffsJoin(game)) v.Fail(std::string("the editor's cliffs in the Test game: ") + wrong);
    } else {
        v.Fail("the editor's scenario makes no game: " + error);
    }

    // A random map: Arabia, the smallest, for the scenario's players -- its
    // world turned back into the file, and onto the grid again, its cliffs
    // still joining.
    s.map_kind = 2;
    s.map_size = 0;
    s.map_type = 0;
    s.seed_text = "7";
    Editor::Generate(s);
    size_t made = 0, centres = 0;
    for (const auto& p : s.doc.players) {
        made += p.units.size();
        for (const auto& u : p.units) centres += u.unit_id == tc ? 1 : 0;
    }
    v.Expect(s.doc.width == 120 && made > 100 && centres >= 2 && s.refs.size() == made,
             "a random map generated has " + std::to_string(made) + " things, " + std::to_string(centres) +
                 " Town Centers, " + std::to_string(s.refs.size()) + " in the world");
    v.Expect(!CliffCells(*s.sim).empty(), "a random Arabia generated has no cliffs");
    if (const char* wrong = CliffsJoin(*s.sim)) v.Fail(std::string("a random map's cliffs in the editor: ") + wrong);
    v.Expect(Editor::SaveTo(s, path) && Scenario::Load(path, -1, back, &error) && back.width == 120,
             "a random map generated does not save");
    std::filesystem::remove(path);
    // The colours the world set are the editor's, not a game's.
    std::array<int, 16> none{};
    none.fill(-1);
    HAL::GraphicCache::Get().SetPlayerColours(none);
    Core::ConfigManager::Get().SetPlayerColours(none);
}

// Map Copy on DEMO.SCN: an area with things in it copied, its terrain,
// elevation and things; the copy turned right, a corner tile going to the
// other side; the copy laid elsewhere with its things given to player 3, but
// Gaia's kept Gaia's, each with a new id, and the world built again to show
// them.
void MapCopying(Verdict& v) {
    using Core::Fixed32;
    Editor::State s;
    std::string error;
    if (!Scenario::Load(kDemo, -1, s.doc, &error)) {
        v.Fail("DEMO.SCN unread for Map Copy: " + error);
        return;
    }
    Editor::Layout(s);
    Editor::BuildWorld(s, false);
    // An area round player 1's first unit, and the ground there made uneven
    // so that a turn shows.
    const Scenario::Unit* first = s.doc.players[1].units.empty() ? nullptr : &s.doc.players[1].units.front();
    if (first == nullptr) {
        v.Fail("DEMO.SCN has no unit of player 1 to copy");
        return;
    }
    const int cx = first->x.ToInt(), cy = first->y.ToInt();
    s.has_area = true;
    s.area_x0 = std::max(0, cx - 3);
    s.area_y0 = std::max(0, cy - 2);
    s.area_x1 = std::min(s.doc.width - 1, cx + 3);
    s.area_y1 = std::min(s.doc.height - 1, cy + 2);
    const int corner = s.area_y0 * s.doc.width + s.area_x0;
    s.doc.terrain[static_cast<size_t>(corner)] = 22;
    Editor::CopyArea(s);
    const auto& c = s.copied;
    const int w = s.area_x1 - s.area_x0 + 1, h = s.area_y1 - s.area_y0 + 1;
    int things = 0, gaia = 0;
    for (int p = 0; p < Core::kMaxPlayers; ++p) {
        for (const auto& u : s.doc.players[p].units) {
            if (u.garrisoned_in >= 0 || u.x < Fixed32(s.area_x0) || u.x >= Fixed32(s.area_x1 + 1) ||
                u.y < Fixed32(s.area_y0) || u.y >= Fixed32(s.area_y1 + 1)) {
                continue;
            }
            ++things;
            gaia += p == 0 ? 1 : 0;
        }
    }
    v.Expect(c.w == w && c.h == h && static_cast<int>(c.terrain.size()) == w * h && c.terrain[0] == 22 &&
                 static_cast<int>(c.units.size()) == things && things > 0,
             "Map Copy did not copy the area's ground and things");
    Editor::TurnCopy(s, 1);
    v.Expect(c.w == h && c.h == w && c.terrain[static_cast<size_t>(h - 1)] == 22,
             "Map Copy's Rotate Right did not turn the copy");
    size_t before = 0;
    for (const auto& p : s.doc.players) before += p.units.size();
    const uint32_t next_id = s.doc.next_unit_id;
    s.copy_change = 1;
    s.copy_player = 3;
    const int to_x = s.doc.width / 2, to_y = s.doc.height / 2;
    Editor::LayCopy(s, to_x, to_y);
    size_t after = 0, theirs = 0;
    for (int p = 0; p < Core::kMaxPlayers; ++p) {
        after += s.doc.players[p].units.size();
        for (const auto& u : s.doc.players[p].units) {
            if (static_cast<uint32_t>(u.id) >= next_id) theirs += p == 3 ? 1 : 0;
        }
    }
    const int x0 = to_x - c.w / 2, y0 = to_y - c.h / 2;
    v.Expect(s.doc.terrain[static_cast<size_t>(y0 * s.doc.width + x0 + h - 1)] == 22,
             "Map Copy did not lay the copy's ground");
    v.Expect(after == before + static_cast<size_t>(things) && theirs == static_cast<size_t>(things - gaia) &&
                 s.doc.next_unit_id == next_id + static_cast<uint32_t>(things),
             "Map Copy did not lay the copy's things as player 3's with new ids, Gaia's kept");
    v.Expect(s.refs.size() == after, "the editor's world was not built again with the copy's things");
}

} // namespace

bool CheckEditor() {
    Verdict v;
    int files = 0, identical = 0;
    size_t packed = 0, written = 0;
    std::vector<uint8_t> bytes, demo;
    std::string error;
    if (!ReadBytes(kDemo, -1, demo, error)) {
        v.Fail("DEMO.SCN unread: " + error);
    } else {
        ++files;
        RoundTrip(v, demo, "DEMO.SCN", identical, packed, written);
    }
    for (const char* path : kCampaigns) {
        Scenario::Campaign campaign;
        if (!Scenario::LoadCampaign(path, campaign, &error)) {
            v.Fail(std::string(path) + " unread: " + error);
            continue;
        }
        for (size_t i = 0; i < campaign.scenarios.size(); ++i) {
            if (!ReadBytes(path, static_cast<int>(i), bytes, error)) {
                v.Fail(campaign.scenarios[i].file + " unread: " + error);
                continue;
            }
            ++files;
            RoundTrip(v, bytes, campaign.scenarios[i].file, identical, packed, written);
        }
    }
    v.Expect(files == 32 && identical == 32,
             std::to_string(identical) + " of " + std::to_string(files) + " scenarios written back identical");
    if (!demo.empty()) Edits(v, demo);
    NewFile(v);
    Editing(v);
    MapCopying(v);
    if (v.ok()) {
        std::printf("[editor] %d scenarios written back byte-identical inflated; %zu bytes packed by the "
                    "editor, %zu by the writer\n",
                    identical, packed, written);
    }
    std::printf("editor=%s\n", v.ok() ? "ok" : v.why.c_str());
    return v.ok();
}

} // namespace Verify
