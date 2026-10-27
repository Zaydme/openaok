#include "ecs/Walls.h"
#include "ecs/Nearby.h"

#include <algorithm>
#include <cstdlib>

#include "core/ConfigManager.h"
#include "core/FixedMath.h"
#include "core/Media.h"
#include "core/Rules.h"
#include "ecs/BuildingGeometry.h"
#include "ecs/Buildings.h"
#include "ecs/Diplomacy.h"
#include "ecs/Combat.h"
#include "ecs/Events.h"
#include "ecs/Players.h"
#include "ecs/components/Building.h"
#include "ecs/components/Gate.h"
#include "ecs/components/Health.h"
#include "ecs/components/Identity.h"
#include "ecs/components/Owner.h"
#include "ecs/components/PartOf.h"
#include "ecs/components/Sprite.h"
#include "ecs/components/SpritePieces.h"
#include "ecs/components/Transform.h"

namespace ECS::Walls {

namespace {

namespace R = Core::Rules;

// The dat's classes for a wall and a gate.
constexpr int kWallClass = 27;
constexpr int kGateClass = 39;

// A graphic's draw layers below this are its shadow.
constexpr int kShadowLayer = 10;

// How near a unit a gate opens for has to come before it is drawn raised:
// a step from its arch.
const Core::Fixed32 kOpenReach = Core::Fixed32(3) / Core::Fixed32(2);

Core::PlayerId OwnerOf(const entt::registry& registry, entt::entity e) {
    const auto* owner = registry.try_get<Components::Owner>(e);
    return owner != nullptr ? owner->player_id : Core::PLAYER_GAIA;
}

int CivOf(const entt::registry& registry, Core::PlayerId player) {
    const Players::Player* p = Players::Find(registry, player);
    return p != nullptr ? p->civ : 1;
}

// A wall's art is its own age's, whatever age its owner is in: the earliest
// age the building art table has for the unit. The table files the stone
// walls under the palisade's id as well, for later ages, and a palisade
// standing in the Feudal Age is still a palisade.
Core::ConfigManager::BuildingArt OwnArt(int art_unit, int civ) {
    auto& cfg = Core::ConfigManager::Get();
    for (int age = 1; age <= 4; ++age) {
        const auto art = cfg.GetBuildingArt(art_unit, age, civ);
        if (art.Valid()) return art;
    }
    return {};
}

// One drawable of a composite graphic.
struct Layer {
    int slp = -1;
    int frames = 1;
    int frame_ms = 100;
    int layer = 0;
    int offset_x = 0;
    int offset_y = 0;
};

// A graphic as drawn: its deltas, each another graphic (or,
// naming none, the graphic's own art) at a pixel offset, the shadow among
// them; a graphic without deltas is its own art. In draw order: the dat's
// layer, then list order.
void Compose(int graphic, std::vector<Layer>& body, int& shadow_slp) {
    auto& cfg = Core::ConfigManager::Get();
    auto add = [&](int g, int ox, int oy) {
        const auto info = cfg.GetGraphic(g);
        if (!info.Valid() || info.angle_count <= 0) return;
        if (info.layer < kShadowLayer) {
            if (shadow_slp < 0) shadow_slp = info.slp_id;
            return;
        }
        for (const Layer& l : body) {
            if (l.slp == info.slp_id && l.offset_x == ox && l.offset_y == oy) return;
        }
        body.push_back({info.slp_id, std::max(1, info.frame_count), cfg.GetFrameMs(g, 100),
                        info.layer, ox, oy});
    };
    const auto& deltas = Core::Media::Deltas(graphic);
    if (deltas.empty()) {
        add(graphic, 0, 0);
    } else {
        for (const auto& d : deltas) add(d.graphic >= 0 ? d.graphic : graphic, d.offset_x, d.offset_y);
    }
    std::stable_sort(body.begin(), body.end(),
                     [](const Layer& a, const Layer& b) { return a.layer < b.layer; });
}

// A sprite showing angle `piece` of a graphic: the renderer picks a file
// sprite's frame from its facing, and the first five facings are the first
// five angles unmirrored.
Components::Sprite PieceSprite(const Layer& l, int piece, int bias) {
    Components::Sprite sprite{};
    sprite.slp_id = l.slp;
    sprite.width = 0;
    sprite.height = 0;
    sprite.is_file_sprite = true;
    sprite.facing = static_cast<Core::Direction>(std::clamp(piece, 0, 4));
    sprite.frames_per_angle = l.frames;
    sprite.animation_speed = l.frame_ms;
    sprite.sort_bias = bias;
    sprite.shadow_resolved = true;
    return sprite;
}

entt::entity PartAt(entt::registry& registry, entt::entity building, Components::Vector2D at,
                    Components::Sprite sprite) {
    const entt::entity part = registry.create();
    registry.emplace<Components::Transform>(part, at, at);
    registry.emplace<Components::Sprite>(part, std::move(sprite));
    registry.emplace<Components::Owner>(part, OwnerOf(registry, building));
    registry.emplace<Components::PartOf>(part, building);
    return part;
}

// The tiles of the player's joining buildings in a rectangle, as a mask over
// it, for working out pieces without asking every building for every tile.
struct Occupancy {
    int x0 = 0, y0 = 0, w = 0, h = 0;
    std::vector<uint8_t> joined;

    bool At(int x, int y) const {
        if (x < x0 || y < y0 || x >= x0 + w || y >= y0 + h) return false;
        return joined[static_cast<size_t>((y - y0) * w + (x - x0))] != 0;
    }
};

Occupancy Occupied(const entt::registry& registry, Core::PlayerId owner,
                   int x0, int y0, int x1, int y1) {
    Occupancy occ;
    occ.x0 = x0;
    occ.y0 = y0;
    occ.w = std::max(0, x1 - x0 + 1);
    occ.h = std::max(0, y1 - y0 + 1);
    occ.joined.assign(static_cast<size_t>(occ.w * occ.h), 0);
    auto buildings = registry.view<const Components::Building, const Components::Identity,
                                   const Components::Transform, const Components::Owner>();
    for (auto b : buildings) {
        if (buildings.get<const Components::Owner>(b).player_id != owner) continue;
        const int unit_id = buildings.get<const Components::Identity>(b).unit_id;
        if (!Joins(unit_id)) continue;
        const auto& at = buildings.get<const Components::Transform>(b).position;
        Buildings::ForEachFootprintTile(unit_id, Physics::TileOf(at.x), Physics::TileOf(at.y),
                                        [&](int x, int y) {
            if (x < occ.x0 || y < occ.y0 || x >= occ.x0 + occ.w || y >= occ.y0 + occ.h) return;
            occ.joined[static_cast<size_t>((y - occ.y0) * occ.w + (x - occ.x0))] = 1;
        });
    }
    return occ;
}

int PieceFrom(const Occupancy& occ, int x, int y) {
    const bool n = occ.At(x, y - 1), s = occ.At(x, y + 1);
    const bool w = occ.At(x - 1, y), e = occ.At(x + 1, y);
    const bool ne = occ.At(x + 1, y - 1), sw = occ.At(x - 1, y + 1);
    const bool se = occ.At(x + 1, y + 1), nw = occ.At(x - 1, y - 1);
    // A run: one on each side of it along one line, and nothing touching it
    // across that line.
    if (n && s && !w && !e) return kAlongY;
    if (w && e && !n && !s) return kAlongX;
    const bool orthogonal = n || s || w || e;
    if (!orthogonal && ne && sw && !se && !nw) return kAcross;
    if (!orthogonal && se && nw && !ne && !sw) return kDown;
    return kPost;
}

// The middle of a footprint, from its origin tile: an even side is anchored up
// and to the left, so its middle is half a tile back.
Components::Vector2D MiddleOf(Components::Vector2D origin, int unit_id) {
    const Buildings::Footprint f = Buildings::FootprintOf(unit_id);
    return {origin.x + Core::Fixed32(f.x.low + f.x.high) / Core::Fixed32(2),
            origin.y + Core::Fixed32(f.y.low + f.y.high) / Core::Fixed32(2)};
}

// A world offset as screen pixels, the isometric projection's own figures:
// a tile is 96 by 48.
void ToPixels(Components::Vector2D d, int& px, int& py) {
    px = ((d.x - d.y) * Core::Fixed32(48)).ToInt();
    py = ((d.x + d.y) * Core::Fixed32(24)).ToInt();
}

void SpawnWallArt(entt::registry& registry, entt::entity wall) {
    const Core::PlayerId owner = OwnerOf(registry, wall);
    const int unit_id = registry.get<Components::Identity>(wall).unit_id;
    const auto art = OwnArt(Players::ArtOf(registry, owner, unit_id), CivOf(registry, owner));
    if (!art.Valid()) return;
    std::vector<Layer> layers;
    int shadow = -1;
    Compose(art.body, layers, shadow);
    if (art.shadow >= 0) {
        const auto info = Core::ConfigManager::Get().GetGraphic(art.shadow);
        if (info.Valid()) shadow = info.slp_id;
    }
    if (layers.empty()) return;
    const int piece = PieceOf(registry, wall);
    const auto at = registry.get<Components::Transform>(wall).position;
    int bias = 0;
    for (size_t i = 0; i < layers.size(); ++i) {
        Components::Sprite sprite = PieceSprite(layers[i], piece, bias++);
        if (i == 0) {
            // The first is the wall itself, and casts the piece's shadow.
            sprite.shadow_slp = shadow;
            registry.emplace_or_replace<Components::Sprite>(wall, std::move(sprite));
        } else {
            PartAt(registry, wall, at, std::move(sprite));
        }
    }
}

void SpawnGateArt(entt::registry& registry, entt::entity gate) {
    auto& cfg = Core::ConfigManager::Get();
    const Core::PlayerId owner = OwnerOf(registry, gate);
    const int civ = CivOf(registry, owner);
    const int unit_id = registry.get<Components::Identity>(gate).unit_id;
    const R::UnitRules* rules = Players::UnitOf(registry, owner, unit_id);
    if (rules == nullptr) return;
    const auto origin = registry.get<Components::Transform>(gate).position;
    const auto middle = MiddleOf(origin, unit_id);

    // The arch, shut and raised: the gate's own art and its transform unit's.
    const auto shut = OwnArt(Players::ArtOf(registry, owner, unit_id), civ);
    const auto raised = rules->transform_unit >= 0
        ? OwnArt(Players::ArtOf(registry, owner, rules->transform_unit), civ)
        : Core::ConfigManager::BuildingArt{};
    auto& state = registry.get_or_emplace<Components::Gate>(gate);
    state.closed_slp = shut.Valid() ? cfg.GetGraphic(shut.body).slp_id : -1;
    state.open_slp = raised.Valid() ? cfg.GetGraphic(raised.body).slp_id : state.closed_slp;

    // Drawn at the gate's middle, which an even footprint puts half a tile
    // off its origin tile: the gate entity's own art is one piece offset there.
    int px = 0, py = 0;
    ToPixels({middle.x - origin.x, middle.y - origin.y}, px, py);
    Components::Sprite own{};
    own.slp_id = state.open && state.open_slp >= 0 ? state.open_slp : state.closed_slp;
    own.width = 0;
    own.height = 0;
    own.shadow_resolved = true;
    registry.emplace_or_replace<Components::Sprite>(gate, own);
    Components::SpritePieces arch;
    arch.count = 1;
    arch.piece[0].slp_id = own.slp_id;
    arch.piece[0].frames_per_angle = 1;
    arch.piece[0].angles = 1;
    arch.piece[0].offset_x = px;
    arch.piece[0].offset_y = py;
    registry.emplace_or_replace<Components::SpritePieces>(gate, arch);
    if (shut.Valid() && shut.shadow >= 0) {
        Components::Sprite shade{};
        shade.slp_id = cfg.GetGraphic(shut.shadow).slp_id;
        shade.width = 0;
        shade.height = 0;
        shade.is_shadow = true;
        shade.shadow_resolved = true;
        shade.sort_bias = -1;
        PartAt(registry, gate, middle, shade);
    }

    // Its posts: the dat's annex units, at their offsets from the middle,
    // each drawn as its own graphic's layers -- a tower, its shadow, a flag.
    for (int a = 0; a < rules->annex_count; ++a) {
        const R::Annex& annex = rules->annexes[a];
        const R::UnitRules* post = Players::UnitOf(registry, owner, annex.unit);
        if (post == nullptr) continue;
        const Components::Vector2D offset = Buildings::FromDatAxes(annex.dx, annex.dy);
        const Components::Vector2D at{middle.x + offset.x, middle.y + offset.y};
        std::vector<Layer> layers;
        int shadow = -1;
        Compose(post->standing_graphic, layers, shadow);
        int bias = 0;
        for (size_t i = 0; i < layers.size(); ++i) {
            Components::Sprite sprite = PieceSprite(layers[i], 0, bias++);
            if (i == 0) sprite.shadow_slp = shadow;
            PartAt(registry, gate, at, std::move(sprite));
        }
    }
}

// Whom `a`'s gates open for: itself, and those it regards as allies -- the
// lobby's team-mates to begin with, and whatever diplomacy makes of them.
bool Friends(const entt::registry& registry, Core::PlayerId a, Core::PlayerId b) {
    if (a == b) return true;
    return Diplomacy::StanceOf(registry, a, b) == Diplomacy::Stance::Ally;
}

} // namespace

bool Joins(int unit_id) {
    const R::UnitRules* rules = R::Unit(unit_id);
    return rules != nullptr && rules->adjacent_mode != 0;
}

bool IsWall(int unit_id) {
    const R::UnitRules* rules = R::Unit(unit_id);
    return rules != nullptr && rules->adjacent_mode != 0 && rules->cls == kWallClass;
}

bool IsGate(int unit_id) {
    const R::UnitRules* rules = R::Unit(unit_id);
    return rules != nullptr && rules->cls == kGateClass;
}

std::vector<std::pair<int, int>> Line(int x0, int y0, int x1, int y1) {
    std::vector<std::pair<int, int>> tiles;
    const int dx = std::abs(x1 - x0);
    const int dy = -std::abs(y1 - y0);
    const int sx = x0 < x1 ? 1 : -1;
    const int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    int x = x0, y = y0;
    while (true) {
        tiles.emplace_back(x, y);
        if (x == x1 && y == y1) break;
        const int e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y += sy;
        }
    }
    return tiles;
}

std::vector<entt::entity> Lay(entt::registry& registry, Physics::Grid& grid,
                              Core::PlayerId player, int unit_id,
                              int x0, int y0, int x1, int y1) {
    std::vector<entt::entity> sites;
    const R::UnitRules* rules = Players::UnitOf(registry, player, unit_id);
    Players::Player* payer = Players::Find(registry, player);
    if (rules == nullptr || payer == nullptr || !IsWall(unit_id)) return sites;
    const auto tiles = Line(x0, y0, x1, y1);
    for (const auto& [x, y] : tiles) {
        if (!Buildings::CanPlace(grid, unit_id, x, y)) continue;
        if (!Players::CanPay(*payer, rules->costs, rules->cost_count)) break;
        Players::Pay(*payer, rules->costs, rules->cost_count, -1);
        const entt::entity site = Buildings::Place(registry, grid, unit_id, player, x, y, false);
        if (site != entt::null) sites.push_back(site);
    }
    if (!sites.empty()) {
        Refresh(registry, player, std::min(x0, x1) - 1, std::min(y0, y1) - 1,
                std::max(x0, x1) + 1, std::max(y0, y1) + 1);
    }
    return sites;
}

int PieceOf(const entt::registry& registry, entt::entity wall) {
    const auto* at = registry.try_get<Components::Transform>(wall);
    if (at == nullptr) return kPost;
    const int x = Physics::TileOf(at->position.x);
    const int y = Physics::TileOf(at->position.y);
    const Occupancy occ = Occupied(registry, OwnerOf(registry, wall), x - 1, y - 1, x + 1, y + 1);
    return PieceFrom(occ, x, y);
}

void Refresh(entt::registry& registry, Core::PlayerId owner, int x0, int y0, int x1, int y1) {
    std::vector<entt::entity> walls;
    auto buildings = registry.view<Components::Building, Components::Identity,
                                   Components::Transform, Components::Owner>();
    for (auto b : buildings) {
        if (buildings.get<Components::Owner>(b).player_id != owner) continue;
        if (!IsWall(buildings.get<Components::Identity>(b).unit_id)) continue;
        const auto& at = buildings.get<Components::Transform>(b).position;
        const int x = Physics::TileOf(at.x), y = Physics::TileOf(at.y);
        if (x < x0 || y < y0 || x > x1 || y > y1) continue;
        walls.push_back(b);
    }
    for (entt::entity wall : walls) {
        if (registry.get<Components::Building>(wall).state == Components::BuildingState::Complete) {
            Buildings::RefreshArt(registry, wall);
        } else {
            SiteArt(registry, wall);
        }
    }
}

void RefreshAround(entt::registry& registry, entt::entity building) {
    if (!registry.valid(building) ||
        !registry.all_of<Components::Identity, Components::Transform>(building)) {
        return;
    }
    const int unit_id = registry.get<Components::Identity>(building).unit_id;
    if (!Joins(unit_id)) return;
    const auto bounds = Buildings::FootprintBounds(
        registry.get<Components::Transform>(building).position, unit_id);
    Refresh(registry, OwnerOf(registry, building), Physics::TileOf(bounds.min_x) - 1,
            Physics::TileOf(bounds.min_y) - 1, Physics::TileOf(bounds.max_x) + 1,
            Physics::TileOf(bounds.max_y) + 1);
}

int GateFor(const entt::registry& registry, Core::PlayerId player, int button_unit,
            int tile_x, int tile_y) {
    // Every orientation the dat has, as the button units each stacks: the
    // one placed (GTAX2) and those that stack the other three ways.
    const R::Tables& tables = R::Get();
    std::vector<int> buttons;
    for (int id = 0; id < static_cast<int>(tables.units.size()); ++id) {
        const R::UnitRules& u = tables.units[id];
        if (!u.valid || u.cls != kGateClass || u.stack_unit < 0) continue;
        // Only those of the button's own grade: GTAX2's, not GTAX3's.
        const R::UnitRules* button = R::Unit(button_unit);
        if (button == nullptr || u.hp != button->hp) continue;
        buttons.push_back(id);
    }
    const Occupancy occ = Occupied(registry, player, tile_x - 3, tile_y - 3, tile_x + 3, tile_y + 3);
    // Which of them the wall under the cursor runs along: the most of the
    // player's wall segments on the gate's own line through the tile.
    int best = button_unit;
    int best_count = 0;
    for (int candidate : buttons) {
        const R::UnitRules* u = R::Unit(candidate);
        const R::UnitRules* stack = R::Unit(u->stack_unit);
        if (stack == nullptr || stack->annex_count < 2) continue;
        // The line the gate lies along: from one post to the other.
        const auto a = Buildings::FromDatAxes(stack->annexes[0].dx, stack->annexes[0].dy);
        const auto b = Buildings::FromDatAxes(stack->annexes[1].dx, stack->annexes[1].dy);
        const int sx = (b.x - a.x).value > 0 ? 1 : (b.x - a.x).value < 0 ? -1 : 0;
        const int sy = (b.y - a.y).value > 0 ? 1 : (b.y - a.y).value < 0 ? -1 : 0;
        int count = 0;
        for (int k = -2; k <= 2; ++k) {
            if (occ.At(tile_x + k * sx, tile_y + k * sy)) ++count;
        }
        if (count > best_count) {
            best_count = count;
            best = candidate;
        }
    }
    return best;
}

bool CanPlaceGate(const entt::registry& registry, const Physics::Grid& grid,
                  Core::PlayerId player, int unit_id, int tile_x, int tile_y) {
    const int placed = Buildings::PlacedUnit(unit_id);
    const R::UnitRules* rules = R::Unit(placed);
    if (rules == nullptr) return false;
    auto& cfg = Core::ConfigManager::Get();
    // The player's own wall segments, which the gate would replace.
    std::vector<std::pair<int, int>> walls;
    auto buildings = registry.view<const Components::Building, const Components::Identity,
                                   const Components::Transform, const Components::Owner>();
    for (auto b : buildings) {
        if (buildings.get<const Components::Owner>(b).player_id != player) continue;
        if (!IsWall(buildings.get<const Components::Identity>(b).unit_id)) continue;
        const auto& at = buildings.get<const Components::Transform>(b).position;
        walls.emplace_back(Physics::TileOf(at.x), Physics::TileOf(at.y));
    }
    bool clear = true;
    Buildings::ForEachFootprintTile(placed, tile_x, tile_y, [&](int x, int y) {
        if (!clear) return;
        if (!grid.InBounds(x, y) ||
            !R::Buildable(rules->terrain_restriction,
                          cfg.GetTerrainArt(grid.GetTile(x, y).terrain).dat_id)) {
            clear = false;
            return;
        }
        if (grid.IsBlocked(x, y) &&
            std::find(walls.begin(), walls.end(), std::make_pair(x, y)) == walls.end()) {
            clear = false;
        }
    });
    return clear;
}

void ClearForGate(entt::registry& registry, Physics::Grid& grid, Core::PlayerId player,
                  int unit_id, int tile_x, int tile_y) {
    const int placed = Buildings::PlacedUnit(unit_id);
    std::vector<entt::entity> under;
    auto buildings = registry.view<Components::Building, Components::Identity,
                                   Components::Transform, Components::Owner>();
    Buildings::ForEachFootprintTile(placed, tile_x, tile_y, [&](int x, int y) {
        for (auto b : buildings) {
            if (buildings.get<Components::Owner>(b).player_id != player) continue;
            if (!IsWall(buildings.get<Components::Identity>(b).unit_id)) continue;
            const auto& at = buildings.get<Components::Transform>(b).position;
            if (Physics::TileOf(at.x) == x && Physics::TileOf(at.y) == y &&
                std::find(under.begin(), under.end(), b) == under.end()) {
                under.push_back(b);
            }
        }
    });
    for (entt::entity wall : under) Buildings::Destroy(registry, grid, wall, false);
}

// The tiles a gate lets its own side through: its arch's, the gate unit's own
// size about its middle -- two tiles of the four, the posts at either end
// being annexes of their own that block like any wall.
template <typename Visit>
void ForEachArchTile(const entt::registry& registry, entt::entity gate, Visit visit) {
    const int unit_id = registry.get<Components::Identity>(gate).unit_id;
    const auto& at = registry.get<Components::Transform>(gate).position;
    const R::UnitRules* rules = Players::UnitOf(registry, OwnerOf(registry, gate), unit_id);
    if (rules == nullptr) return;
    const auto middle = MiddleOf(at, unit_id);
    // The dat's x extent is the engine's y extent (ecs/BuildingGeometry).
    const Core::Fixed32 half_x = rules->size_y;
    const Core::Fixed32 half_y = rules->size_x;
    Buildings::ForEachFootprintTile(unit_id, Physics::TileOf(at.x), Physics::TileOf(at.y),
                                    [&](int x, int y) {
        const Core::Fixed32 dx = Core::FixedMath::Abs(Core::Fixed32(x) - middle.x);
        const Core::Fixed32 dy = Core::FixedMath::Abs(Core::Fixed32(y) - middle.y);
        if (dx < half_x && dy < half_y) visit(x, y);
    });
}

void GateFinished(entt::registry& registry, Physics::Grid& grid, entt::entity gate) {
    if (!registry.all_of<Components::Identity, Components::Transform>(gate)) return;
    const Core::PlayerId owner = OwnerOf(registry, gate);
    const bool locked = registry.get_or_emplace<Components::Gate>(gate).locked;
    ForEachArchTile(registry, gate, [&](int x, int y) {
        grid.SetGate(x, y, owner);
        grid.SetGateLocked(x, y, locked);
    });
}

void GateGone(entt::registry& registry, Physics::Grid& grid, entt::entity gate) {
    if (!registry.all_of<Components::Identity, Components::Transform>(gate)) return;
    const int unit_id = registry.get<Components::Identity>(gate).unit_id;
    const auto& at = registry.get<Components::Transform>(gate).position;
    Buildings::ForEachFootprintTile(unit_id, Physics::TileOf(at.x), Physics::TileOf(at.y),
                                    [&](int x, int y) { grid.SetGate(x, y, 0); });
}

bool SetLocked(entt::registry& registry, Physics::Grid& grid, entt::entity gate, bool locked) {
    if (!registry.valid(gate) || !registry.all_of<Components::Building, Components::Gate,
                                                  Components::Identity,
                                                  Components::Transform>(gate)) {
        return false;
    }
    if (registry.get<Components::Building>(gate).state != Components::BuildingState::Complete) {
        return false;
    }
    auto& state = registry.get<Components::Gate>(gate);
    if (state.locked == locked) return false;
    state.locked = locked;
    const int unit_id = registry.get<Components::Identity>(gate).unit_id;
    const auto at = registry.get<Components::Transform>(gate).position;
    ForEachArchTile(registry, gate, [&](int x, int y) { grid.SetGateLocked(x, y, locked); });
    // Nobody is left standing in a gate that has just shut.
    if (locked) Buildings::StepAside(registry, grid, Buildings::FootprintBounds(at, unit_id));
    Events::Emit(registry, {.kind = locked ? Events::Kind::GateLocked : Events::Kind::GateUnlocked,
                            .subject = gate, .player = OwnerOf(registry, gate), .id = unit_id,
                            .at = at});
    return true;
}

void SetAllies(entt::registry& registry, Physics::Grid& grid) {
    for (int p = 1; p < Core::kMaxPlayers; ++p) {
        uint16_t friends = 0;
        for (int q = 1; q < Core::kMaxPlayers; ++q) {
            if (q != p && Friends(registry, static_cast<Core::PlayerId>(p),
                                  static_cast<Core::PlayerId>(q))) {
                friends = static_cast<uint16_t>(friends | (1u << q));
            }
        }
        grid.SetGateFriends(static_cast<Core::PlayerId>(p), friends);
    }
}

bool SpawnArt(entt::registry& registry, entt::entity building) {
    const int unit_id = registry.get<Components::Identity>(building).unit_id;
    if (IsGate(unit_id)) {
        SpawnGateArt(registry, building);
        return true;
    }
    if (IsWall(unit_id)) {
        SpawnWallArt(registry, building);
        return true;
    }
    return false;
}

void SiteArt(entt::registry& registry, entt::entity site) {
    auto* sprite = registry.try_get<Components::Sprite>(site);
    if (sprite == nullptr || !registry.all_of<Components::Identity>(site)) return;
    const int unit_id = registry.get<Components::Identity>(site).unit_id;
    if (!IsWall(unit_id)) return;
    const R::UnitRules* rules = Players::UnitOf(registry, OwnerOf(registry, site), unit_id);
    if (rules == nullptr) return;
    const auto info = Core::ConfigManager::Get().GetGraphic(rules->construction_graphic);
    // A construction graphic with a wall's five angles keeps its stages for
    // each piece; any other steps through all its frames.
    if (!info.Valid() || info.angle_count != 5) return;
    sprite->construction_frames = std::max(1, info.frame_count);
    sprite->construction_offset = PieceOf(registry, site) * sprite->construction_frames;
    sprite->slp_frame = sprite->construction_offset;
}

void Update(entt::registry& registry, const Physics::Grid& grid) {
    auto gates = registry.view<Components::Gate, Components::Building, Components::SpritePieces,
                               Components::Identity, Components::Transform, Components::Owner>();
    if (gates.begin() == gates.end()) return;
    auto units = registry.view<const Components::Transform, const Components::Owner,
                               const Components::Health>();
    static Nearby::Buckets near;
    Nearby::Build(near, units, grid.GetWidth(), grid.GetHeight());
    for (auto gate : gates) {
        auto& state = gates.get<Components::Gate>(gate);
        const Core::PlayerId owner = gates.get<Components::Owner>(gate).player_id;
        bool open = false;
        if (!state.locked) {
            const auto bounds = Buildings::FootprintBounds(
                gates.get<Components::Transform>(gate).position,
                gates.get<Components::Identity>(gate).unit_id);
            // The buckets around the footprint grown by the reach.
            const auto& middle = gates.get<Components::Transform>(gate).position;
            const Core::Fixed32 reach =
                std::max({bounds.max_x - middle.x, middle.x - bounds.min_x, bounds.max_y - middle.y,
                          middle.y - bounds.min_y}) + kOpenReach;
            Nearby::ForEachNear(near, middle, reach, [&](entt::entity unit) {
                if (open) return;
                // Outside the gate's footprint grown by the reach on either
                // axis is farther than the reach: the cheap test first.
                const auto& at = units.get<const Components::Transform>(unit).position;
                if (at.x < bounds.min_x - kOpenReach || at.x > bounds.max_x + kOpenReach ||
                    at.y < bounds.min_y - kOpenReach || at.y > bounds.max_y + kOpenReach) {
                    return;
                }
                if (bounds.DistanceFrom(at) > kOpenReach) return;
                if (registry.all_of<Components::Building>(unit)) return;
                if (!Friends(registry, owner, units.get<const Components::Owner>(unit).player_id)) return;
                if (!Combat::Alive(registry, unit)) return;
                open = true;
            });
        }
        if (open == state.open) continue;
        state.open = open;
        const int slp = open && state.open_slp >= 0 ? state.open_slp : state.closed_slp;
        gates.get<Components::SpritePieces>(gate).piece[0].slp_id = slp;
    }
}

} // namespace ECS::Walls

namespace ECS::Walls {

std::vector<GhostPiece> Ghost(const entt::registry& registry, const Physics::Grid& grid,
                              Core::PlayerId player, int button_unit,
                              int from_x, int from_y, int to_x, int to_y) {
    std::vector<GhostPiece> out;
    auto& cfg = Core::ConfigManager::Get();
    const int civ = CivOf(registry, player);
    if (IsWall(button_unit)) {
        const auto art = OwnArt(Players::ArtOf(registry, player, button_unit), civ);
        if (!art.Valid()) return out;
        std::vector<Layer> layers;
        int shadow = -1;
        Compose(art.body, layers, shadow);
        if (layers.empty()) return out;
        const auto tiles = Line(from_x, from_y, to_x, to_y);
        // The pieces as they would be with the line laid: what stands
        // already, and every segment of the line that can go up.
        int x0 = from_x, y0 = from_y, x1 = from_x, y1 = from_y;
        for (const auto& [x, y] : tiles) {
            x0 = std::min(x0, x); y0 = std::min(y0, y);
            x1 = std::max(x1, x); y1 = std::max(y1, y);
        }
        Occupancy occ = Occupied(registry, player, x0 - 1, y0 - 1, x1 + 1, y1 + 1);
        std::vector<bool> clear;
        for (const auto& [x, y] : tiles) {
            const bool ok = Buildings::CanPlace(grid, button_unit, x, y);
            clear.push_back(ok);
            if (ok) occ.joined[static_cast<size_t>((y - occ.y0) * occ.w + (x - occ.x0))] = 1;
        }
        for (size_t i = 0; i < tiles.size(); ++i) {
            const auto [x, y] = tiles[i];
            out.push_back({x, y, Core::Fixed32(0), Core::Fixed32(0), layers[0].slp,
                           PieceFrom(occ, x, y) * layers[0].frames, clear[i]});
        }
        return out;
    }
    if (IsGate(button_unit)) {
        const int unit = GateFor(registry, player, button_unit, to_x, to_y);
        const int placed = Buildings::PlacedUnit(unit);
        const R::UnitRules* rules = Players::UnitOf(registry, player, placed);
        if (rules == nullptr) return out;
        const bool clear = CanPlaceGate(registry, grid, player, unit, to_x, to_y);
        const auto origin = Components::Vector2D{Core::Fixed32(to_x), Core::Fixed32(to_y)};
        const auto middle = MiddleOf(origin, placed);
        const auto shut = OwnArt(Players::ArtOf(registry, player, placed), civ);
        if (shut.Valid()) {
            out.push_back({to_x, to_y, middle.x - origin.x, middle.y - origin.y,
                           cfg.GetGraphic(shut.body).slp_id, 0, clear});
        }
        for (int a = 0; a < rules->annex_count; ++a) {
            const R::UnitRules* post = Players::UnitOf(registry, player, rules->annexes[a].unit);
            if (post == nullptr) continue;
            std::vector<Layer> layers;
            int shadow = -1;
            Compose(post->standing_graphic, layers, shadow);
            if (layers.empty()) continue;
            const auto offset = Buildings::FromDatAxes(rules->annexes[a].dx, rules->annexes[a].dy);
            out.push_back({to_x, to_y, middle.x - origin.x + offset.x,
                           middle.y - origin.y + offset.y, layers[0].slp, 0, clear});
        }
    }
    return out;
}

} // namespace ECS::Walls
