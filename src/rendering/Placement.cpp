#include "rendering/Placement.h"

#include <algorithm>
#include <cmath>

#include "core/ConfigManager.h"
#include "core/InputManager.h"
#include "ecs/BuildingGeometry.h"
#include "ecs/Buildings.h"
#include "ecs/Players.h"
#include "ecs/Techs.h"
#include "ecs/Walls.h"
#include "hal/Window.h"
#include "rendering/Ground.h"
#include "rendering/Renderer.h"

namespace Rendering::Placement {

void OriginTile(int unit_id, float wx, float wy, int& tile_x, int& tile_y) {
    const ECS::Buildings::Footprint f = ECS::Buildings::FootprintOf(ECS::Buildings::PlacedUnit(unit_id));
    const float mid_x = static_cast<float>(f.x.low + f.x.high) / 2.0f;
    const float mid_y = static_cast<float>(f.y.low + f.y.high) / 2.0f;
    tile_x = static_cast<int>(std::floor(wx - mid_x + 0.5f));
    tile_y = static_cast<int>(std::floor(wy - mid_y + 0.5f));
}

int BlockedColour(uint64_t game_ms, bool main) {
    const int step = static_cast<int>((game_ms >> 7) & 7);
    if (main) return step < 4 ? 0x22 + step : 0x2a - step;
    return step < 4 ? 0x24 + step : 0x2c - step;
}

bool Collect(ECS::Simulation& sim, const Renderer& renderer, std::vector<Piece>& pieces,
             Outline& outline) {
    pieces.clear();
    outline = Outline{};
    int button_id = -1;
    if (!Core::InputManager::Get().PlacingBuilding(button_id)) return false;

    auto& cfg = Core::ConfigManager::Get();
    // What the button actually puts up, and in whose art: the Town Center's
    // button places the Town Center, wearing the player's current age.
    const int unit_id = ECS::Buildings::PlacedUnit(button_id);
    const auto* ui = unit_id >= 0 ? cfg.GetUnitInterface(unit_id) : nullptr;
    if (!ui) return false;
    auto& registry = sim.GetRegistry();
    const Core::PlayerId viewer = Core::LocalPlayer();
    const int art_unit = ECS::Players::ArtOf(registry, viewer, unit_id);
    const auto& grid = sim.GetGrid();
    auto& graphics = HAL::GraphicCache::Get();

    int mx = 0;
    int my = 0;
    if (HAL::Window* window = HAL::MainWindow()) {
        window->MouseState(mx, my);
    } else {
        SDL_GetMouseState(&mx, &my);
    }
    const float inv_zoom = 1.0f / renderer.camera_zoom;
    const auto world = Ground::Pick(grid, renderer.camera_x + mx * inv_zoom,
                                    renderer.camera_y + my * inv_zoom);
    // The tile under the cursor: tiles are centred on whole numbers. A wall
    // is laid tile by tile; anything else is placed with its middle there.
    int tile_x = static_cast<int>(std::floor(world.first + 0.5f));
    int tile_y = static_cast<int>(std::floor(world.second + 0.5f));
    if (!ECS::Walls::IsWall(unit_id) && !ECS::Walls::IsGate(unit_id)) {
        OriginTile(unit_id, world.first, world.second, tile_x, tile_y);
    }
    // Drawn about the footprint's middle, as the finished building is.
    float mid_x = 0.0f, mid_y = 0.0f;
    {
        const ECS::Buildings::Footprint f = ECS::Buildings::FootprintOf(ECS::Buildings::PlacedUnit(unit_id));
        mid_x = static_cast<float>(f.x.low + f.x.high) / 2.0f;
        mid_y = static_cast<float>(f.y.low + f.y.high) / 2.0f;
    }

    auto add = [&](int slp, int frame, float wx, float wy, bool clear, bool main) {
        const HAL::GraphicAtlas* atlas = graphics.GetAtlas(static_cast<uint32_t>(slp));
        if (atlas == nullptr || atlas->base == nullptr || frame < 0 ||
            static_cast<size_t>(frame) >= atlas->frames.size() ||
            atlas->frames[static_cast<size_t>(frame)].rect.w == 0) {
            return;
        }
        Piece piece;
        piece.atlas = atlas;
        piece.player = graphics.GetPlayerLayer(static_cast<uint32_t>(slp), viewer);
        piece.frame = frame;
        piece.wx = wx;
        piece.wy = wy;
        piece.clear = clear;
        piece.main = main;
        pieces.push_back(piece);
    };

    if (ECS::Walls::IsWall(button_id) || ECS::Walls::IsGate(button_id)) {
        // A wall is previewed segment by segment along the line being dragged,
        // each the piece the line gives it and each checked on its own; a gate
        // as its arch and posts, turned to the wall under the cursor, the arch
        // the building's own piece.
        int from_x = tile_x, from_y = tile_y;
        Core::InputManager::Get().WallDrag(from_x, from_y);
        const auto ghosts = ECS::Walls::Ghost(registry, grid, viewer, button_id, from_x, from_y,
                                              tile_x, tile_y);
        const bool gate = ECS::Walls::IsGate(button_id);
        for (size_t i = 0; i < ghosts.size(); ++i) {
            const auto& ghost = ghosts[i];
            add(ghost.slp, ghost.frame, (Core::Fixed32(ghost.tile_x) + ghost.dx).ToFloat(),
                (Core::Fixed32(ghost.tile_y) + ghost.dy).ToFloat(), ghost.clear, gate && i == 0);
        }
    } else {
        // Whether the site is clear -- the same answer the order is checked
        // against, so the preview never says yes to a placement the order
        // will refuse.
        const bool clear = ECS::Walls::SiteOpen(registry, grid, viewer, button_id, tile_x, tile_y);
        // Every piece of a multi-part building: its own piece, then its
        // annexes.
        const auto* parts = cfg.GetAnnexes(art_unit);
        if (parts == nullptr) parts = cfg.GetAnnexes(unit_id);
        if (parts) {
            for (const auto& part : *parts) {
                if (part.shadow) continue;
                // Resolved exactly as the finished building's art is, so the
                // preview shows the building the player's age would put up.
                const int slp = ECS::Buildings::PieceGraphic(registry, viewer, part);
                const bool main = part.unit_id == unit_id || part.unit_id == art_unit ||
                                  part.counterpart_of == unit_id || part.counterpart_of == art_unit;
                if (slp >= 0) {
                    add(slp, 0, static_cast<float>(tile_x) + part.dx + mid_x,
                        static_cast<float>(tile_y) + part.dy + mid_y, clear, main);
                }
            }
        } else {
            // The same resolution the finished building uses, so the preview
            // is the building rather than whatever the unit's standing graphic
            // names.
            const auto* player = ECS::Players::Find(registry, viewer);
            const int civ = player != nullptr && player->civ >= 1 ? player->civ : 1;
            const int age = ECS::Techs::Age(registry, viewer) + 1;
            auto art = cfg.GetBuildingArt(art_unit, age, civ);
            if (!art.Valid()) art = cfg.GetBuildingArt(unit_id, age, civ);
            for (int later = age + 1; later <= 4 && !art.Valid(); ++later) {
                art = cfg.GetBuildingArt(art_unit, later, civ);
            }
            // Failing that, the unit's standing graphic: a Farm's field, which
            // the finished Farm lays as ground instead.
            int graphic = art.body;
            if (graphic < 0) {
                if (const auto* rules = ECS::Players::UnitOf(registry, viewer, art_unit)) {
                    graphic = rules->standing_graphic;
                }
            }
            const auto body = cfg.GetGraphic(graphic);
            if (body.Valid()) {
                add(body.slp_id, 0, static_cast<float>(tile_x) + mid_x,
                    static_cast<float>(tile_y) + mid_y, clear, true);
            }
        }
        if ((ui->selection_effect & 1) != 0) {
            const auto bounds = ECS::Buildings::FootprintBounds(
                {Core::Fixed32(tile_x), Core::Fixed32(tile_y)}, unit_id);
            outline.shown = true;
            outline.wx = ((bounds.min_x + bounds.max_x) / 2).ToFloat();
            outline.wy = ((bounds.min_y + bounds.max_y) / 2).ToFloat();
            outline.rx = ui->outline_x;
            outline.ry = ui->outline_y;
            outline.round = ui->obstruction == 5;
            outline.clear = clear;
        }
    }
    return true;
}

} // namespace Rendering::Placement
