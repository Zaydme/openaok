// The scenario editor's file and the world that shows it. Every edit goes to
// both: the file (what Save writes) and the world (what the view draws). The
// world is built and changed only through the modules a scenario game uses
// (ecs/ScenarioWorld, ecs/Buildings), and is never run.
#include <algorithm>
#include <array>
#include <cmath>
#include <ctime>
#include <filesystem>
#include <iostream>
#include <queue>

#include "core/Cliffs.h"
#include "core/ConfigManager.h"
#include "core/Random.h"
#include "core/MapGenerator.h"
#include "core/Rules.h"
#include "core/Strings.h"
#include "ecs/Buildings.h"
#include "ecs/ScenarioWorld.h"
#include "ecs/Triggers.h"
#include "ecs/components/Building.h"
#include "ecs/components/Garrison.h"
#include "ecs/components/Identity.h"
#include "ecs/components/Mortal.h"
#include "ecs/components/Owner.h"
#include "ecs/components/PartOf.h"
#include "ecs/components/Sprite.h"
#include "ecs/components/Transform.h"
#include "editor/EditorState.h"
#include "hal/GraphicCache.h"
#include "rendering/EntityRenderer.h"
#include "rendering/TerrainRenderer.h"
#include "rendering/UIRenderer.h"

namespace Editor {

namespace C = ECS::Components;
namespace R = Core::Rules;
using Core::Fixed32;

// The Terrain panel's list, in its order: dat terrain id, name string id.
const PaintTerrain kPaintTerrains[] = {
    {0, 10621},  {6, 10622},  {10, 10623}, {1, 10624},  {24, 10645}, {13, 10625}, {20, 10626},
    {19, 10628}, {4, 10627},  {22, 10629}, {23, 10644}, {12, 10640}, {9, 10641},  {11, 10642},
    {3, 10643},  {25, 10646}, {14, 10648}, {18, 10649}, {5, 10650},
};
const int kPaintTerrainCount = static_cast<int>(sizeof(kPaintTerrains) / sizeof(kPaintTerrains[0]));

namespace {

const Fixed32 kHalf = Fixed32(1) / Fixed32(2);

// The tree each forest terrain grows, one per tile, centred (the dat's
// Terrain.TerrainUnitID, density 1000, centering 1).
struct Grows {
    int dat;
    int tree;
};
constexpr Grows kGrows[] = {{10, 411}, {13, 351}, {18, 348}, {19, 350}, {20, 349}};
constexpr int kTreeClass = 15;
constexpr int kTypeCreatable = 70;
constexpr int kTypeBuilding = 80;

int TreeFor(int dat_terrain) {
    for (const Grows& g : kGrows) {
        if (g.dat == dat_terrain) return g.tree;
    }
    return -1;
}

size_t Index(const Scenario::File& f, int x, int y) {
    return static_cast<size_t>(y) * static_cast<size_t>(f.width) + static_cast<size_t>(x);
}

bool InMap(const Scenario::File& f, int x, int y) { return x >= 0 && y >= 0 && x < f.width && y < f.height; }

Scenario::Unit* FindUnit(Scenario::File& f, const Ref& ref, size_t* at = nullptr) {
    auto& units = f.players[ref.player].units;
    for (size_t i = 0; i < units.size(); ++i) {
        if (units[i].id == ref.id) {
            if (at != nullptr) *at = i;
            return &units[i];
        }
    }
    return nullptr;
}

// How many tiles across a building stands, or 0 for anything else -- a
// trebuchet among the rest, a unit (Rules::IsBuilding).
int FootprintOf(int unit_id) {
    const R::UnitRules* rules = R::Unit(unit_id);
    if (rules == nullptr || !R::IsBuilding(*rules)) return 0;
    const auto* ui = Core::ConfigManager::Get().GetUnitInterface(unit_id);
    return ui != nullptr ? ui->footprint : 0;
}

// Snaps a file coordinate: units, trees and odd-sized buildings to the middle
// of the tile, even-sized buildings to the nearest tile corner.
Fixed32 Snap(float file, int footprint) {
    if (footprint > 0 && footprint % 2 == 0) return Fixed32(static_cast<int>(std::floor(file + 0.5f)));
    return Fixed32(static_cast<int>(std::floor(file))) + kHalf;
}

// The file's place for a point of the world, snapped.
void SnapAt(const State& s, float wx, float wy, int footprint, Fixed32& x, Fixed32& y) {
    float fx = 0, fy = 0;
    FileOf(s, wx, wy, fx, fy);
    x = Snap(fx, footprint);
    y = Snap(fy, footprint);
}

entt::entity PlaceInWorld(State& s, const Scenario::Unit& unit, int player) {
    auto& reg = s.sim->GetRegistry();
    const entt::entity e = ECS::ScenarioWorld::Place(reg, s.sim->GetGrid(), unit.unit_id,
                                                     static_cast<Core::PlayerId>(player), WorldOf(s, unit.x, unit.y),
                                                     unit.rotation, unit.frame);
    if (e != entt::null) s.refs[static_cast<uint32_t>(e)] = Ref{player, unit.id};
    return e;
}

// The world's thing for one of the file's, or null.
entt::entity EntityOf(const State& s, int player, int32_t id) {
    for (const auto& [key, ref] : s.refs) {
        if (ref.player == player && ref.id == id) return static_cast<entt::entity>(key);
    }
    return entt::null;
}

void RemoveFromWorld(State& s, entt::entity e) {
    auto& reg = s.sim->GetRegistry();
    if (!reg.valid(e)) return;
    s.refs.erase(static_cast<uint32_t>(e));
    if (reg.all_of<C::Building>(e)) {
        ECS::Buildings::Destroy(reg, s.sim->GetGrid(), e, false);
        // Destroy leaves the entity for its rubble; the editor removes it.
        if (reg.valid(e)) reg.destroy(e);
        return;
    }
    ECS::ScenarioWorld::Remove(reg, s.sim->GetGrid(), e);
}

// Replaces any tree on a tile, in the file and the world, with the tree its
// new terrain grows (if any).
void Regrow(State& s, int x, int y, int dat_terrain) {
    auto& reg = s.sim->GetRegistry();
    std::vector<entt::entity> gone;
    for (const auto& [key, ref] : s.refs) {
        if (ref.player != 0) continue;
        const auto e = static_cast<entt::entity>(key);
        if (!reg.valid(e)) continue;
        const auto* id = reg.try_get<C::Identity>(e);
        const auto* at = reg.try_get<C::Transform>(e);
        if (id == nullptr || at == nullptr) continue;
        const R::UnitRules* rules = R::Unit(id->unit_id);
        if (rules == nullptr || rules->cls != kTreeClass) continue;
        if (Physics::TileOf(at->position.x) == x && Physics::TileOf(at->position.y) == y) gone.push_back(e);
    }
    for (entt::entity e : gone) {
        const Ref ref = s.refs[static_cast<uint32_t>(e)];
        size_t i = 0;
        if (FindUnit(s.doc, ref, &i) != nullptr) {
            auto& units = s.doc.players[0].units;
            units.erase(units.begin() + static_cast<long>(i));
        }
        RemoveFromWorld(s, e);
    }
    const int tree = TreeFor(dat_terrain);
    if (tree < 0) return;
    // The tile's middle, in the file's frame.
    const auto [fx, fy] = FileTile(s, x, y);
    Scenario::Unit unit;
    unit.x = Fixed32(fx) + kHalf;
    unit.y = Fixed32(fy) + kHalf;
    unit.unit_id = tree;
    unit.state = 2;
    const auto art = Core::ConfigManager::Get().GetObjectArt(tree);
    const int shapes = std::max(1, art.angle_count);
    unit.frame = static_cast<int32_t>((static_cast<uint32_t>(fx) * 7919u + static_cast<uint32_t>(fy) * 104729u) %
                                      static_cast<uint32_t>(shapes));
    unit.rotation = Fixed32(unit.frame);
    unit.id = static_cast<int32_t>(s.doc.next_unit_id++);
    s.doc.players[0].units.push_back(unit);
    PlaceInWorld(s, unit, 0);
}

// Sets the terrain of a grid tile and of the file tile it maps to.
void SetGround(State& s, int x, int y, int dat_terrain) {
    auto& grid = s.sim->GetGrid();
    const auto [fx, fy] = FileTile(s, x, y);
    if (!grid.InBounds(x, y) || !InMap(s.doc, fx, fy)) return;
    s.doc.terrain[Index(s.doc, fx, fy)] = static_cast<uint8_t>(dat_terrain);
    const Core::TerrainType t = ECS::ScenarioWorld::TerrainOf(dat_terrain);
    grid.SetTerrain(x, y, t);
    Core::MapGenerator::Paint(grid, x, y, t);
    grid.Refresh(x, y);
    Regrow(s, x, y, dat_terrain);
}

// Raises or lowers the ground in the brush to `level`, and the ground round
// it as far as needed for no two neighbours to differ by more than one. The
// brush's square is the grid's tiles (x0, y0) to (x1, y1); the slopes are
// worked in the file.
void SetElevation(State& s, int gx0, int gy0, int gx1, int gy1, int level) {
    const auto [ax, ay] = FileTile(s, gx0, gy0);
    const auto [bx, by] = FileTile(s, gx1, gy1);
    const int x0 = std::min(ax, bx), x1 = std::max(ax, bx), y0 = std::min(ay, by), y1 = std::max(ay, by);
    std::queue<std::pair<int, int>> changed;
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            if (!InMap(s.doc, x, y)) continue;
            uint8_t& e = s.doc.elevation[Index(s.doc, x, y)];
            if (e == level) continue;
            e = static_cast<uint8_t>(level);
            changed.emplace(x, y);
        }
    }
    while (!changed.empty()) {
        const auto [x, y] = changed.front();
        changed.pop();
        const int here = s.doc.elevation[Index(s.doc, x, y)];
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                const int nx = x + dx, ny = y + dy;
                if ((dx | dy) == 0 || !InMap(s.doc, nx, ny)) continue;
                uint8_t& n = s.doc.elevation[Index(s.doc, nx, ny)];
                if (n > here + 1) n = static_cast<uint8_t>(here + 1);
                else if (n + 1 < here) n = static_cast<uint8_t>(here - 1);
                else continue;
                changed.emplace(nx, ny);
            }
        }
    }
    auto& grid = s.sim->GetGrid();
    for (int y = 0; y < s.doc.height; ++y) {
        for (int x = 0; x < s.doc.width; ++x) {
            const auto [gx, gy] = GridTile(s, x, y);
            if (grid.InBounds(gx, gy)) grid.GetTileMut(gx, gy).elevation = s.doc.elevation[Index(s.doc, x, y)];
        }
    }
    Rendering::TerrainRenderer::InvalidateChunks();
}

void Erase(State& s, int x0, int y0, int x1, int y1, bool buildings) {
    auto& reg = s.sim->GetRegistry();
    std::vector<entt::entity> gone;
    for (const auto& [key, ref] : s.refs) {
        const auto e = static_cast<entt::entity>(key);
        if (!reg.valid(e)) continue;
        const auto* at = reg.try_get<C::Transform>(e);
        const auto* id = reg.try_get<C::Identity>(e);
        if (at == nullptr || id == nullptr) continue;
        const R::UnitRules* rules = R::Unit(id->unit_id);
        if (rules == nullptr || R::IsBuilding(*rules) != buildings) continue;
        const int x = Physics::TileOf(at->position.x), y = Physics::TileOf(at->position.y);
        if (x >= x0 && x <= x1 && y >= y0 && y <= y1) gone.push_back(e);
    }
    for (entt::entity e : gone) Delete(s, e);
}

} // namespace

ECS::Components::Vector2D WorldOf(const State& s, Fixed32 x, Fixed32 y) {
    return ECS::ScenarioWorld::FromFile(x, y, s.doc.width);
}

void FileOf(const State& s, float wx, float wy, float& fx, float& fy) {
    // The inverse of FromFile: the grid's x is the file's y, half a tile
    // back; its y is the file's x counted back from the far edge.
    fx = static_cast<float>(s.doc.width) - wy - 0.5f;
    fy = wx + 0.5f;
}

std::pair<int, int> FileTile(const State& s, int grid_x, int grid_y) {
    return {s.doc.width - 1 - grid_y, grid_x};
}

std::pair<int, int> GridTile(const State& s, int file_x, int file_y) {
    return ECS::ScenarioWorld::TileFromFile(file_x, file_y, s.doc.width);
}

Scenario::File NewFile() {
    // A new scenario: 144 by 144 tiles of grass, eight players named
    // "Player 1" ... (strings 10341 on), civilisations 1 to 8 in turn.
    Scenario::File f;
    f.version = "1.18";
    f.player_data_version = 120;
    f.trigger_version = 16;
    f.width = 144;
    f.height = 144;
    f.terrain.assign(static_cast<size_t>(f.width * f.height), 0);
    f.elevation.assign(f.terrain.size(), 0);
    f.enabled_players = Scenario::kPlayers;
    f.next_unit_id = 0;
    const Fixed32 middle = Fixed32(f.width / 2) + kHalf;
    for (int p = 1; p <= Scenario::kPlayers; ++p) {
        Scenario::Player& player = f.players[p];
        player.active = true;
        player.human = p == 1;
        player.civ = p;
        player.name = Core::Strings::Get(10340 + p);
        player.food = 200;
        player.wood = 200;
        player.gold = 100;
        player.stone = 200;
        player.starting_age = 0;
        player.colour = p - 1;
        player.camera_x = middle;
        player.camera_y = middle;
        for (int q = 0; q < Core::kMaxPlayers; ++q) player.stance[q] = q == p ? 0 : 3;
    }
    f.camera_x = f.width / 2;
    f.camera_y = f.height / 2;
    f.victory.mode = Scenario::VictoryMode::Standard;
    f.victory.conquest = true;
    return f;
}

void BuildWorld(State& s, bool keep_view) {
    const float cx = s.view.camera_x, cy = s.view.camera_y;
    Core::GameSetup setup = Core::DefaultSetup();
    // Every seat is filled, so every player's units show in their
    // civilisation's art and colour.
    std::array<int, 16> colours{};
    colours.fill(-1);
    for (int p = 1; p <= Scenario::kPlayers; ++p) {
        const Scenario::Player& from = s.doc.players[p];
        Core::PlayerSetup& seat = setup.players[p];
        seat = Core::PlayerSetup{};
        seat.active = true;
        seat.civ = from.civ >= 1 ? from.civ : 1;
        seat.colour = from.colour >= 0 && from.colour < Scenario::kPlayers ? from.colour : p - 1;
        seat.starting_age = std::clamp(from.starting_age, 0, 4);
        colours[static_cast<size_t>(p)] = seat.colour;
    }
    setup.map_size = s.doc.width;
    setup.reveal = Core::Reveal::AllVisible;
    HAL::GraphicCache::Get().SetPlayerColours(colours);
    Core::ConfigManager::Get().SetPlayerColours(colours);

    s.sim = std::make_unique<ECS::Simulation>();
    s.sim->InitFromFile(setup, s.doc);
    s.refs.clear();
    s.moving = s.selected = entt::null;
    // Map the world's entities to the file's units, by the unit ids the
    // triggers record.
    if (const auto* triggers = ECS::Triggers::Read(s.sim->GetRegistry())) {
        for (const ECS::Triggers::Placed& placed : triggers->placed) {
            int player = 0;
            if (s.doc.FindUnit(placed.id, &player) != nullptr && placed.entity != entt::null) {
                s.refs[static_cast<uint32_t>(placed.entity)] = Ref{player, placed.id};
            }
        }
    }
    Rendering::TerrainRenderer::InvalidateChunks();
    Rendering::UIRenderer::NewGame();
    Rendering::EntityRenderer::ClearMarkers();
    if (keep_view) {
        s.view.camera_x = cx;
        s.view.camera_y = cy;
    } else {
        // The view the file opens on, a tile of the file.
        const int x = s.doc.camera_x >= 0 ? s.doc.camera_x : s.doc.width / 2;
        const int y = s.doc.camera_y >= 0 ? s.doc.camera_y : s.doc.height / 2;
        const auto [gx, gy] = GridTile(s, x, y);
        CentreOn(s, static_cast<float>(gx), static_cast<float>(gy));
    }
    FillUnitList(s);
}

void Paint(State& s, int tx, int ty) {
    if (!s.sim) return;
    const int reach = s.brush_size - 1;
    const int x0 = tx - reach, y0 = ty - reach, x1 = tx + reach, y1 = ty + reach;
    switch (s.brush) {
    case kBrushTerrain: {
        const int dat = kPaintTerrains[std::clamp(s.terrain_choice, 0, kPaintTerrainCount - 1)].dat;
        for (int y = y0; y <= y1; ++y) {
            for (int x = x0; x <= x1; ++x) SetGround(s, x, y, dat);
        }
        s.sim->GetGrid().WalkabilityChanged();
        break;
    }
    case kBrushElevation:
        SetElevation(s, x0, y0, x1, y1, std::clamp(s.elevation_choice, 0, 6));
        break;
    case kBrushErase:
        Erase(s, x0, y0, x1, y1, s.erase_choice == 1);
        break;
    default:
        return;
    }
    s.dirty = true;
}

bool PlaceAt(const State& s, float wx, float wy, Fixed32& x, Fixed32& y, C::Vector2D& world, int& footprint) {
    if (s.unit_choice <= 0 || s.unit_choice > static_cast<int>(s.unit_ids.size())) return false;
    const int unit_id = s.unit_ids[static_cast<size_t>(s.unit_choice - 1)];
    footprint = FootprintOf(unit_id);
    SnapAt(s, wx, wy, footprint, x, y);
    world = WorldOf(s, x, y);
    return true;
}

void Place(State& s, float wx, float wy) {
    if (!s.sim || s.unit_choice <= 0 || s.unit_choice > static_cast<int>(s.unit_ids.size())) return;
    const int unit_id = s.unit_ids[static_cast<size_t>(s.unit_choice - 1)];
    const R::UnitRules* rules = R::Unit(unit_id);
    if (rules == nullptr) return;
    const int footprint = FootprintOf(unit_id);
    Scenario::Unit unit;
    SnapAt(s, wx, wy, footprint, unit.x, unit.y);
    unit.unit_id = unit_id;
    unit.state = 2;
    const auto& grid = s.sim->GetGrid();
    // The grid's tile it stands on, as ScenarioWorld::Place will put it there.
    const C::Vector2D at = WorldOf(s, unit.x, unit.y);
    const int tile_x = Physics::TileOf(at.x);
    const int tile_y = Physics::TileOf(at.y);
    if (!grid.InBounds(tile_x, tile_y)) return;
    // Refused where it may not stand.
    if (footprint > 0) {
        if (!ECS::Buildings::CanPlace(grid, unit_id, tile_x, tile_y)) return;
    } else if (rules->type == kTypeCreatable || R::PackingSiege(*rules)) {
        const auto [fx, fy] = FileTile(s, tile_x, tile_y);
        const int dat = s.doc.terrain[Index(s.doc, fx, fy)];
        if (grid.IsBlocked(tile_x, tile_y) || !R::Passable(rules->terrain_restriction, dat)) return;
    } else if (grid.IsBlocked(tile_x, tile_y)) {
        return;
    }
    const auto art = Core::ConfigManager::Get().GetObjectArt(unit_id);
    if (art.Valid() && !art.Animated()) {
        // A still object's shape: the next of its shapes.
        unit.frame = static_cast<int32_t>(s.doc.next_unit_id % static_cast<uint32_t>(std::max(1, art.angle_count)));
        unit.rotation = Fixed32(unit.frame);
    } else {
        // A unit's facing, an eighth of a turn at a time, in radians.
        unit.rotation = Fixed32(static_cast<float>(s.unit_rotation) * 0.785398163f);
    }
    unit.id = static_cast<int32_t>(s.doc.next_unit_id++);
    s.doc.players[s.unit_player].units.push_back(unit);
    PlaceInWorld(s, unit, s.unit_player);
    s.dirty = true;
}

entt::entity ThingAt(State& s, float wx, float wy) {
    if (!s.sim) return entt::null;
    auto& reg = s.sim->GetRegistry();
    entt::entity best = entt::null;
    float best_d = 0.0f;
    for (const auto& [key, ref] : s.refs) {
        const auto e = static_cast<entt::entity>(key);
        if (!reg.valid(e)) continue;
        const auto* at = reg.try_get<C::Transform>(e);
        const auto* id = reg.try_get<C::Identity>(e);
        if (at == nullptr || id == nullptr) continue;
        float cx = at->position.x.ToFloat(), cy = at->position.y.ToFloat();
        float reach = 0.75f;
        const int footprint = FootprintOf(id->unit_id);
        if (footprint > 0) {
            reach = footprint * 0.5f;
            if (footprint % 2 == 0) { cx -= 0.5f; cy -= 0.5f; }
        }
        const float dx = cx - wx, dy = cy - wy;
        if (std::abs(dx) > reach || std::abs(dy) > reach) continue;
        const float d = dx * dx + dy * dy;
        if (best == entt::null || d < best_d) {
            best = e;
            best_d = d;
        }
    }
    return best;
}

void Delete(State& s, entt::entity e) {
    if (!s.sim || e == entt::null) return;
    const auto it = s.refs.find(static_cast<uint32_t>(e));
    if (it == s.refs.end()) return;
    const Ref ref = it->second;
    size_t i = 0;
    if (FindUnit(s.doc, ref, &i) != nullptr) {
        auto& units = s.doc.players[ref.player].units;
        units.erase(units.begin() + static_cast<long>(i));
    }
    // Units garrisoned in it are released in the file.
    for (int p = 0; p < Core::kMaxPlayers; ++p) {
        for (auto& unit : s.doc.players[p].units) {
            if (unit.garrisoned_in == ref.id) unit.garrisoned_in = -1;
        }
    }
    RemoveFromWorld(s, e);
    if (s.selected == e) s.selected = entt::null;
    s.dirty = true;
}

void Rotate(State& s, entt::entity e) {
    if (!s.sim || e == entt::null) return;
    const auto it = s.refs.find(static_cast<uint32_t>(e));
    if (it == s.refs.end()) return;
    const Ref ref = it->second;
    Scenario::Unit* unit = FindUnit(s.doc, ref);
    if (unit == nullptr) return;
    const auto art = Core::ConfigManager::Get().GetObjectArt(unit->unit_id);
    if (art.Valid() && !art.Animated()) {
        // A still object turns to its next shape.
        unit->frame = (unit->frame + 1) % std::max(1, art.angle_count);
        unit->rotation = Fixed32(unit->frame);
    } else if (FootprintOf(unit->unit_id) == 0) {
        // A unit an eighth of a turn.
        const int k = (static_cast<int>(std::lround(unit->rotation.ToFloat() / 0.785398163f)) + 1) % 8;
        unit->rotation = Fixed32(static_cast<float>(k) * 0.785398163f);
    } else {
        return;
    }
    const Scenario::Unit copy = *unit;
    RemoveFromWorld(s, e);
    PlaceInWorld(s, copy, ref.player);
    s.dirty = true;
}

void CopyArea(State& s) {
    if (!s.has_area) return;
    const int x0 = std::min(s.area_x0, s.area_x1), x1 = std::max(s.area_x0, s.area_x1);
    const int y0 = std::min(s.area_y0, s.area_y1), y1 = std::max(s.area_y0, s.area_y1);
    State::Copied c;
    c.w = x1 - x0 + 1;
    c.h = y1 - y0 + 1;
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            const bool in = InMap(s.doc, x, y);
            c.terrain.push_back(in ? s.doc.terrain[Index(s.doc, x, y)] : 0);
            c.elevation.push_back(in ? s.doc.elevation[Index(s.doc, x, y)] : 0);
        }
    }
    const Fixed32 fx0(x0), fy0(y0), fx1(x1 + 1), fy1(y1 + 1);
    for (int p = 0; p < Core::kMaxPlayers; ++p) {
        for (const Scenario::Unit& unit : s.doc.players[p].units) {
            if (unit.garrisoned_in >= 0) continue;
            if (unit.x < fx0 || unit.x >= fx1 || unit.y < fy0 || unit.y >= fy1) continue;
            Scenario::Unit moved = unit;
            moved.x = unit.x - fx0;
            moved.y = unit.y - fy0;
            c.units.emplace_back(p, moved);
        }
    }
    s.copied = std::move(c);
}

void TurnCopy(State& s, int how) {
    State::Copied& c = s.copied;
    if (c.w <= 0 || c.h <= 0) return;
    const bool turn = how == 0 || how == 1;
    const int w = turn ? c.h : c.w, h = turn ? c.w : c.h;
    std::vector<uint8_t> terrain(c.terrain.size()), elevation(c.elevation.size());
    for (int y = 0; y < c.h; ++y) {
        for (int x = 0; x < c.w; ++x) {
            int nx = x, ny = y;
            switch (how) {
            case 0: nx = y; ny = c.w - 1 - x; break;
            case 1: nx = c.h - 1 - y; ny = x; break;
            case 2: nx = c.w - 1 - x; break;
            default: ny = c.h - 1 - y; break;
            }
            terrain[static_cast<size_t>(ny * w + nx)] = c.terrain[static_cast<size_t>(y * c.w + x)];
            elevation[static_cast<size_t>(ny * w + nx)] = c.elevation[static_cast<size_t>(y * c.w + x)];
        }
    }
    const Fixed32 cw(c.w), ch(c.h);
    for (auto& [player, unit] : c.units) {
        const Fixed32 x = unit.x, y = unit.y;
        switch (how) {
        case 0: unit.x = y; unit.y = cw - x; break;
        case 1: unit.x = ch - y; unit.y = x; break;
        case 2: unit.x = cw - x; break;
        default: unit.y = ch - y; break;
        }
        for (uint32_t& w0 : unit.written) w0 = 0;
        // A unit faces round with the ground, by eighths of a turn; a still
        // object keeps its shape and a building its place.
        const auto art = Core::ConfigManager::Get().GetObjectArt(unit.unit_id);
        if ((art.Valid() && !art.Animated()) || FootprintOf(unit.unit_id) != 0) continue;
        int k = static_cast<int>(std::lround(unit.rotation.ToFloat() / 0.785398163f));
        switch (how) {
        case 0: k -= 2; break;
        case 1: k += 2; break;
        case 2: k = 4 - k; break;
        default: k = -k; break;
        }
        k = ((k % 8) + 8) % 8;
        unit.rotation = Fixed32(static_cast<float>(k) * 0.785398163f);
    }
    c.w = w;
    c.h = h;
    c.terrain = std::move(terrain);
    c.elevation = std::move(elevation);
}

void LayCopy(State& s, int file_x, int file_y) {
    const State::Copied& c = s.copied;
    if (c.w <= 0 || c.h <= 0) return;
    const int x0 = file_x - c.w / 2, y0 = file_y - c.h / 2;
    for (int y = 0; y < c.h; ++y) {
        for (int x = 0; x < c.w; ++x) {
            if (!InMap(s.doc, x0 + x, y0 + y)) continue;
            s.doc.terrain[Index(s.doc, x0 + x, y0 + y)] = c.terrain[static_cast<size_t>(y * c.w + x)];
            s.doc.elevation[Index(s.doc, x0 + x, y0 + y)] = c.elevation[static_cast<size_t>(y * c.w + x)];
        }
    }
    const Fixed32 fx0(x0), fy0(y0);
    for (const auto& [player, unit] : c.units) {
        Scenario::Unit laid = unit;
        laid.x = unit.x + fx0;
        laid.y = unit.y + fy0;
        if (laid.x < Fixed32(0) || laid.y < Fixed32(0) || laid.x >= Fixed32(s.doc.width) ||
            laid.y >= Fixed32(s.doc.height)) {
            continue;
        }
        for (uint32_t& w0 : laid.written) w0 = 0;
        laid.id = static_cast<int32_t>(s.doc.next_unit_id++);
        laid.garrisoned_in = -1;
        // Gaia's things stay Gaia's when the copy is given to a player.
        const int to = s.copy_change == 1 && player != 0 ? s.copy_player : player;
        s.doc.players[to].units.push_back(laid);
    }
    BuildWorld(s, true);
    s.dirty = true;
}

void MoveTo(State& s, entt::entity e, float wx, float wy) {
    if (!s.sim || e == entt::null) return;
    const auto it = s.refs.find(static_cast<uint32_t>(e));
    if (it == s.refs.end()) return;
    const Ref ref = it->second;
    Scenario::Unit* unit = FindUnit(s.doc, ref);
    if (unit == nullptr) return;
    const int footprint = FootprintOf(unit->unit_id);
    Fixed32 x, y;
    SnapAt(s, wx, wy, footprint, x, y);
    if (x == unit->x && y == unit->y) return;
    const Scenario::Unit before = *unit;
    unit->x = x;
    unit->y = y;
    const Scenario::Unit copy = *unit;
    RemoveFromWorld(s, e);
    entt::entity moved = PlaceInWorld(s, copy, ref.player);
    if (moved == entt::null) {
        // Not there: back where it was.
        *FindUnit(s.doc, ref) = before;
        moved = PlaceInWorld(s, before, ref.player);
    }
    s.moving = moved;
    s.dirty = true;
}

namespace {

// The Cliffs brush. It works in the file's frame, on the lattice of 3x3-tile
// cells cliffs are laid on (core/Cliffs). A cell holds one piece, chosen by its
// links to the neighbouring cells: per side, whether the cliff shows its face
// there (1), its lip (-1), or no link (0).
constexpr int kCell = 3;
constexpr int kFirstCliff = 264;   // CLF01..CLF10
constexpr int kLastCliff = 273;
constexpr int kRandMax = 32767;
using Links = std::array<int8_t, 4>;
// Links run +x, +y, -x, -y.
constexpr int kStepX[4] = {1, 0, -1, 0};
constexpr int kStepY[4] = {0, 1, 0, -1};

int Opposite(int side) { return (side + 2) % 4; }

// The id of the file's cliff piece in a cell, or -1: one standing on the
// cell's middle four tiles.
int32_t CliffIn(const State& s, int cx, int cy) {
    if (cx < 0 || cy < 0 || kCell * cx + 2 > s.doc.width || kCell * cy + 2 > s.doc.height) return -1;
    for (const Scenario::Unit& u : s.doc.players[0].units) {
        if (u.unit_id < kFirstCliff || u.unit_id > kLastCliff) continue;
        const int tx = u.x.ToInt(), ty = u.y.ToInt();
        if (tx >= kCell * cx + 1 && tx <= kCell * cx + 2 && ty >= kCell * cy + 1 && ty <= kCell * cy + 2) return u.id;
    }
    return -1;
}

// A piece's links, by the drawing it shows; none for a cell with no piece.
Links LinksIn(State& s, int32_t id) {
    const Scenario::Unit* u = id >= 0 ? FindUnit(s.doc, Ref{0, id}) : nullptr;
    const Core::Cliffs::Piece* piece = u != nullptr ? Core::Cliffs::PieceDrawn(u->unit_id, u->frame) : nullptr;
    return piece != nullptr ? piece->links : Links{0, 0, 0, 0};
}

// A piece taken up, from the file and the world. Its tiles stay blocked in
// the world until the button comes up and the world is laid again.
void Uproot(State& s, int32_t id) {
    size_t i = 0;
    if (FindUnit(s.doc, Ref{0, id}, &i) != nullptr) {
        auto& units = s.doc.players[0].units;
        units.erase(units.begin() + static_cast<long>(i));
    }
    RemoveFromWorld(s, EntityOf(s, 0, id));
    s.cliffs_cut = true;
    s.dirty = true;
}

// Lays the piece for a cell's links, if one exists: at the cell's corner plus
// the piece's own offset; a straight uses its second drawing half the time.
void LayPiece(State& s, int cx, int cy, const Links& links) {
    const Core::Cliffs::Piece* piece = Core::Cliffs::PieceFor(links);
    if (piece == nullptr) return;
    int unit_id = piece->unit;
    int frame = piece->frame;
    if (piece->alt_unit >= 0 && 2 * static_cast<int>(s.cliff_rng.NextInt(kRandMax + 1)) > kRandMax) {
        unit_id = piece->alt_unit;
        frame = piece->alt_frame;
    }
    Scenario::Unit unit;
    unit.x = Fixed32(2 * kCell * cx + piece->x2) * kHalf;
    unit.y = Fixed32(2 * kCell * cy + piece->y2) * kHalf;
    unit.unit_id = unit_id;
    unit.state = 2;
    // The rotation holds the frame too.
    unit.frame = frame;
    unit.rotation = Fixed32(frame);
    unit.id = static_cast<int32_t>(s.doc.next_unit_id++);
    s.doc.players[0].units.push_back(unit);
    PlaceInWorld(s, unit, 0);
    s.dirty = true;
}

// The neighbour on one side of a cell lets go of it: laid again without its
// link back, so a straight becomes an end and an end goes.
void Unlink(State& s, int cx, int cy, int side) {
    const int nx = cx + kStepX[side], ny = cy + kStepY[side];
    const int32_t id = CliffIn(s, nx, ny);
    if (id < 0) return;
    Links links = LinksIn(s, id);
    Uproot(s, id);
    links[static_cast<size_t>(Opposite(side))] = 0;
    LayPiece(s, nx, ny, links);
}

// A cell's piece linked on `side`: with `value`, or, for 0, the
// face if a piece has it and the lip if not. `keep`, when another side, is
// the one link of the cell's that stays (the way the cliff came in); with
// none, a cell already linked twice keeps the first of its links a piece
// allows with the new one, and its other neighbours are let go. Returns the
// value linked.
int8_t Link(State& s, int cx, int cy, int side, int8_t value, int keep) {
    const int32_t id = CliffIn(s, cx, cy);
    Links links = LinksIn(s, id);
    const size_t at = static_cast<size_t>(side);
    if (links[at] != 0 && (value == 0 || value == links[at])) return links[at];
    if (id >= 0) Uproot(s, id);
    int first = -1, second = -1;
    int8_t first_value = 0, second_value = 0;
    if (keep != side && keep >= 0) {
        for (int d = 0; d < 4; ++d) {
            if (d == keep) continue;
            Unlink(s, cx, cy, d);
            links[static_cast<size_t>(d)] = 0;
        }
    } else {
        int others = 0;
        for (int d = 0; d < 4; ++d) others += d != side && links[static_cast<size_t>(d)] != 0 ? 1 : 0;
        if (others == 2) {
            for (int d = 0; d < 4; ++d) {
                int8_t& l = links[static_cast<size_t>(d)];
                if (d == side || l == 0) continue;
                if (first < 0) {
                    first = d;
                    first_value = l;
                } else {
                    second = d;
                    second_value = l;
                }
                l = 0;
            }
        }
    }
    auto valid = [&] { return Core::Cliffs::PieceFor(links) != nullptr; };
    auto let_go = [&] {
        for (int d = 0; d < 4; ++d) {
            if (d == side || links[static_cast<size_t>(d)] == 0) continue;
            Unlink(s, cx, cy, d);
            links[static_cast<size_t>(d)] = 0;
        }
    };
    int8_t linked = value;
    if (value != 0) {
        links[at] = value;
        for (const auto& [d, v] : {std::pair<int, int8_t>{first, first_value}, std::pair<int, int8_t>{second, second_value}}) {
            if (d < 0) continue;
            links[static_cast<size_t>(d)] = v;
            if (valid()) continue;
            links[static_cast<size_t>(d)] = 0;
            Unlink(s, cx, cy, d);
        }
        if (!valid()) let_go();
    } else {
        bool kept_first = false, kept_second = false, found = false;
        for (const int8_t v : {int8_t{1}, int8_t{-1}}) {
            links[at] = v;
            linked = v;
            if (first >= 0) {
                links[static_cast<size_t>(first)] = first_value;
                if ((kept_first = valid())) break;
                links[static_cast<size_t>(first)] = 0;
            }
            if (second >= 0) {
                links[static_cast<size_t>(second)] = second_value;
                if ((kept_second = valid())) break;
                links[static_cast<size_t>(second)] = 0;
            }
            if ((found = valid())) break;
        }
        if (!kept_first && !kept_second && !found) let_go();
        if (first >= 0 && !kept_first) Unlink(s, cx, cy, first);
        if (second >= 0 && !kept_second) Unlink(s, cx, cy, second);
    }
    LayPiece(s, cx, cy, links);
    return linked;
}

// Rubs out a cell's piece; its neighbours let go of it.
void EraseCell(State& s, int cx, int cy) {
    const int32_t id = CliffIn(s, cx, cy);
    if (id < 0) return;
    const Links links = LinksIn(s, id);
    for (int d = 0; d < 4; ++d) {
        if (links[static_cast<size_t>(d)] != 0) Unlink(s, cx, cy, d);
    }
    Uproot(s, id);
}

// The pointer on a tile of the file. From the cell the cliff last reached,
// the cliff goes on a cell across if the pointer has left that cell's
// columns, else a cell down; the last cell is linked toward the new one, face
// first, and the new one back to it the same way. It advances one cell per
// call and returns true while there is further to go, so a quick drag leaves
// no gap.
bool CliffPoint(State& s, int x, int y, bool erase) {
    int cx = x / kCell, cy = y / kCell;
    s.cliff_cell_x = cx;
    s.cliff_cell_y = cy;
    const int lx = s.cliff_last_x, ly = s.cliff_last_y;
    if (lx == cx && ly == cy) return false;
    if (kCell * cx + 2 >= s.doc.width || kCell * cy + 2 >= s.doc.height || cx < 0 || cy < 0) return false;
    if (lx != -1 && ly != -1) {
        const int dx = x - kCell * lx, dy = y - kCell * ly;
        if (dx >= 0 && dx < kCell) {
            if (dy >= 0 && dy < kCell) return false;
            cx = lx;
        } else {
            cy = ly;
        }
    }
    if (cx == lx && cy == ly) return false;
    if (erase) {
        EraseCell(s, cx, cy);
        return false;
    }
    if (lx == -1) {
        s.cliff_last_x = cx;
        s.cliff_last_y = cy;
        s.cliff_back = -1;
        return false;
    }
    const int nx = cx > lx ? lx + 1 : cx < lx ? lx - 1 : lx;
    const int ny = cx != lx ? ly : cy > ly ? ly + 1 : ly - 1;
    const int side = lx < nx ? 0 : lx > nx ? 2 : ly < ny ? 1 : 3;
    const int8_t value = Link(s, lx, ly, side, 0, s.cliff_back);
    s.cliff_back = Opposite(side);
    s.cliff_last_x = nx;
    s.cliff_last_y = ny;
    Link(s, nx, ny, s.cliff_back, value, -1);
    return true;
}

} // namespace

void CliffDown(State& s, int tx, int ty, bool erase) {
    if (!s.sim) return;
    const auto [fx, fy] = FileTile(s, tx, ty);
    if (!InMap(s.doc, fx, fy)) return;
    // A press in a cell other than the one the pointer was last in starts a
    // new cliff; in the same one, the last goes on from its end.
    // Rubbing out always starts afresh.
    if (erase || fx / kCell != s.cliff_cell_x || fy / kCell != s.cliff_cell_y) {
        s.cliff_last_x = s.cliff_last_y = -1;
        s.cliff_cell_x = s.cliff_cell_y = -1;
        s.cliff_back = -1;
    }
    s.laying_cliffs = !erase;
    s.erasing_cliffs = erase;
    CliffDrag(s, tx, ty);
}

void CliffDrag(State& s, int tx, int ty) {
    if (!s.sim || !(s.laying_cliffs || s.erasing_cliffs)) return;
    const auto [fx, fy] = FileTile(s, tx, ty);
    if (!InMap(s.doc, fx, fy)) return;
    for (int guard = 0; guard < 4 * kCell * 256 && CliffPoint(s, fx, fy, s.erasing_cliffs); ++guard) {
    }
}

void CliffUp(State& s) {
    const bool down = s.laying_cliffs || s.erasing_cliffs;
    s.laying_cliffs = s.erasing_cliffs = false;
    if (!down || !s.sim) return;
    if (s.cliffs_cut) {
        // The tiles of the pieces taken up are freed by laying the world
        // again from the file.
        s.cliffs_cut = false;
        BuildWorld(s, true);
    } else {
        s.sim->GetGrid().WalkabilityChanged();
    }
}

void Generate(State& s) {
    const int tiles = Core::kMapSizes[std::clamp(s.map_size, 0, 5)].tiles;
    if (s.map_kind == 0) {
        // A blank map: the size, all one terrain, nothing on it.
        const int dat = kPaintTerrains[std::clamp(s.default_terrain, 0, kPaintTerrainCount - 1)].dat;
        s.doc.width = s.doc.height = tiles;
        s.doc.terrain.assign(static_cast<size_t>(tiles * tiles), static_cast<uint8_t>(dat));
        s.doc.elevation.assign(s.doc.terrain.size(), 0);
        for (auto& player : s.doc.players) player.units.clear();
        if (TreeFor(dat) >= 0) {
            BuildWorld(s, false);
            for (int y = 0; y < tiles; ++y) {
                for (int x = 0; x < tiles; ++x) Regrow(s, x, y, dat);
            }
        }
    } else {
        // A random map: the map script run as for a game with the scenario's
        // players, and the result written into the file.
        static const char* kScripts[] = {"Arabia",      "Archipelago",   "Baltic",       "Black Forest",
                                         "Coastal",     "Continental",   "Crater Lake",  "Fortress",
                                         "Gold Rush",   "Highland",      "Islands",      "Mediterranean",
                                         "Migration",   "Rivers",        "Team Islands"};
        Core::GameSetup setup = Core::DefaultSetup();
        setup.map_script = kScripts[std::clamp(s.map_type, 0, 14)];
        setup.map_size = tiles;
        uint32_t seed = static_cast<uint32_t>(std::strtoul(s.seed_text.c_str(), nullptr, 10));
        if (s.map_kind == 1) {
            seed = static_cast<uint32_t>(std::time(nullptr)) ^ SDL_GetTicks();
            seed %= 100000u;
            s.seed_text = std::to_string(seed);
        }
        setup.seed = seed;
        for (int p = 1; p <= Scenario::kPlayers; ++p) {
            setup.players[p] = Core::PlayerSetup{};
            setup.players[p].active = s.doc.players[p].active;
            setup.players[p].civ = std::max(1, s.doc.players[p].civ);
        }
        // The generated world is in the grid's frame: everything is turned
        // back into the file's frame, and the world rebuilt from the file as
        // for any scenario.
        ECS::Simulation made;
        made.Init(setup);
        const auto& grid = made.GetGrid();
        auto& reg = made.GetRegistry();
        auto& cfg = Core::ConfigManager::Get();
        s.doc.width = grid.GetHeight();
        s.doc.height = grid.GetWidth();
        s.doc.terrain.resize(static_cast<size_t>(s.doc.width * s.doc.height));
        s.doc.elevation.resize(s.doc.terrain.size());
        for (int y = 0; y < s.doc.height; ++y) {
            for (int x = 0; x < s.doc.width; ++x) {
                const auto [gx, gy] = GridTile(s, x, y);
                const auto& tile = grid.GetTile(gx, gy);
                const int dat = cfg.GetTerrainArt(tile.terrain).dat_id;
                s.doc.terrain[Index(s.doc, x, y)] = static_cast<uint8_t>(dat >= 0 ? dat : 0);
                s.doc.elevation[Index(s.doc, x, y)] = static_cast<uint8_t>(std::clamp(tile.elevation, 0, 7));
            }
        }
        for (auto& player : s.doc.players) player.units.clear();
        auto things = reg.view<C::Identity, C::Transform>();
        for (auto e : things) {
            if (reg.any_of<C::PartOf, C::Dying, C::Garrisoned>(e)) continue;
            const int unit_id = things.get<C::Identity>(e).unit_id;
            const R::UnitRules* rules = R::Unit(unit_id);
            if (rules == nullptr || !rules->valid) continue;
            const auto* owner = reg.try_get<C::Owner>(e);
            const int player = owner != nullptr ? owner->player_id : 0;
            if (player < 0 || player >= Core::kMaxPlayers) continue;
            const auto& pos = things.get<C::Transform>(e).position;
            Scenario::Unit unit;
            // An even building's origin is the tile whose corner its centre
            // is, half a tile on from the centre both ways (ecs/Buildings).
            const int footprint = reg.all_of<C::Building>(e) ? FootprintOf(unit_id) : 0;
            const Fixed32 back = footprint > 0 && footprint % 2 == 0 ? kHalf : Fixed32(0);
            unit.x = Fixed32(s.doc.width) - (pos.y - back) - kHalf;
            unit.y = pos.x - back + kHalf;
            unit.unit_id = unit_id;
            unit.state = 2;
            if (const auto* sprite = reg.try_get<C::Sprite>(e); sprite != nullptr && !sprite->is_file_sprite) {
                unit.frame = sprite->slp_frame;
                unit.rotation = Fixed32(sprite->slp_frame);
            }
            unit.id = static_cast<int32_t>(s.doc.next_unit_id++);
            s.doc.players[player].units.push_back(unit);
        }
        // Each player's view on its start.
        size_t start = 0;
        for (int p = 1; p <= Scenario::kPlayers && start < made.player_starts.size(); ++p) {
            if (!s.doc.players[p].active) continue;
            const auto [fx, fy] = FileTile(s, made.player_starts[start].first, made.player_starts[start].second);
            s.doc.players[p].camera_x = Fixed32(fx) + kHalf;
            s.doc.players[p].camera_y = Fixed32(fy) + kHalf;
            if (p == 1) {
                s.doc.camera_x = fx;
                s.doc.camera_y = fy;
            }
            ++start;
        }
    }
    s.dirty = true;
    BuildWorld(s, false);
}

void FillUnitList(State& s) {
    // The list for the category: the dat's units in order, less those it
    // hides from the editor, sorted into the four lists by type and hero
    // flag, with fixed exceptions by unit id.
    static constexpr int kNotUnits[] = {285, 287, 288, 289, 290, 292, 294, 295, 296, 297, 298, 688};
    static constexpr int kUnitBuildings[] = {42, 331};                 // the trebuchet, set up and packed
    static constexpr int kHeroBuildings[] = {682, 730, 683, 729};
    static constexpr int kOtherBuildings[] = {445, 655, 696, 690, 684, 685, 624, 626, 625};
    static constexpr int kOtherUnits[] = {285, 688};
    auto in = [](int id, const auto& list) { return std::find(std::begin(list), std::end(list), id) != std::end(list); };
    s.unit_ids.clear();
    s.unit_names.clear();
    s.unit_names.push_back(Core::Strings::Get(10101));   // " <None> "
    const auto& units = R::Get().units;
    for (int id = 0; id < static_cast<int>(units.size()); ++id) {
        const R::UnitRules& u = units[static_cast<size_t>(id)];
        if (!u.valid || u.hide_in_editor) continue;
        bool listed = false;
        switch (s.category) {
        case kUnitsList:
            listed = (u.type == kTypeCreatable && !u.hero && !in(id, kNotUnits)) ||
                     (u.type == kTypeBuilding && in(id, kUnitBuildings));
            break;
        case kBuildingsList:
            listed = u.type == kTypeBuilding && !in(id, kUnitBuildings) && !in(id, kHeroBuildings) &&
                     !in(id, kOtherBuildings);
            break;
        case kHeroesList:
            listed = (u.type == kTypeCreatable && u.hero) || (u.type == kTypeBuilding && in(id, kHeroBuildings));
            break;
        default:
            listed = (u.type != kTypeCreatable && u.type != kTypeBuilding) ||
                     (u.type == kTypeCreatable && in(id, kOtherUnits)) ||
                     (u.type == kTypeBuilding && in(id, kOtherBuildings));
            break;
        }
        if (!listed) continue;
        s.unit_ids.push_back(id);
        const std::string& label = R::UnitLabel(id);
        s.unit_names.push_back(!label.empty() ? label : R::Get().unit_text[static_cast<size_t>(id)].name);
    }
    if (s.unit_choice >= static_cast<int>(s.unit_names.size())) s.unit_choice = 0;
}

bool SaveTo(State& s, const std::string& path, bool adopt) {
    if (path.empty()) return false;
    Scenario::File& f = s.doc;
    // What the header and the editor keep in step with the rest.
    f.header_instructions = f.texts[Scenario::kInstructions].text;
    int active = 0;
    for (int p = 1; p <= Scenario::kPlayers; ++p) active += f.players[p].active ? 1 : 0;
    f.enabled_players = active;
    f.original_name = std::filesystem::path(path).filename().string();
    f.save_time = static_cast<uint32_t>(std::time(nullptr));
    // The view the editor was left on, player 1's when the scenario opens.
    float wx = 0, wy = 0;
    const SDL_Rect& v = s.frame.view;
    if (WorldAt(s, v.x + v.w / 2, v.y + v.h / 2, wx, wy)) {
        const auto [fx, fy] = FileTile(s, static_cast<int>(std::lround(wx)), static_cast<int>(std::lround(wy)));
        f.camera_x = std::clamp(fx, 0, f.width - 1);
        f.camera_y = std::clamp(fy, 0, f.height - 1);
    }
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);
    std::string why;
    if (!Scenario::Save(path, f, &why)) {
        Notice(s, Core::Strings::Get(2403));
        std::cerr << "[Editor] " << path << ": " << why << std::endl;
        return false;
    }
    if (adopt) {
        s.path = path;
        s.dirty = false;
    }
    return true;
}

void SetPlayerCount(State& s, int count) {
    for (int p = 1; p <= Scenario::kPlayers; ++p) s.doc.players[p].active = p <= count;
    s.doc.enabled_players = count;
    s.dirty = true;
}

} // namespace Editor
