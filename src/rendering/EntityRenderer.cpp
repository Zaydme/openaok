#include "EntityRenderer.h"
#include "Renderer.h"
#include "Ground.h"
#include "UIRenderer.h"
#include "FogRenderer.h"
#include "Placement.h"
#include "Stipple.h"
#include "ecs/components/Transform.h"
#include "ecs/components/Sprite.h"
#include "ecs/components/SpritePieces.h"
#include "ecs/components/Path.h"
#include "ecs/components/Selectable.h"
#include "ecs/components/Health.h"
#include "ecs/components/Doppelganger.h"
#include "ecs/components/Owner.h"
#include "ecs/components/Waypoints.h"
#include "ecs/components/Building.h"
#include "ecs/components/Identity.h"
#include "ecs/components/RallyPoint.h"
#include "ecs/components/PartOf.h"
#include "ecs/BuildingGeometry.h"
#include "ecs/Walls.h"
#include "ecs/Players.h"
#include "ecs/Vision.h"
#include "core/ConfigManager.h"
#include "core/Media.h"
#include "core/IsometricMath.h"
#include "hal/GraphicCache.h"
#include "hal/TextureManager.h"
#include "physics/Grid.h"
#include <SDL2/SDL.h>
#include <array>
#include <vector>
#include <algorithm>
#include <cmath>

namespace Rendering {

namespace {

// How a selected thing is marked: a one-pixel white outline on the ground
// under it, a 24 by 2 hit point bar
// of pure green over pure red floating above it, and its control group's
// digit (interfac.drs 50403, one frame per digit) to its left.
constexpr int kHpBarWidth = 24;
constexpr int kHpBarHeight = 2;
constexpr uint32_t kGroupDigits = 50403;

struct SelectionMark {
    int sx = 0, sy = 0;           // the pixel its outline is centred on
    float rx = 0.2f, ry = 0.2f;   // the dat's outline radii, in tiles
    bool round = true;
    bool bar = true;
    float bar_height = 2.0f;      // the dat's, in half tiles
    int hp = 0, max_hp = 0;
    int group = 0;
    SDL_Color colour{255, 255, 255, 255};   // palette index 255
};

// The flash on what an order was given at. The view keeps eight. Each lasts
// 1500 ms of the wall clock and is off for every other 256 ms, so the thing
// shows three times; while on, its selection outline is drawn whether it is
// selected or not, in pure green (palette index 241), and nothing else of a
// selection with it -- no hit point bar, no group.
struct Flashing {
    entt::entity target = entt::null;
    uint32_t started = 0;   // SDL ticks
    bool live = false;
};
constexpr size_t kFlashSlots = 8;
constexpr uint32_t kFlashMs = 1500;
constexpr uint32_t kFlashBlinkBit = 0x100;
constexpr size_t kFlashColour = 0xf1;

std::array<Flashing, kFlashSlots>& Flashes() {
    static std::array<Flashing, kFlashSlots> flashes;
    return flashes;
}

// The ellipse a circle on the ground makes in the isometric view, rasterised
// one pixel wide: every column and every row it crosses gets the pixel on its
// rim nearest the centre, which leaves no gaps where it runs steep or flat.
// The centre sits half a pixel above the foot row.
void PlotEllipse(std::vector<SDL_Point>& out, int cx, int cy, float a, float b) {
    if (a < 1.0f || b < 1.0f) return;
    const float fy = static_cast<float>(cy) - 0.5f;
    const int span_x = static_cast<int>(a);
    for (int dx = -span_x; dx <= span_x; ++dx) {
        const float t = static_cast<float>(dx) / a;
        const float h = b * std::sqrt(std::max(0.0f, 1.0f - t * t));
        out.push_back({cx + dx, static_cast<int>(std::ceil(fy - h))});
        out.push_back({cx + dx, static_cast<int>(std::floor(fy + h))});
    }
    const int top = static_cast<int>(std::ceil(fy - b));
    const int bottom = static_cast<int>(std::floor(fy + b));
    for (int y = top; y <= bottom; ++y) {
        const float t = (static_cast<float>(y) - fy) / b;
        const float w = a * std::sqrt(std::max(0.0f, 1.0f - t * t));
        out.push_back({static_cast<int>(std::ceil(static_cast<float>(cx) - w)), y});
        out.push_back({static_cast<int>(std::floor(static_cast<float>(cx) + w)), y});
    }
}

void DrawOutline(SDL_Renderer* r, const SelectionMark& m, std::vector<SDL_Point>& scratch) {
    constexpr float kHalfW = Core::IsometricMath::TILE_WIDTH / 2.0f;
    constexpr float kHalfH = Core::IsometricMath::TILE_HEIGHT / 2.0f;
    SDL_SetRenderDrawColor(r, m.colour.r, m.colour.g, m.colour.b, 255);
    if (m.round) {
        // A circle of radius r tiles reaches r*sqrt(2) half tiles each way
        // on screen, the world's axes being the screen's diagonals.
        const float reach = std::sqrt(2.0f) * 0.5f * (m.rx + m.ry);
        scratch.clear();
        PlotEllipse(scratch, m.sx, m.sy, reach * kHalfW, reach * kHalfH);
        SDL_RenderDrawPoints(r, scratch.data(), static_cast<int>(scratch.size()));
        return;
    }
    // A square outline is the diamond its radii make on the ground.
    const float x = static_cast<float>(m.sx);
    const float y = static_cast<float>(m.sy);
    const SDL_FPoint corners[5] = {
        {x + (m.ry - m.rx) * kHalfW, y - (m.rx + m.ry) * kHalfH},
        {x + (m.rx + m.ry) * kHalfW, y + (m.rx - m.ry) * kHalfH},
        {x + (m.rx - m.ry) * kHalfW, y + (m.rx + m.ry) * kHalfH},
        {x - (m.rx + m.ry) * kHalfW, y + (m.ry - m.rx) * kHalfH},
        {x + (m.ry - m.rx) * kHalfW, y - (m.rx + m.ry) * kHalfH},
    };
    SDL_RenderDrawLinesF(r, corners, 5);
}

// The bar's top edge stands `bar_height` half tiles above the outline's top
// corner -- 58 pixels over a footman's feet, 67 over a camel's -- and its
// green part is the share of hit points left.
void DrawHealthBar(SDL_Renderer* r, const SelectionMark& m) {
    if (!m.bar || m.max_hp <= 0) return;
    constexpr float kHalfH = Core::IsometricMath::TILE_HEIGHT / 2.0f;
    const float rise = (m.rx + m.ry + m.bar_height) * kHalfH;
    const int top = static_cast<int>(std::floor(static_cast<float>(m.sy) - rise));
    const int left = m.sx - kHpBarWidth / 2;
    const int hp = std::clamp(m.hp, 0, m.max_hp);
    int green = hp * kHpBarWidth / m.max_hp;
    if (hp > 0 && green == 0) green = 1;
    if (green > 0) {
        SDL_SetRenderDrawColor(r, 0, 255, 0, 255);
        const SDL_Rect g{left, top, green, kHpBarHeight};
        SDL_RenderFillRect(r, &g);
    }
    if (green < kHpBarWidth) {
        SDL_SetRenderDrawColor(r, 255, 0, 0, 255);
        const SDL_Rect red{left + green, top, kHpBarWidth - green, kHpBarHeight};
        SDL_RenderFillRect(r, &red);
    }
}

// The group's digit sits level with the feet, a little way left of where
// the outline's left corner would be.
void DrawGroupNumber(SDL_Renderer* r, const SelectionMark& m) {
    if (m.group < 1 || m.group > 9) return;
    const HAL::GraphicAtlas* digits = HAL::GraphicCache::Get().GetInterfaceAtlas(kGroupDigits);
    if (digits == nullptr || digits->base == nullptr) return;
    const size_t frame = static_cast<size_t>(m.group - 1);
    if (frame >= digits->frames.size()) return;
    const HAL::GraphicFrame& gf = digits->frames[frame];
    constexpr float kHalfW = Core::IsometricMath::TILE_WIDTH / 2.0f;
    const int left_corner = static_cast<int>(std::lround((m.rx + m.ry) * kHalfW));
    const SDL_Rect dest{m.sx - left_corner - 6 - gf.hotspot_x, m.sy - 7 - gf.hotspot_y,
                        gf.rect.w, gf.rect.h};
    SDL_RenderCopy(r, digits->base, &gf.rect, &dest);
}

// World markers drawn by graphic id, not as units: a move order's arrows
// (MOVEX) and the small flag that marks a waypoint or a building's gather
// point (WAYPT). Ten at most.
constexpr int kMoveMarkerGraphic = 2961;
constexpr int kWaypointGraphic = 2962;
constexpr size_t kMaxMarkers = 10;

struct Marker {
    float wx = 0.0f, wy = 0.0f;
    uint32_t started = 0;   // SDL ticks
};

std::vector<Marker>& Markers() {
    static std::vector<Marker> markers;
    return markers;
}

struct RenderEntry {
    float depth;
    entt::entity ent;
    void* tex_ptr;

    bool is_selected;
    float sel_sx, sel_sy;
    int mark = -1;       // its SelectionMark, or -1

    int sort_bias;
    int tex_w, tex_h;

    void* shadow_tex;
    bool shadow_flip;
    SDL_Rect shadow_src;
    SDL_FRect shadow_dest;
    void* player_tex;
    void* outline_tex;
    int outline_player;

    SDL_Rect src;
    SDL_FRect dest;
    const HAL::GraphicAtlas* atlas;
    int frame;
    uint32_t fallback_color;
    float fallback_w, fallback_h;
    bool flip_h;
    // Drawn at ground level, under the fog, with the ground (FogRenderer.h).
    bool ground;
    // Standing on ground the player has explored but does not see now:
    // dithered whole.
    bool fogged;
    // A piece of the building being placed (Placement.h): 0 none, else which
    // of the blitter's masks goes over it -- kGhostClear, or the pulsing red
    // of the building's own piece or of the rest.
    uint8_t ghost;
};
constexpr uint8_t kGhostClear = 1;
constexpr uint8_t kGhostBlockedMain = 2;
constexpr uint8_t kGhostBlocked = 3;
// Game time as the frame was gathered, for the pulsing red.
uint64_t g_game_ms = 0;

// This frame's entries and selection marks, built by DrawGroundLevel and
// drawn by it and DrawStanding; kept between frames so they never reallocate.
std::vector<RenderEntry>& Entries() {
    static std::vector<RenderEntry> entries;
    return entries;
}

std::vector<SelectionMark>& Marks() {
    static std::vector<SelectionMark> marks;
    return marks;
}

// Whose colour a sprite is drawn in: its owner's, or for a picture the fog
// keeps, its owner's then; player 1's for what belongs to nobody.
int ColourOf(const entt::registry& registry, entt::entity entity) {
    if (const auto* owner = registry.try_get<ECS::Components::Owner>(entity)) return owner->player_id;
    if (const auto* picture = registry.try_get<ECS::Components::Doppelganger>(entity)) {
        return picture->colour != Core::PLAYER_GAIA ? picture->colour : 1;
    }
    return 1;
}

std::vector<SDL_Point>& RingPoints() {
    static std::vector<SDL_Point> points;
    return points;
}

// Whether the dat draws an SLP at ground level: every graphic using it on
// layer 5 or under -- farms, rubble, cliffs, most shadows. Those layers are
// drawn first, through the ground's fog regions.
bool GroundLevel(int slp_id) {
    static const std::vector<uint8_t> table = [] {
        std::vector<uint8_t> t;
        auto& cfg = Core::ConfigManager::Get();
        constexpr int kGraphicIds = 8192;   // the dat holds 5,577
        for (int id = 0; id < kGraphicIds; ++id) {
            const auto info = cfg.GetGraphic(id);
            if (!info.Valid()) continue;
            const auto slp = static_cast<size_t>(info.slp_id);
            if (slp >= t.size()) t.resize(slp + 1, 0);
            // 1 ground, 2 standing; a standing use wins.
            t[slp] = std::max<uint8_t>(t[slp], info.layer <= 5 ? 1 : 2);
        }
        return t;
    }();
    return slp_id >= 0 && static_cast<size_t>(slp_id) < table.size() &&
           table[static_cast<size_t>(slp_id)] == 1;
}

// Draws one entry's sprite, or its placeholder.
void DrawSprite(SDL_Renderer* sdl_r, const RenderEntry& e) {
    if (e.tex_ptr) {
        SDL_Texture* tex = static_cast<SDL_Texture*>(e.tex_ptr);
        if (e.player_tex) {
            SDL_Texture* pt = static_cast<SDL_Texture*>(e.player_tex);
            if (e.flip_h) {
                SDL_RenderCopyExF(sdl_r, tex, &e.src, &e.dest, 0.0, nullptr, SDL_FLIP_HORIZONTAL);
                SDL_RenderCopyExF(sdl_r, pt, &e.src, &e.dest, 0.0, nullptr, SDL_FLIP_HORIZONTAL);
            } else {
                SDL_RenderCopyF(sdl_r, tex, &e.src, &e.dest);
                SDL_RenderCopyF(sdl_r, pt, &e.src, &e.dest);
            }
        } else if (e.flip_h) {
            SDL_RenderCopyExF(sdl_r, tex, &e.src, &e.dest, 0.0, nullptr, SDL_FLIP_HORIZONTAL);
        } else {
            SDL_RenderCopyF(sdl_r, tex, &e.src, &e.dest);
        }
    } else if (e.fallback_w > 0.0f && e.fallback_h > 0.0f) {
        uint8_t a = (e.fallback_color >> 24) & 0xFF;
        uint8_t r = (e.fallback_color >> 16) & 0xFF;
        uint8_t g = (e.fallback_color >> 8) & 0xFF;
        uint8_t b = e.fallback_color & 0xFF;
        SDL_SetRenderDrawColor(sdl_r, r, g, b, a);

        SDL_FRect dest = {
            e.sel_sx - e.fallback_w / 2.0f,
            e.sel_sy - e.fallback_h / 2.0f,
            static_cast<float>(e.fallback_w),
            static_cast<float>(e.fallback_h)
        };
        SDL_RenderFillRectF(sdl_r, &dest);
    }
}

// One texture of an entry into the fog's layer: in black, or wiping it.
void DrawInto(SDL_Renderer* sdl_r, SDL_Texture* tex, const RenderEntry& e, bool wipe) {
    Uint8 r = 255, g = 255, b = 255;
    SDL_BlendMode mode = SDL_BLENDMODE_BLEND;
    SDL_GetTextureColorMod(tex, &r, &g, &b);
    SDL_GetTextureBlendMode(tex, &mode);
    if (wipe) SDL_SetTextureBlendMode(tex, FogRenderer::EraseBlend());
    else SDL_SetTextureColorMod(tex, 0, 0, 0);
    if (e.flip_h) {
        SDL_RenderCopyExF(sdl_r, tex, &e.src, &e.dest, 0.0, nullptr, SDL_FLIP_HORIZONTAL);
    } else {
        SDL_RenderCopyF(sdl_r, tex, &e.src, &e.dest);
    }
    SDL_SetTextureColorMod(tex, r, g, b);
    SDL_SetTextureBlendMode(tex, mode);
}

} // namespace

void EntityRenderer::AddMoveMarker(float wx, float wy) {
    auto& markers = Markers();
    if (markers.size() >= kMaxMarkers) markers.erase(markers.begin());
    markers.push_back({wx, wy, SDL_GetTicks()});
}

void EntityRenderer::ClearMarkers() {
    Markers().clear();
}

void EntityRenderer::Flash(const entt::registry& registry, entt::entity target, bool ends_others) {
    if (!registry.valid(target)) return;
    // A building's piece stands for the building.
    if (const auto* part = registry.try_get<ECS::Components::PartOf>(target)) {
        if (registry.valid(part->building)) target = part->building;
    }
    auto& flashes = Flashes();
    // An order's flash ends whatever flash of the same kind is showing; a
    // conversion's ends none of them.
    Flashing* slot = nullptr;
    for (Flashing& f : flashes) {
        if (f.live && ends_others) f.live = false;
        if (!f.live) slot = &f;
    }
    if (slot == nullptr) return;
    *slot = {target, SDL_GetTicks(), true};
}

void EntityRenderer::ClearFlashes() {
    for (Flashing& f : Flashes()) f.live = false;
}

void EntityRenderer::ArtOffset(const entt::registry& registry, entt::entity entity, float& ox,
                               float& oy) {
    ox = 0.0f;
    oy = 0.0f;
    entt::entity building = entity;
    if (const auto* part = registry.try_get<ECS::Components::PartOf>(entity)) {
        if (!registry.valid(part->building)) return;
        building = part->building;
    }
    if (!registry.all_of<ECS::Components::Building, ECS::Components::Identity>(building)) return;
    const int unit_id = registry.get<ECS::Components::Identity>(building).unit_id;
    // A gate's art is laid at its middle already, arch and posts (ecs/Walls).
    if (ECS::Walls::IsGate(unit_id)) return;
    const ECS::Buildings::Footprint f = ECS::Buildings::FootprintOf(unit_id);
    ox = static_cast<float>(f.x.low + f.x.high) / 2.0f;
    oy = static_cast<float>(f.y.low + f.y.high) / 2.0f;
}

entt::entity EntityRenderer::PickAt(const entt::registry& registry, float vx, float vy) {
    const auto& entries = Entries();
    // Back to front as drawn: the last that covers the point is in front.
    for (size_t i = entries.size(); i-- > 0;) {
        const RenderEntry& e = entries[i];
        if (e.ent == entt::null || e.ghost != 0 || e.atlas == nullptr || e.atlas->opaque.empty()) continue;
        if (vx < e.dest.x || vy < e.dest.y || vx >= e.dest.x + e.dest.w || vy >= e.dest.y + e.dest.h) continue;
        int fx = static_cast<int>(vx - e.dest.x);
        const int fy = static_cast<int>(vy - e.dest.y);
        if (e.flip_h) fx = static_cast<int>(e.dest.w) - 1 - fx;
        if (!e.atlas->Covers(e.frame, fx, fy)) continue;
        if (!registry.valid(e.ent)) continue;
        if (const auto* part = registry.try_get<ECS::Components::PartOf>(e.ent)) {
            if (registry.valid(part->building)) return part->building;
        }
        if (!registry.all_of<ECS::Components::Selectable>(e.ent)) continue;
        return e.ent;
    }
    return entt::null;
}

void EntityRenderer::DrawGroundLevel(ECS::Simulation& sim, Renderer& renderer, SDL_Renderer* sdl_r, float alpha) {
    auto& registry = sim.GetRegistry();
    // Whether an entity stands on ground the player sees only in fog: its
    // tile, as ECS::Vision::Shows reads it, explored but not seen now.
    const auto* vision = registry.ctx().find<ECS::Vision::Map>();
    const bool fog_on = vision != nullptr && vision->reveal != Core::Reveal::AllVisible;
    auto in_fog = [&](entt::entity entity) {
        if (!fog_on || entity == entt::null) return false;
        entt::entity subject = entity;
        if (const auto* part = registry.try_get<ECS::Components::PartOf>(entity)) {
            if (registry.valid(part->building)) subject = part->building;
        }
        const auto* at = registry.try_get<ECS::Components::Transform>(subject);
        if (at == nullptr) return false;
        return !ECS::Vision::Visible(registry, Core::LocalPlayer(), Physics::TileOf(at->position.x),
                                     Physics::TileOf(at->position.y));
    };
    const auto& grid = sim.GetGrid();
    auto view = registry.view<ECS::Components::Transform, ECS::Components::Sprite>();
    
    float margin = 200.0f;
    float inv_zoom = 1.0f / renderer.camera_zoom;
    float cull_min_x = -margin;
    float cull_max_x = (renderer.view_w * inv_zoom) + margin;
    float cull_min_y = -margin;
    float cull_max_y = (renderer.view_h * inv_zoom) + margin;
    float cx = renderer.camera_x;
    float cy = renderer.camera_y;
    
    auto& entries = Entries();
    entries.clear();
    auto& marks = Marks();
    marks.clear();
    auto& cfg_ui = Core::ConfigManager::Get();
    // What is flashing this frame: its outline shows, green.
    static std::vector<entt::entity> flashing;
    flashing.clear();
    {
        const uint32_t now = SDL_GetTicks();
        for (Flashing& f : Flashes()) {
            if (!f.live) continue;
            const uint32_t elapsed = now - f.started;
            if (elapsed > kFlashMs || !registry.valid(f.target)) {
                f.live = false;
            } else if ((elapsed & kFlashBlinkBit) == 0) {
                flashing.push_back(f.target);
            }
        }
    }
    const HAL::PaletteColor flash_colour = HAL::GraphicCache::Get().GetPaletteColor(kFlashColour);
    auto flashing_now = [&](entt::entity entity) {
        return !flashing.empty() &&
               std::find(flashing.begin(), flashing.end(), entity) != flashing.end();
    };
    // What marks a selected or flashing entity, from its dat entry.
    auto mark_of = [&](entt::entity entity, float sx, float sy, bool selected, bool flash) -> int {
        SelectionMark m;
        if (flash) m.colour = SDL_Color{flash_colour.r, flash_colour.g, flash_colour.b, 255};
        m.sx = static_cast<int>(std::lround(sx));
        m.sy = static_cast<int>(std::lround(sy));
        if (const auto* id = registry.try_get<ECS::Components::Identity>(entity)) {
            Core::PlayerId owner = 0;
            if (const auto* o = registry.try_get<ECS::Components::Owner>(entity)) owner = o->player_id;
            const int art = ECS::Players::ArtOf(registry, owner, id->unit_id);
            const auto* ui = cfg_ui.GetUnitInterface(art);
            if (ui == nullptr) ui = cfg_ui.GetUnitInterface(id->unit_id);
            if (ui != nullptr) {
                m.rx = ui->outline_x;
                m.ry = ui->outline_y;
                m.round = ui->obstruction == 5;
                m.bar_height = ui->hp_bar_height;
                m.bar = ui->selection_effect == 0 || ui->selection_effect == 1 ||
                        ui->selection_effect == 5;
            }
        }
        if (const auto* hp = registry.try_get<ECS::Components::Health>(entity)) {
            m.hp = hp->current_hp;
            m.max_hp = hp->max_hp;
        } else {
            m.bar = false;
        }
        if (const auto* sel = registry.try_get<ECS::Components::Selectable>(entity)) {
            m.group = sel->group;
        }
        if (!selected) {
            m.bar = false;
            m.group = 0;
        }
        marks.push_back(m);
        return static_cast<int>(marks.size()) - 1;
    };
    
    // Sprites come from the DRS archives, decoded and packed into one atlas
    // per graphic.
    auto& graphics = HAL::GraphicCache::Get();
    
    view.each([&](auto entity, const auto& t, auto& sprite) {
        float lx = t.previous_position.x.ToFloat() + (t.position.x.ToFloat() - t.previous_position.x.ToFloat()) * alpha;
        float ly = t.previous_position.y.ToFloat() + (t.position.y.ToFloat() - t.previous_position.y.ToFloat()) * alpha;
        // A building stands about its footprint's middle, its art, outline
        // and health bar alike.
        {
            float ox = 0.0f, oy = 0.0f;
            ArtOffset(registry, entity, ox, oy);
            lx += ox;
            ly += oy;
        }
        
        // Inline WorldToScreen, standing on the ground's height there.
        float sx = (lx - ly) * (Core::IsometricMath::TILE_WIDTH / 2.0f) - cx;
        float sy = (lx + ly) * (Core::IsometricMath::TILE_HEIGHT / 2.0f) - cy -
                   Ground::HeightPx(grid, lx, ly);
        
        // Fast frustum culling
        if (sx < cull_min_x || sx > cull_max_x || sy < cull_min_y || sy > cull_max_y) {
            return;
        }
        // What the player at this machine cannot see is not drawn.
        if (!ECS::Vision::Shows(registry, Core::LocalPlayer(), entity)) return;
        
        RenderEntry e{};
        e.depth = lx + ly;
        e.ent = entity;
        e.fogged = in_fog(entity);
        e.is_selected = registry.any_of<ECS::Components::Selectable>(entity) && registry.get<ECS::Components::Selectable>(entity).is_selected;
        const bool flash = flashing_now(entity);
        e.sort_bias = sprite.sort_bias;
        e.shadow_tex = nullptr;
        e.shadow_flip = false;
        e.shadow_src = {0, 0, 0, 0};
        e.sel_sx = sx;
        e.sel_sy = sy;
        e.mark = -1;
        
        // A composite graphic -- a ship's hull, sails and wake -- draws as its
        // pieces, each from its own atlas with its own facings, in the dat's
        // layer order. The first piece carries the selection and health bar.
        if (const auto* pieces = registry.try_get<ECS::Components::SpritePieces>(entity)) {
            const int owner_player = ColourOf(registry, entity);
            bool first = true;
            for (int p = 0; p < pieces->count; ++p) {
                const ECS::Components::SpritePiece& piece = pieces->piece[p];
                const HAL::GraphicAtlas* piece_atlas =
                    graphics.GetAtlas(static_cast<uint32_t>(piece.slp_id));
                if (!piece_atlas || !piece_atlas->base) continue;
                // Of n facings the first n/2 + 1 are stored, clockwise from
                // south, and the rest mirror them.
                const int n = std::max(1, piece.angles);
                int angle = static_cast<int>(sprite.facing) * n / 8;
                bool flip = false;
                if (n > 1 && angle > n / 2) {
                    flip = true;
                    angle = n - angle;
                }
                const int per_angle = std::max(1, piece.frames_per_angle);
                const int frame = angle * per_angle + sprite.current_frame % per_angle;
                if (frame < 0 || static_cast<size_t>(frame) >= piece_atlas->frames.size()) continue;
                const HAL::GraphicFrame& gf = piece_atlas->frames[static_cast<size_t>(frame)];
                if (gf.rect.w <= 0) continue;
                RenderEntry part = e;
                part.sort_bias = sprite.sort_bias + piece.bias;
                part.is_selected = first && e.is_selected;
                if (first && (e.is_selected || flash)) {
                    part.mark = mark_of(entity, sx, sy, e.is_selected, flash);
                }
                part.tex_ptr = piece_atlas->base;
                part.ground = GroundLevel(piece.slp_id);
                part.atlas = piece_atlas;
                part.frame = frame;
                part.tex_w = piece_atlas->width;
                part.tex_h = piece_atlas->height;
                part.src = gf.rect;
                part.flip_h = flip;
                const int anchor_x = flip ? (gf.rect.w - gf.hotspot_x) : gf.hotspot_x;
                part.dest.x = sx + static_cast<float>(piece.offset_x - anchor_x);
                part.dest.y = sy + static_cast<float>(piece.offset_y - gf.hotspot_y) -
                              static_cast<float>(sprite.lift_px);
                part.dest.w = static_cast<float>(gf.rect.w);
                part.dest.h = static_cast<float>(gf.rect.h);
                part.player_tex =
                    graphics.GetPlayerLayer(static_cast<uint32_t>(piece.slp_id), owner_player);
                part.outline_player = owner_player;
                part.outline_tex = graphics.GetOutlineLayer(static_cast<uint32_t>(piece.slp_id));
                first = false;
                entries.push_back(part);
            }
            return;
        }

        // Pick the frame. AoE2 stores 5 of the 8 facings and mirrors the rest.
        if (sprite.is_file_sprite) {
            int angle_index = static_cast<int>(sprite.facing);
            sprite.flip_h = false;
            // Directions NE(5), E(6), SE(7) mirror NW(3), W(2), SW(1)
            if (angle_index >= 5) {
                sprite.flip_h = true;
                if (angle_index == 5) angle_index = 3;      // NE -> NW
                else if (angle_index == 6) angle_index = 2;  // E -> W
                else if (angle_index == 7) angle_index = 1;  // SE -> SW
            }
            sprite.slp_frame = angle_index * sprite.frames_per_angle + sprite.current_frame;
        }
        
        e.flip_h = sprite.flip_h;
        e.player_tex = nullptr;
        e.outline_tex = nullptr;
        e.outline_player = 1;
        e.tex_ptr = nullptr;
        
        const HAL::GraphicAtlas* atlas = sprite.slp_id >= 0
            ? graphics.GetAtlas(static_cast<uint32_t>(sprite.slp_id))
            : nullptr;

        // A shadow sprite draws its graphic's shadow layer and nothing else, so
        // it lands in the shadow pass beneath every piece of the building.
        if (sprite.is_shadow) {
            SDL_Texture* sh_tex = atlas
                ? graphics.GetShadowLayer(static_cast<uint32_t>(sprite.slp_id))
                : nullptr;
            if (sh_tex && !atlas->frames.empty()) {
                const HAL::GraphicFrame& sgf = atlas->frames[0];
                if (sgf.rect.w > 0) {
                    // Cropping to a sub-rectangle moves the anchor with it: the
                    // hotspot is expressed against the full frame.
                    SDL_Rect src = sgf.rect;
                    int hx = sgf.hotspot_x;
                    int hy = sgf.hotspot_y;
                    if (sprite.sub_w > 0) {
                        src.x += sprite.sub_x;
                        src.y += sprite.sub_y;
                        src.w = sprite.sub_w;
                        src.h = sprite.sub_h;
                        hx -= sprite.sub_x;
                        hy -= sprite.sub_y;
                    }
                    const int ax = e.flip_h ? (src.w - hx) : hx;
                    e.shadow_tex = sh_tex;
                    e.shadow_src = src;
                    e.shadow_flip = e.flip_h;
                    e.shadow_dest.x = sx - static_cast<float>(ax);
                    e.shadow_dest.y = sy - static_cast<float>(hy);
                    e.shadow_dest.w = static_cast<float>(src.w);
                    e.shadow_dest.h = static_cast<float>(src.h);
                    entries.push_back(e);
                }
            }
            return;
        }

        if (atlas && atlas->base && sprite.slp_frame >= 0
            && static_cast<size_t>(sprite.slp_frame) < atlas->frames.size()
            && atlas->frames[sprite.slp_frame].rect.w > 0) {
            const HAL::GraphicFrame& gf = atlas->frames[sprite.slp_frame];
            e.tex_ptr = atlas->base;
            e.ground = GroundLevel(sprite.slp_id);
            e.atlas = atlas;
            e.frame = sprite.slp_frame;
            e.tex_w = atlas->width;
            e.tex_h = atlas->height;
            e.src = gf.rect;
            // The hotspot is the texel that sits on the tile, from the SLP
            // frame header. Mirroring a frame mirrors its hotspot too, so a
            // flipped frame anchors at (width - hotspot_x).
            const int anchor_x = e.flip_h ? (gf.rect.w - gf.hotspot_x) : gf.hotspot_x;
            e.dest.x = sx - static_cast<float>(anchor_x);
            e.dest.y = sy - static_cast<float>(gf.hotspot_y) -
                       static_cast<float>(sprite.lift_px);
            e.dest.w = static_cast<float>(gf.rect.w);
            e.dest.h = static_cast<float>(gf.rect.h);
            
            const int owner_player = ColourOf(registry, entity);
            e.player_tex = graphics.GetPlayerLayer(
                static_cast<uint32_t>(sprite.slp_id), owner_player);
            e.outline_player = owner_player;
            e.outline_tex = graphics.GetOutlineLayer(static_cast<uint32_t>(sprite.slp_id));
            
            // Companion shadow graphic, resolved once per graphic.
            if (!sprite.shadow_resolved) {
                sprite.shadow_resolved = true;
                sprite.shadow_slp = Core::ConfigManager::Get().GetShadowSlp(sprite.slp_id);
            }
            if (sprite.shadow_slp >= 0) {
                const HAL::GraphicAtlas* sh_atlas =
                    graphics.GetAtlas(static_cast<uint32_t>(sprite.shadow_slp));
                SDL_Texture* sh_tex =
                    graphics.GetShadowLayer(static_cast<uint32_t>(sprite.shadow_slp));
                if (sh_atlas && sh_tex && !sh_atlas->frames.empty()) {
                    const size_t sf = static_cast<size_t>(sprite.slp_frame) < sh_atlas->frames.size()
                                    ? static_cast<size_t>(sprite.slp_frame) : 0;
                    const HAL::GraphicFrame& sgf = sh_atlas->frames[sf];
                    if (sgf.rect.w > 0) {
                        e.shadow_tex = sh_tex;
                        e.shadow_src = sgf.rect;
                        e.shadow_dest.x = sx - static_cast<float>(sgf.hotspot_x);
                        e.shadow_dest.y = sy - static_cast<float>(sgf.hotspot_y);
                        e.shadow_dest.w = static_cast<float>(sgf.rect.w);
                        e.shadow_dest.h = static_cast<float>(sgf.rect.h);
                    }
                }
            }
        } else {
            // No art: a flat placeholder rectangle (foundations, projectiles).
            e.fallback_color = sprite.color;
            e.fallback_w = static_cast<float>(sprite.width);
            e.fallback_h = static_cast<float>(sprite.height);
        }
        
        if (e.is_selected || flash) e.mark = mark_of(entity, sx, sy, e.is_selected, flash);
        entries.push_back(e);
    });
    
    // The order markers, depth-sorted with everything else: presentation
    // only, timed on the wall clock.
    {
        auto& cfg = Core::ConfigManager::Get();
        const uint32_t now = SDL_GetTicks();
        auto add_marker = [&](int graphic, float wx, float wy, size_t frame, int player) {
            const auto info = cfg.GetGraphic(graphic);
            if (!info.Valid()) return;
            const HAL::GraphicAtlas* atlas = graphics.GetAtlas(static_cast<uint32_t>(info.slp_id));
            if (!atlas || !atlas->base || frame >= atlas->frames.size()) return;
            const HAL::GraphicFrame& gf = atlas->frames[frame];
            if (gf.rect.w <= 1) return;
            const float mx = (wx - wy) * (Core::IsometricMath::TILE_WIDTH / 2.0f) - cx;
            const float my = (wx + wy) * (Core::IsometricMath::TILE_HEIGHT / 2.0f) - cy -
                             Ground::HeightPx(grid, wx, wy);
            if (mx < cull_min_x || mx > cull_max_x || my < cull_min_y || my > cull_max_y) return;
            RenderEntry e{};
            e.depth = wx + wy;
            e.ent = entt::null;
            e.tex_ptr = atlas->base;
            e.atlas = atlas;
            e.frame = static_cast<int>(frame);
            e.tex_w = atlas->width;
            e.tex_h = atlas->height;
            e.src = gf.rect;
            e.dest = {mx - static_cast<float>(gf.hotspot_x), my - static_cast<float>(gf.hotspot_y),
                      static_cast<float>(gf.rect.w), static_cast<float>(gf.rect.h)};
            e.sel_sx = mx;
            e.sel_sy = my;
            e.player_tex = graphics.GetPlayerLayer(static_cast<uint32_t>(info.slp_id), player);
            entries.push_back(e);
        };
        // A move's arrows play through once and are gone.
        const int move_ms = std::max(1, cfg.GetFrameMs(kMoveMarkerGraphic, 60));
        auto& markers = Markers();
        std::erase_if(markers, [&](const Marker& m) {
            return (now - m.started) / static_cast<uint32_t>(move_ms) >=
                   static_cast<uint32_t>(std::max(1, cfg.GetGraphic(kMoveMarkerGraphic).frame_count));
        });
        for (const Marker& m : markers) {
            add_marker(kMoveMarkerGraphic, m.wx, m.wy,
                       (now - m.started) / static_cast<uint32_t>(move_ms), Core::LocalPlayer());
        }
        // The selected building's gather point: the waypoint flag, waving,
        // in its owner's colour.
        const int flag_ms = std::max(1, cfg.GetFrameMs(kWaypointGraphic, 100));
        const int flag_frames = std::max(1, cfg.GetGraphic(kWaypointGraphic).frame_count);
        auto rallies = registry.view<ECS::Components::RallyPoint, ECS::Components::Selectable,
                                     ECS::Components::Owner>();
        for (auto b : rallies) {
            if (!rallies.get<ECS::Components::Selectable>(b).is_selected) continue;
            const Core::PlayerId owner = rallies.get<ECS::Components::Owner>(b).player_id;
            if (owner != Core::LocalPlayer()) continue;
            const auto& rally = rallies.get<ECS::Components::RallyPoint>(b);
            add_marker(kWaypointGraphic, rally.at.x.ToFloat(), rally.at.y.ToFloat(),
                       (now / static_cast<uint32_t>(flag_ms)) % static_cast<uint32_t>(flag_frames),
                       owner);
        }
        // A selected unit's waypoints: the same flag at the point it is
        // walking to and at each laid after it.
        auto routed = registry.view<ECS::Components::Waypoints, ECS::Components::Selectable,
                                    ECS::Components::Owner>();
        for (auto u : routed) {
            if (!routed.get<ECS::Components::Selectable>(u).is_selected) continue;
            const Core::PlayerId owner = routed.get<ECS::Components::Owner>(u).player_id;
            if (owner != Core::LocalPlayer()) continue;
            const uint32_t frame = (now / static_cast<uint32_t>(flag_ms)) % static_cast<uint32_t>(flag_frames);
            if (const auto* path = registry.try_get<ECS::Components::Path>(u); path != nullptr && path->has_goal) {
                add_marker(kWaypointGraphic, path->goal.x.ToFloat(), path->goal.y.ToFloat(), frame, owner);
            }
            const auto& route = routed.get<ECS::Components::Waypoints>(u);
            for (int i = 0; i < route.count; ++i) {
                const auto& p = route.points[static_cast<size_t>(i)];
                add_marker(kWaypointGraphic, p.x.ToFloat(), p.y.ToFloat(), frame, owner);
            }
        }
    }

    // The building being placed, depth-sorted among the world's own pieces
    // (rendering/Placement.h).
    {
        static std::vector<Placement::Piece> pieces;
        Placement::Outline outline;
        g_game_ms = sim.Tick() * 16;
        if (Placement::Collect(sim, renderer, pieces, outline)) {
            if (outline.shown) {
                SelectionMark m;
                m.sx = static_cast<int>(std::lround(
                    (outline.wx - outline.wy) * (Core::IsometricMath::TILE_WIDTH / 2.0f) - cx));
                m.sy = static_cast<int>(std::lround(
                    (outline.wx + outline.wy) * (Core::IsometricMath::TILE_HEIGHT / 2.0f) - cy -
                    Ground::HeightPx(grid, outline.wx, outline.wy)));
                m.rx = outline.rx;
                m.ry = outline.ry;
                m.round = outline.round;
                m.bar = false;
                const HAL::PaletteColor c = graphics.GetPaletteColor(static_cast<size_t>(
                    outline.clear ? Placement::kOutlineClear : Placement::kOutlineBlocked));
                m.colour = SDL_Color{c.r, c.g, c.b, 255};
                marks.push_back(m);
                // An entry of its own, under the pieces standing there, so the
                // outline shows even with no art: a Farm's field (SLP 419) is
                // not on the disc.
                RenderEntry e{};
                e.depth = outline.wx + outline.wy;
                e.sort_bias = -1;
                e.ent = entt::null;
                e.mark = static_cast<int>(marks.size()) - 1;
                entries.push_back(e);
            }
            for (const Placement::Piece& piece : pieces) {
                const HAL::GraphicFrame& gf = piece.atlas->frames[static_cast<size_t>(piece.frame)];
                const float px = (piece.wx - piece.wy) * (Core::IsometricMath::TILE_WIDTH / 2.0f) - cx;
                const float py = (piece.wx + piece.wy) * (Core::IsometricMath::TILE_HEIGHT / 2.0f) -
                                 cy - Ground::HeightPx(grid, piece.wx, piece.wy);
                RenderEntry e{};
                e.depth = piece.wx + piece.wy;
                e.ent = entt::null;
                e.tex_ptr = piece.atlas->base;
                e.atlas = piece.atlas;
                e.frame = piece.frame;
                e.tex_w = piece.atlas->width;
                e.tex_h = piece.atlas->height;
                e.src = gf.rect;
                e.dest = {px - static_cast<float>(gf.hotspot_x), py - static_cast<float>(gf.hotspot_y),
                          static_cast<float>(gf.rect.w), static_cast<float>(gf.rect.h)};
                e.sel_sx = px;
                e.sel_sy = py;
                e.player_tex = piece.player;
                e.ghost = piece.clear ? kGhostClear : piece.main ? kGhostBlockedMain : kGhostBlocked;
                entries.push_back(e);
            }
        }
    }

    // Fire on a damaged building: the dat's damage graphic for how hurt it
    // is, drawn over it. Each stage is a composite -- a dozen flame graphics
    // at pixel offsets from the building unit's own anchor -- and the stages
    // are thresholds of damage taken (25%, 50%, 75%), the heaviest reached
    // showing. Presentation only: which stage is derived from hit points each
    // frame, and the flames animate on the wall clock.
    {
        auto& cfg = Core::ConfigManager::Get();
        const uint32_t now = SDL_GetTicks();
        auto buildings = registry.view<ECS::Components::Building, ECS::Components::Health,
                                       ECS::Components::Identity, ECS::Components::Owner,
                                       ECS::Components::Transform>();
        for (auto b : buildings) {
            if (buildings.get<ECS::Components::Building>(b).state !=
                ECS::Components::BuildingState::Complete) {
                continue;
            }
            const auto& hp = buildings.get<ECS::Components::Health>(b);
            if (hp.max_hp <= 0 || hp.current_hp >= hp.max_hp) continue;
            if (!ECS::Vision::Shows(registry, Core::LocalPlayer(), b)) continue;
            const int damage = 100 - hp.current_hp * 100 / hp.max_hp;
            const Core::PlayerId owner = buildings.get<ECS::Components::Owner>(b).player_id;
            const int unit_id = buildings.get<ECS::Components::Identity>(b).unit_id;
            const auto* rules = ECS::Players::UnitOf(registry, owner, unit_id);
            if (rules == nullptr) continue;
            int stage = -1;
            int reached = -1;
            for (int i = 0; i < rules->damage_graphic_count; ++i) {
                const auto& dg = rules->damage_graphics[i];
                if (damage >= dg.percent && dg.percent > reached) {
                    reached = dg.percent;
                    stage = dg.graphic;
                }
            }
            if (stage < 0) continue;

            // Anchored on the building unit's own piece, which in a composed
            // building need not be the origin tile.
            Core::Fixed32 ax = buildings.get<ECS::Components::Transform>(b).position.x;
            Core::Fixed32 ay = buildings.get<ECS::Components::Transform>(b).position.y;
            const int art = ECS::Players::ArtOf(registry, owner, unit_id);
            const auto* pieces = cfg.GetAnnexes(art);
            if (pieces == nullptr) pieces = cfg.GetAnnexes(unit_id);
            if (pieces != nullptr) {
                for (const auto& piece : *pieces) {
                    if (piece.unit_id == art || piece.unit_id == unit_id) {
                        ax = ax + piece.fx;
                        ay = ay + piece.fy;
                        break;
                    }
                }
            }
            float ox = 0.0f, oy = 0.0f;
            ArtOffset(registry, b, ox, oy);
            const float wx = ax.ToFloat() + ox;
            const float wy = ay.ToFloat() + oy;
            const float bx = (wx - wy) * (Core::IsometricMath::TILE_WIDTH / 2.0f) - cx;
            const float by = (wx + wy) * (Core::IsometricMath::TILE_HEIGHT / 2.0f) - cy -
                             Ground::HeightPx(grid, wx, wy);
            if (bx < cull_min_x || bx > cull_max_x || by < cull_min_y || by > cull_max_y) continue;

            auto add_flame = [&](int graphic, int ox, int oy, int phase) {
                const auto info = cfg.GetGraphic(graphic);
                if (!info.Valid()) return;
                const HAL::GraphicAtlas* atlas =
                    graphics.GetAtlas(static_cast<uint32_t>(info.slp_id));
                if (!atlas || !atlas->base || atlas->frames.empty()) return;
                const int frame_ms = std::max(1, cfg.GetFrameMs(graphic, 100));
                const size_t frame = (now / static_cast<uint32_t>(frame_ms) +
                                      static_cast<uint32_t>(phase)) % atlas->frames.size();
                const HAL::GraphicFrame& gf = atlas->frames[frame];
                if (gf.rect.w <= 0) return;
                RenderEntry e{};
                // In front of every piece of the building it burns on.
                e.depth = wx + wy + 3.0f;
                e.sort_bias = 1000;
                e.ent = b;
                e.fogged = in_fog(b);
                e.tex_ptr = atlas->base;
                e.tex_w = atlas->width;
                e.tex_h = atlas->height;
                e.src = gf.rect;
                e.dest = {bx + static_cast<float>(ox - gf.hotspot_x),
                          by + static_cast<float>(oy - gf.hotspot_y),
                          static_cast<float>(gf.rect.w), static_cast<float>(gf.rect.h)};
                e.sel_sx = bx;
                e.sel_sy = by;
                entries.push_back(e);
            };

            const auto& deltas = Core::Media::Deltas(stage);
            if (deltas.empty()) {
                add_flame(stage, 0, 0, 0);
            } else {
                int phase = 0;
                for (const auto& delta : deltas) {
                    // Staggered, so a dozen flames do not flicker in step.
                    add_flame(delta.graphic, delta.offset_x, delta.offset_y, phase);
                    phase += 7;
                }
            }
        }
    }

    // Depth sort (back-to-front painter's algorithm), depth compared exactly.
    // Ties fall to sort bias, then texture -- static map objects on integer
    // tiles tie exactly and batch by texture -- then entity id, so the order
    // is stable from frame to frame.
    std::sort(entries.begin(), entries.end(), [](const RenderEntry& a, const RenderEntry& b) {
        if (a.depth != b.depth) return a.depth < b.depth;
        if (a.sort_bias != b.sort_bias) return a.sort_bias < b.sort_bias;
        if (a.tex_ptr != b.tex_ptr) return a.tex_ptr < b.tex_ptr;
        return a.ent < b.ent;
    });
    
    // Shadows are a pass of their own, beneath every sprite, so one building
    // piece's shadow never lands on another piece drawn at the same depth.
    // Only the dat's shadow graphics cast shadows; a graphic with none casts
    // none.
    for (const auto& e : entries) {
        if (!e.shadow_tex) continue;
        SDL_Texture* shadow = static_cast<SDL_Texture*>(e.shadow_tex);
        SDL_SetTextureAlphaMod(shadow, 110);
        const SDL_Rect* src = e.shadow_src.w > 0 ? &e.shadow_src : nullptr;
        if (e.shadow_flip) {
            SDL_RenderCopyExF(sdl_r, shadow, src, &e.shadow_dest, 0.0, nullptr, SDL_FLIP_HORIZONTAL);
        } else {
            SDL_RenderCopyF(sdl_r, shadow, src, &e.shadow_dest);
        }
    }

    // Then what lies on the ground, before the fog goes over it all.
    for (const auto& e : entries) {
        if (!e.ground) continue;
        if (e.mark >= 0) DrawOutline(sdl_r, marks[static_cast<size_t>(e.mark)], RingPoints());
        DrawSprite(sdl_r, e);
    }
}

void EntityRenderer::DrawStanding(Renderer& renderer, SDL_Renderer* sdl_r) {
    auto& graphics = HAL::GraphicCache::Get();
    auto& entries = Entries();
    auto& marks = Marks();

    // Outline pass: a unit's silhouette shows through whatever hides it.
    // Anything taller than a tile occludes -- buildings and trees --
    // and only entries drawn later (greater depth) can cover an entity, so the
    // search runs forward from each candidate and stops once the depth gap is
    // wide enough that no sprite could still reach back over it.
    constexpr float kOccluderHeight = 64.0f;   // taller than one tile
    constexpr float kDepthWindow = 12.0f;      // tiles ahead worth testing
    for (size_t i = 0; i < entries.size(); ++i) {
        RenderEntry& e = entries[i];
        if (!e.outline_tex || !e.tex_ptr || e.ground || e.dest.h > kOccluderHeight) continue;
        bool covered = false;
        // Points down the middle of the unit: head, chest and feet. It counts
        // as hidden only where the occluder's own art is opaque at one of them
        // -- a tree's bounding box reaches far past its leaves.
        const float mid_x = e.dest.x + e.dest.w * 0.5f;
        const float probe_y[3] = {e.dest.y + e.dest.h * 0.25f, e.dest.y + e.dest.h * 0.5f,
                                  e.dest.y + e.dest.h * 0.85f};
        for (size_t j = i + 1; j < entries.size() && !covered; ++j) {
            const RenderEntry& o = entries[j];
            if (o.depth - e.depth > kDepthWindow) break;
            if (!o.tex_ptr || o.ground || o.dest.h <= kOccluderHeight) continue;
            const bool boxes = e.dest.x < o.dest.x + o.dest.w && o.dest.x < e.dest.x + e.dest.w
                            && e.dest.y < o.dest.y + o.dest.h && o.dest.y < e.dest.y + e.dest.h;
            if (!boxes) continue;
            if (o.atlas == nullptr || o.atlas->opaque.empty()) {
                covered = true;
                continue;
            }
            for (float py : probe_y) {
                int fx = static_cast<int>(mid_x - o.dest.x);
                const int fy = static_cast<int>(py - o.dest.y);
                if (o.flip_h) fx = static_cast<int>(o.dest.w) - 1 - fx;
                if (o.atlas->Covers(o.frame, fx, fy)) { covered = true; break; }
            }
        }
        if (!covered) e.outline_tex = nullptr;
    }

    for (const auto& e : entries) {
        if (e.ground) continue;
        // The outline lies on the ground under its own sprite, so whatever
        // stands in front of it -- the unit itself included -- covers it.
        if (e.mark >= 0) DrawOutline(sdl_r, marks[static_cast<size_t>(e.mark)], RingPoints());
        DrawSprite(sdl_r, e);
    }

    // The building being placed: the blitter's masks over its pieces, a pass
    // for each, wiped wherever something stands in front of it.
    {
        size_t first = entries.size();
        bool kinds[4] = {false, false, false, false};
        for (size_t i = 0; i < entries.size(); ++i) {
            if (entries[i].ghost == 0) continue;
            first = std::min(first, i);
            kinds[entries[i].ghost] = true;
        }
        for (uint8_t kind = kGhostClear; first < entries.size() && kind <= kGhostBlocked; ++kind) {
            if (!kinds[kind] || !Stipple::Begin(renderer, sdl_r)) continue;
            for (size_t i = first; i < entries.size(); ++i) {
                const RenderEntry& e = entries[i];
                if (e.ground || !e.tex_ptr) continue;
                const bool wipe = e.ghost != kind;
                DrawInto(sdl_r, static_cast<SDL_Texture*>(e.tex_ptr), e, wipe);
                if (e.player_tex) DrawInto(sdl_r, static_cast<SDL_Texture*>(e.player_tex), e, wipe);
            }
            SDL_Color colour{0, 0, 0, 255};
            if (kind != kGhostClear) {
                const HAL::PaletteColor c = graphics.GetPaletteColor(static_cast<size_t>(
                    Placement::BlockedColour(g_game_ms, kind == kGhostBlockedMain)));
                colour = SDL_Color{c.r, c.g, c.b, 255};
            }
            Stipple::End(sdl_r, kind == kGhostClear ? Stipple::Pattern::Quarter
                                                    : Stipple::Pattern::Half,
                         colour);
        }
    }

    // What stands in the fog is dithered whole (FogRenderer.h): its
    // silhouette in black into the fog's layer, in the same back-to-front
    // order, wiped again wherever something seen is drawn in front of it.
    size_t first = entries.size();
    SDL_FRect box{0, 0, 0, 0};
    for (size_t i = 0; i < entries.size(); ++i) {
        const RenderEntry& e = entries[i];
        if (!e.fogged || e.ground || !e.tex_ptr) continue;
        if (first == entries.size()) {
            first = i;
            box = e.dest;
        } else {
            const float x1 = std::max(box.x + box.w, e.dest.x + e.dest.w);
            const float y1 = std::max(box.y + box.h, e.dest.y + e.dest.h);
            box.x = std::min(box.x, e.dest.x);
            box.y = std::min(box.y, e.dest.y);
            box.w = x1 - box.x;
            box.h = y1 - box.y;
        }
    }
    if (first < entries.size() && FogRenderer::BeginObjects(renderer, sdl_r)) {
        for (size_t i = first; i < entries.size(); ++i) {
            const RenderEntry& e = entries[i];
            if (e.ground || !e.tex_ptr) continue;
            const bool wipe = !e.fogged;
            if (wipe && (e.dest.x >= box.x + box.w || e.dest.x + e.dest.w <= box.x ||
                         e.dest.y >= box.y + box.h || e.dest.y + e.dest.h <= box.y)) {
                continue;
            }
            DrawInto(sdl_r, static_cast<SDL_Texture*>(e.tex_ptr), e, wipe);
            if (e.player_tex) DrawInto(sdl_r, static_cast<SDL_Texture*>(e.player_tex), e, wipe);
        }
        FogRenderer::EndObjects(renderer, sdl_r);
    }

    // Drawn after everything, tinted with the owner's colour.
    for (const auto& e : entries) {
        if (!e.outline_tex) continue;
        SDL_Texture* ol = static_cast<SDL_Texture*>(e.outline_tex);
        const HAL::PaletteColor pc = graphics.GetPlayerColor(e.outline_player);
        SDL_SetTextureColorMod(ol, pc.r, pc.g, pc.b);
        if (e.flip_h) {
            SDL_RenderCopyExF(sdl_r, ol, &e.src, &e.dest, 0.0, nullptr, SDL_FLIP_HORIZONTAL);
        } else {
            SDL_RenderCopyF(sdl_r, ol, &e.src, &e.dest);
        }
    }

    // Hit point bars and group numbers float over everything in the world.
    for (const auto& m : marks) {
        DrawHealthBar(sdl_r, m);
        DrawGroupNumber(sdl_r, m);
    }
}

} // namespace Rendering
