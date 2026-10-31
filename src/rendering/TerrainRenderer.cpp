#include "TerrainRenderer.h"
#include "Ground.h"
#include "Renderer.h"
#include "core/IsometricMath.h"
#include "hal/TextureManager.h"
#include "ecs/Fog.h"
#include "hal/GraphicCache.h"
#include "core/ConfigManager.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace Rendering {

// Which ground draws over which where they meet: the dat's blend priority.
static int GetTerrainPriority(Core::TerrainType t) {
    return Core::ConfigManager::Get().GetTerrainArt(t).blend_priority;
}

// Which mask set a transition uses, from the two grounds' blend types -- the
// ground being drawn over picks the row, the ground flooding onto it the
// column. The table names eight mask sets; blendomatic.dat holds four, and
// the ones it lacks (the ice and snow shapes) fall back to the ordinary edge.
// The short set (3) is the one farms make with everything.
static int GetBlendMode(Core::TerrainType base, Core::TerrainType over, int mask_sets) {
    static constexpr int kLookup[8][8] = {
        {2, 3, 2, 1, 1, 6, 5, 4},
        {3, 3, 3, 1, 1, 6, 5, 4},
        {2, 3, 2, 1, 1, 6, 1, 4},
        {1, 1, 1, 0, 7, 6, 5, 4},
        {1, 1, 1, 7, 7, 6, 5, 4},
        {6, 6, 6, 6, 6, 6, 5, 4},
        {5, 5, 1, 5, 5, 5, 5, 4},
        {4, 3, 4, 4, 4, 4, 4, 4},
    };
    auto& cfg = Core::ConfigManager::Get();
    const int row = std::clamp(cfg.GetTerrainArt(base).blend_type, 0, 7);
    const int column = std::clamp(cfg.GetTerrainArt(over).blend_type, 0, 7);
    const int mode = kLookup[row][column];
    return mode < mask_sets ? mode : 0;
}

// Terrain blending through the masks of blendomatic.dat: 31 alpha masks per
// blending mode, one per pattern of higher-priority neighbours, the dominant
// terrain drawn through the matching mask. The masks live in one sheet: 31
// across, one 49px row per blending mode.
namespace {

constexpr int kMaskW = 97;
constexpr int kMaskH = 49;

// Neighbour bit layout the mask table is indexed by:
//     0 1 2
//     7 . 3
//     6 5 4
// so the edge-adjacent neighbours are the odd bits and the corners the even.
constexpr int kNeighbourDx[8] = {-1,  0,  1, 1, 1, 0, -1, -1};
constexpr int kNeighbourDy[8] = {-1, -1, -1, 0, 1, 1,  1,  0};

SDL_Texture* g_masks = nullptr;
SDL_Texture* g_scratch = nullptr;
bool g_masks_tried = false;

// How many mask sets the sheet holds: one 49-pixel row each.
int g_mask_sets = 1;
int MaskSets() { return g_mask_sets; }

SDL_Texture* BlendMasks(SDL_Renderer* sdl_r) {
    if (!g_masks_tried) {
        g_masks_tried = true;
        HAL::TextureManager::Get().LoadTexture(
            "terrain_blendmasks", "../assets/sprites/terrain/blendmasks.png");
        g_masks = HAL::TextureManager::Get().GetTexture("terrain_blendmasks");
        int w = 0;
        int h = 0;
        if (g_masks && SDL_QueryTexture(g_masks, nullptr, nullptr, &w, &h) == 0) {
            g_mask_sets = std::max(1, h / kMaskH);
        }
        g_scratch = SDL_CreateTexture(sdl_r, SDL_PIXELFORMAT_RGBA8888,
                                      SDL_TEXTUREACCESS_TARGET, kMaskW, kMaskH);
        if (g_scratch) SDL_SetTextureBlendMode(g_scratch, SDL_BLENDMODE_BLEND);
    }
    return g_masks;
}

// Which masks a pattern of influencing neighbours selects. Adjacent edges pick
// one of four interchangeable variants; each corner adds its own mask, but only
// when neither edge beside it is already blending -- otherwise the corner is
// already covered.
int AdjacentMask(uint8_t influence, int variant) {
    switch (influence) {
    case 0b00001000: return 0 + variant;
    case 0b00000010: return 4 + variant;
    case 0b00100000: return 8 + variant;
    case 0b10000000: return 12 + variant;
    case 0b00100010: return 20;
    case 0b10001000: return 21;
    case 0b10100000: return 22;
    case 0b10000010: return 23;
    case 0b00101000: return 24;
    case 0b00001010: return 25;
    case 0b00101010: return 26;
    case 0b10101000: return 27;
    case 0b10100010: return 28;
    case 0b10001010: return 29;
    case 0b10101010: return 30;
    default:         return -1;
    }
}

// Draws one terrain tile through one mask, by multiplying the tile's alpha by
// the mask's in a scratch target and blitting the result.
void DrawMasked(SDL_Renderer* sdl_r, const TerrainRenderer::TerrainTile& terrain,
                SDL_Texture* masks, int mask_id, int mode, const SDL_FRect& dest) {
    if (!g_scratch) return;

    static const SDL_BlendMode multiply_alpha = SDL_ComposeCustomBlendMode(
        SDL_BLENDFACTOR_ZERO, SDL_BLENDFACTOR_ONE, SDL_BLENDOPERATION_ADD,
        SDL_BLENDFACTOR_ZERO, SDL_BLENDFACTOR_SRC_ALPHA, SDL_BLENDOPERATION_ADD);

    SDL_Texture* prev = SDL_GetRenderTarget(sdl_r);
    SDL_SetRenderTarget(sdl_r, g_scratch);
    SDL_SetRenderDrawBlendMode(sdl_r, SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(sdl_r, 0, 0, 0, 0);
    SDL_RenderClear(sdl_r);

    SDL_SetTextureBlendMode(terrain.atlas, SDL_BLENDMODE_NONE);
    SDL_RenderCopy(sdl_r, terrain.atlas, &terrain.src, nullptr);

    const SDL_Rect msrc = {mask_id * kMaskW, mode * kMaskH, kMaskW, kMaskH};
    SDL_SetTextureBlendMode(masks, multiply_alpha);
    SDL_RenderCopy(sdl_r, masks, &msrc, nullptr);
    SDL_SetTextureBlendMode(masks, SDL_BLENDMODE_BLEND);
    SDL_SetTextureBlendMode(terrain.atlas, SDL_BLENDMODE_BLEND);

    SDL_SetRenderTarget(sdl_r, prev);
    SDL_RenderCopyF(sdl_r, g_scratch, nullptr, &dest);
}

} // namespace

// Whose memory of the ground the chunks are baked from, and the world it is
// kept in: set by DrawTerrain each frame.
static const entt::registry* g_registry = nullptr;
static Core::PlayerId g_viewer = 0;

// The terrain the player at this machine sees on a tile: what it remembers of
// ground changed out of its sight (ecs/Fog), or the ground itself.
static Core::TerrainType TerrainAt(const Physics::Grid& grid, int x, int y) {
    const Core::TerrainType now = grid.GetTile(x, y).terrain;
    return g_registry != nullptr ? ECS::Fog::GroundSeen(*g_registry, g_viewer, x, y, now) : now;
}

static void RenderBlendOverlays(
    SDL_Renderer* sdl_r,
    const Physics::Grid& grid,
    int x, int y,
    const SDL_FRect& dest,
    Core::TerrainType my_terrain,
    uint8_t my_variant)
{
    SDL_Texture* masks = BlendMasks(sdl_r);
    if (!masks) return;

    const int my_prio = GetTerrainPriority(my_terrain);

    // Collect the distinct higher-priority terrains touching this tile, and
    // which of the eight neighbours each one occupies.
    std::vector<std::pair<Core::TerrainType, uint8_t>> influences;
    for (int n = 0; n < 8; ++n) {
        const int nx = x + kNeighbourDx[n];
        const int ny = y + kNeighbourDy[n];
        if (!grid.InBounds(nx, ny)) continue;
        const Core::TerrainType nt = TerrainAt(grid, nx, ny);
        if (GetTerrainPriority(nt) <= my_prio) continue;
        auto it = std::find_if(influences.begin(), influences.end(),
                               [nt](const auto& p) { return p.first == nt; });
        if (it == influences.end()) influences.emplace_back(nt, uint8_t(1u << n));
        else it->second |= uint8_t(1u << n);
    }

    std::sort(influences.begin(), influences.end(),
              [](const auto& a, const auto& b) {
                  return GetTerrainPriority(a.first) < GetTerrainPriority(b.first);
              });

    // One of four interchangeable variants, chosen from the tile position so a
    // field of the same transition does not repeat one shape.
    const int variant = ((x * 7 + y * 13) & 3);

    for (const auto& inf : influences) {
        const auto tile = TerrainRenderer::GetTerrainTile(inf.first, my_variant);
        if (!tile.atlas) continue;

        const uint8_t bits = inf.second;
        const uint8_t adjacent = bits & 0b10101010u;
        const int mode = GetBlendMode(my_terrain, inf.first, MaskSets());

        const int adj_mask = AdjacentMask(adjacent, variant);
        if (adj_mask >= 0) DrawMasked(sdl_r, tile, masks, adj_mask, mode, dest);

        // Corners, skipped where an edge beside them already blends.
        static const int kCornerBit[4]  = {2, 4, 0, 6};
        static const int kCornerMask[4] = {16, 17, 18, 19};
        for (int c = 0; c < 4; ++c) {
            const int bit = kCornerBit[c];
            if (!(bits & (1u << bit))) continue;
            const uint8_t left = uint8_t(1u << ((bit + 7) % 8));
            const uint8_t right = uint8_t(1u << ((bit + 1) % 8));
            if (adjacent & (left | right)) continue;
            DrawMasked(sdl_r, tile, masks, kCornerMask[c], mode, dest);
        }
    }
}


TerrainRenderer::TerrainTile TerrainRenderer::GetTerrainTile(
        Core::TerrainType type, uint8_t variant) {
    TerrainTile tile;
    const auto& info = Core::ConfigManager::Get().GetTerrainArt(type);
    if (info.slp_id == 0 || info.frame_count <= 0) return tile;

    const HAL::GraphicAtlas* atlas =
        HAL::GraphicCache::Get().GetTerrainAtlas(info.slp_id);
    if (!atlas || !atlas->base) return tile;

    // Terrains do not all use the same pattern size -- most tile 10x10, the
    // farms 3x3 -- so the cell index wraps on the terrain's own frame count.
    const int frame = variant % info.frame_count;
    if (frame >= static_cast<int>(atlas->frames.size())) return tile;

    tile.atlas = atlas->base;
    tile.src = atlas->frames[frame].rect;
    return tile;
}

std::unordered_map<uint64_t, TerrainChunk> TerrainRenderer::chunks;

namespace {

// Chunks are baked as the viewport first touches them, at most a few per
// frame so scrolling never hitches, and chunks out of view are evicted once
// the budget is exceeded: kMaxLiveChunks, or half as many again as are in
// view when that is more.
constexpr int kBakesPerFrame = 4;
constexpr size_t kMaxLiveChunks = 64;
// Zoomed out further than this, chunks are baked at half their size: a
// quarter of the memory and the drawing, at no cost to what shows.
constexpr float kHalfZoom = 0.6f;

uint64_t g_frame = 0;

// A chunk's key: its place, and whether it is the half-size bake.
uint64_t ChunkKey(int cx, int cy, bool half) {
    return (static_cast<uint64_t>(half) << 63)
         | (static_cast<uint64_t>(static_cast<uint32_t>(cx) & 0x7fffffffu) << 32)
         | static_cast<uint32_t>(cy);
}

} // namespace

void TerrainRenderer::DrawWaterLayer(ECS::Simulation& sim, Renderer& renderer, SDL_Renderer* sdl_r) {
    // Water is baked into the terrain chunks; no separate layer is drawn.
    (void)sim; (void)renderer; (void)sdl_r;
}

void TerrainRenderer::InvalidateChunks() {
    for (auto& entry : chunks) {
        if (entry.second.texture) SDL_DestroyTexture(entry.second.texture);
    }
    chunks.clear();
}

// Bakes one chunk's tiles and blend overlays into its own texture.
static TerrainChunk BakeChunk(SDL_Renderer* sdl_r, const Physics::Grid& grid,
                              int cx, int cy, bool half) {
    TerrainChunk chunk;
    chunk.grid_x = cx;
    chunk.grid_y = cy;

    const int CS = TerrainRenderer::CHUNK_SIZE;
    auto top    = Core::IsometricMath::WorldToScreen(Core::Fixed32(cx * CS),      Core::Fixed32(cy * CS));
    auto right  = Core::IsometricMath::WorldToScreen(Core::Fixed32(cx * CS + CS), Core::Fixed32(cy * CS));
    auto bottom = Core::IsometricMath::WorldToScreen(Core::Fixed32(cx * CS + CS), Core::Fixed32(cy * CS + CS));
    auto left   = Core::IsometricMath::WorldToScreen(Core::Fixed32(cx * CS),      Core::Fixed32(cy * CS + CS));

    // Room above for hills: a tile on high ground is drawn higher up the
    // screen than its flat position.
    constexpr float kHeadroom = 8.0f * Ground::kLevelPx;
    const float min_x = std::min({top.x, right.x, bottom.x, left.x});
    const float max_x = std::max({top.x, right.x, bottom.x, left.x});
    const float min_y = std::min({top.y, right.y, bottom.y, left.y}) - kHeadroom;
    const float max_y = std::max({top.y, right.y, bottom.y, left.y});

    const int tex_w = static_cast<int>(std::ceil(max_x - min_x + Core::IsometricMath::TILE_WIDTH));
    const int tex_h = static_cast<int>(std::ceil(max_y - min_y + Core::IsometricMath::TILE_HEIGHT));

    chunk.screen_bounds = {min_x, min_y, static_cast<float>(tex_w), static_cast<float>(tex_h)};
    const int shrink = half ? 2 : 1;
    chunk.texture = SDL_CreateTexture(sdl_r, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET,
                                      (tex_w + shrink - 1) / shrink, (tex_h + shrink - 1) / shrink);
    if (!chunk.texture) return chunk;
    SDL_SetTextureBlendMode(chunk.texture, SDL_BLENDMODE_BLEND);

    // A half-size chunk is baked whole at full size into a scratch, then
    // shrunk into its own texture, so it is as seamless as a full one.
    SDL_Texture* bake = chunk.texture;
    if (half) {
        static SDL_Texture* full = nullptr;
        static int full_w = 0, full_h = 0;
        if (full == nullptr || full_w != tex_w || full_h != tex_h) {
            if (full != nullptr) SDL_DestroyTexture(full);
            full = SDL_CreateTexture(sdl_r, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, tex_w, tex_h);
            full_w = tex_w;
            full_h = tex_h;
        }
        if (full == nullptr) {
            SDL_DestroyTexture(chunk.texture);
            chunk.texture = nullptr;
            return chunk;
        }
        bake = full;
    }

    // Baked at the scale of its own pixels, whatever the view's zoom.
    float zoom = 1.0f, unused = 1.0f;
    SDL_RenderGetScale(sdl_r, &zoom, &unused);
    SDL_Texture* prev_target = SDL_GetRenderTarget(sdl_r);
    SDL_SetRenderTarget(sdl_r, bake);
    SetZoom(sdl_r, 1.0f);
    SDL_SetRenderDrawColor(sdl_r, 0, 0, 0, 0);
    SDL_RenderClear(sdl_r);

    const int start_x = cx * CS;
    const int start_y = cy * CS;
    const int end_x = std::min(start_x + CS, grid.GetWidth());
    const int end_y = std::min(start_y + CS, grid.GetHeight());

    // A scratch tile for drawing one tile flat before it is laid onto a
    // hillside.
    static SDL_Texture* tile_scratch = nullptr;
    const int tw = Core::IsometricMath::TILE_WIDTH + 1;
    const int th = Core::IsometricMath::TILE_HEIGHT + 1;
    if (!tile_scratch) {
        tile_scratch = SDL_CreateTexture(sdl_r, SDL_PIXELFORMAT_RGBA8888,
                                         SDL_TEXTUREACCESS_TARGET, tw, th);
        if (tile_scratch) SDL_SetTextureBlendMode(tile_scratch, SDL_BLENDMODE_BLEND);
    }

    for (int y = start_y; y < end_y; ++y) {
        for (int x = start_x; x < end_x; ++x) {
            const auto& tile = grid.GetTile(x, y);
            const Core::TerrainType terrain = TerrainAt(grid, x, y);
            auto screen_pos = Core::IsometricMath::WorldToScreen(Core::Fixed32(x), Core::Fixed32(y));
            const float ox = screen_pos.x - min_x;
            const float oy = screen_pos.y - min_y;
            const auto art = TerrainRenderer::GetTerrainTile(terrain, tile.texture_variant);

            // The heights of the tile's corners and middle, in levels.
            const float h_top = Ground::CornerLevel(grid, x, y);
            const float h_right = Ground::CornerLevel(grid, x + 1, y);
            const float h_bottom = Ground::CornerLevel(grid, x + 1, y + 1);
            const float h_left = Ground::CornerLevel(grid, x, y + 1);
            const float h_mid = static_cast<float>(tile.elevation);
            const bool flat = h_top == 0.0f && h_right == 0.0f && h_bottom == 0.0f &&
                              h_left == 0.0f && h_mid == 0.0f;

            if (flat || !tile_scratch) {
                SDL_FRect dest = {
                    std::round(ox - (Core::IsometricMath::TILE_WIDTH / 2.0f)),
                    std::round(oy),
                    static_cast<float>(tw), static_cast<float>(th)
                };
                // Every tile gets an opaque base, including water, so blend
                // overlays always paint onto something.
                if (art.atlas) {
                    SDL_SetTextureColorMod(art.atlas, 255, 255, 255);
                    SDL_SetTextureAlphaMod(art.atlas, 255);
                    SDL_RenderCopyF(sdl_r, art.atlas, &art.src, &dest);
                }
                RenderBlendOverlays(sdl_r, grid, x, y, dest, terrain, tile.texture_variant);
                continue;
            }

            // On a hill: the tile and its blends drawn flat, then laid onto the
            // ground's shape -- each corner raised to its height, and shaded
            // where the slope faces away from the light.
            SDL_Texture* chunk_target = SDL_GetRenderTarget(sdl_r);
            SDL_SetRenderTarget(sdl_r, tile_scratch);
            SDL_SetRenderDrawColor(sdl_r, 0, 0, 0, 0);
            SDL_RenderClear(sdl_r);
            const SDL_FRect flat_dest = {0.0f, 0.0f, static_cast<float>(tw),
                                         static_cast<float>(th)};
            if (art.atlas) {
                SDL_SetTextureColorMod(art.atlas, 255, 255, 255);
                SDL_SetTextureAlphaMod(art.atlas, 255);
                SDL_RenderCopyF(sdl_r, art.atlas, &art.src, &flat_dest);
            }
            RenderBlendOverlays(sdl_r, grid, x, y, flat_dest, tile.terrain,
                                tile.texture_variant);
            SDL_SetRenderTarget(sdl_r, chunk_target);

            const float half_w = Core::IsometricMath::TILE_WIDTH / 2.0f;
            const float half_h = Core::IsometricMath::TILE_HEIGHT / 2.0f;
            const float lift = Ground::kLevelPx;
            // Shade by darkening the tile where the slope faces away from the
            // light; the lit side is brightened in a second, added pass below.
            constexpr float kDeepestShade = 0.45f;
            constexpr float kBrightestLight = 70.0f;
            auto shade = [](float light) {
                const float dark = light < 0.0f ? 1.0f + light * kDeepestShade : 1.0f;
                const Uint8 v = static_cast<Uint8>(std::clamp(dark, 0.0f, 1.0f) * 255.0f);
                return SDL_Color{v, v, v, 255};
            };
            auto glow = [](float light) {
                const Uint8 v = static_cast<Uint8>(
                    std::clamp(light, 0.0f, 1.0f) * kBrightestLight);
                return SDL_Color{v, v, v, 255};
            };
            const float l_top = Ground::Light(grid, x, y);
            const float l_right = Ground::Light(grid, x + 1, y);
            const float l_bottom = Ground::Light(grid, x + 1, y + 1);
            const float l_left = Ground::Light(grid, x, y + 1);
            const float l_mid = 0.25f * (l_top + l_right + l_bottom + l_left);
            const float u_mid = half_w / static_cast<float>(tw);
            const float v_mid = half_h / static_cast<float>(th);
            // The outer corners pushed a pixel outward, so neighbouring tiles
            // overlap the way flat ones do and no seam of background shows
            // between them.
            constexpr float kBleed = 1.0f;
            const SDL_Vertex corners[5] = {
                {{ox, oy - h_top * lift - kBleed}, shade(l_top), {u_mid, 0.0f}},
                {{ox + half_w + kBleed * 2.0f, oy + half_h - h_right * lift}, shade(l_right),
                 {(2.0f * half_w) / static_cast<float>(tw), v_mid}},
                {{ox, oy + 2.0f * half_h - h_bottom * lift + kBleed}, shade(l_bottom),
                 {u_mid, (2.0f * half_h) / static_cast<float>(th)}},
                {{ox - half_w - kBleed * 2.0f, oy + half_h - h_left * lift}, shade(l_left),
                 {0.0f, v_mid}},
                {{ox, oy + half_h - h_mid * lift}, shade(l_mid), {u_mid, v_mid}},
            };
            static const int kFan[12] = {0, 1, 4, 1, 2, 4, 2, 3, 4, 3, 0, 4};
            SDL_RenderGeometry(sdl_r, tile_scratch, corners, 5, kFan, 12);
            if (l_top > 0.0f || l_right > 0.0f || l_bottom > 0.0f || l_left > 0.0f) {
                SDL_Vertex lit[5];
                const float lights[5] = {l_top, l_right, l_bottom, l_left, l_mid};
                for (int k = 0; k < 5; ++k) {
                    lit[k] = corners[k];
                    lit[k].color = glow(lights[k]);
                }
                SDL_SetRenderDrawBlendMode(sdl_r, SDL_BLENDMODE_ADD);
                SDL_RenderGeometry(sdl_r, nullptr, lit, 5, kFan, 12);
                SDL_SetRenderDrawBlendMode(sdl_r, SDL_BLENDMODE_BLEND);
            }
        }
    }

    if (half) {
        SDL_SetRenderTarget(sdl_r, chunk.texture);
        SetZoom(sdl_r, 1.0f);
        SDL_SetRenderDrawBlendMode(sdl_r, SDL_BLENDMODE_NONE);
        SDL_SetRenderDrawColor(sdl_r, 0, 0, 0, 0);
        SDL_RenderClear(sdl_r);
        SDL_SetTextureBlendMode(bake, SDL_BLENDMODE_NONE);
        SDL_RenderCopy(sdl_r, bake, nullptr, nullptr);
        SDL_SetTextureBlendMode(bake, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawBlendMode(sdl_r, SDL_BLENDMODE_BLEND);
    }
    SDL_SetRenderTarget(sdl_r, prev_target);
    SetZoom(sdl_r, zoom);
    return chunk;
}

void TerrainRenderer::DrawTerrain(ECS::Simulation& sim, Renderer& renderer, SDL_Renderer* sdl_r) {
    const auto& grid = sim.GetGrid();
    ++g_frame;
    g_registry = &sim.GetRegistry();
    g_viewer = Core::LocalPlayer();

    // Ground changed during play -- a farm planted, a foundation laid -- makes
    // the chunks it touches stale, along with their neighbours, whose blend
    // edges reach one tile over. Read from the grid's own log, never written
    // back: rendering stays downstream of the simulation.
    // The same for the ground players remember out of sight, which changes as
    // they look again (ecs/Fog).
    auto restale = [](const std::vector<Physics::TerrainChange>& log, uint32_t revision, uint32_t& seen) {
        if (revision == seen) return;
        const bool fell_behind = log.empty() || log.front().revision > seen + 1;
        if (fell_behind && seen != 0) {
            InvalidateChunks();
        } else {
            for (const auto& change : log) {
                if (change.revision <= seen) continue;
                for (int dy = -1; dy <= 1; ++dy) {
                    for (int dx = -1; dx <= 1; ++dx) {
                        const int tx = change.x + dx;
                        const int ty = change.y + dy;
                        if (tx < 0 || ty < 0) continue;
                        for (const bool half : {false, true}) {
                            auto stale = chunks.find(ChunkKey(tx / CHUNK_SIZE, ty / CHUNK_SIZE, half));
                            if (stale == chunks.end()) continue;
                            if (stale->second.texture) SDL_DestroyTexture(stale->second.texture);
                            chunks.erase(stale);
                        }
                    }
                }
            }
        }
        seen = revision;
    };
    static uint32_t seen_revision = 0;
    restale(grid.TerrainLog(), grid.TerrainRevision(), seen_revision);
    static uint32_t seen_remembered = 0;
    if (const ECS::Fog::Memory* memory = ECS::Fog::Read(sim.GetRegistry())) {
        restale(memory->log, memory->revision, seen_remembered);
    }

    const float inv_zoom = 1.0f / renderer.camera_zoom;
    const float view_w = renderer.view_w * inv_zoom;
    const float view_h = renderer.view_h * inv_zoom;

    // Screen rectangle the camera can see, with a margin so a chunk is baked
    // just before it scrolls in.
    const float margin = Core::IsometricMath::TILE_WIDTH * CHUNK_SIZE;
    const float vis_x0 = renderer.camera_x - margin;
    const float vis_y0 = renderer.camera_y - margin;
    const float vis_x1 = renderer.camera_x + view_w + margin;
    const float vis_y1 = renderer.camera_y + view_h + margin;

    const int chunks_x = (grid.GetWidth() + CHUNK_SIZE - 1) / CHUNK_SIZE;
    const int chunks_y = (grid.GetHeight() + CHUNK_SIZE - 1) / CHUNK_SIZE;

    // A chunk's screen bounds without building it, so visibility can be tested
    // before deciding to bake.
    auto chunk_bounds = [](int cx, int cy) {
        const int CS = CHUNK_SIZE;
        auto top    = Core::IsometricMath::WorldToScreen(Core::Fixed32(cx * CS),      Core::Fixed32(cy * CS));
        auto right  = Core::IsometricMath::WorldToScreen(Core::Fixed32(cx * CS + CS), Core::Fixed32(cy * CS));
        auto bottom = Core::IsometricMath::WorldToScreen(Core::Fixed32(cx * CS + CS), Core::Fixed32(cy * CS + CS));
        auto left   = Core::IsometricMath::WorldToScreen(Core::Fixed32(cx * CS),      Core::Fixed32(cy * CS + CS));
        const float x0 = std::min({top.x, right.x, bottom.x, left.x});
        const float y0 = std::min({top.y, right.y, bottom.y, left.y});
        const float x1 = std::max({top.x, right.x, bottom.x, left.x}) + Core::IsometricMath::TILE_WIDTH;
        const float y1 = std::max({top.y, right.y, bottom.y, left.y}) + Core::IsometricMath::TILE_HEIGHT;
        return SDL_FRect{x0, y0, x1 - x0, y1 - y0};
    };

    // Zoomed far out, the half-size bakes; a chunk not yet baked at the size
    // wanted is drawn at the other meanwhile, if it has one.
    const bool half = renderer.camera_zoom < kHalfZoom;
    int baked = 0;
    size_t in_view = 0;
    for (int cy = 0; cy < chunks_y; ++cy) {
        for (int cx = 0; cx < chunks_x; ++cx) {
            const SDL_FRect b = chunk_bounds(cx, cy);
            if (b.x + b.w < vis_x0 || b.x > vis_x1 || b.y + b.h < vis_y0 || b.y > vis_y1) {
                continue;
            }

            const uint64_t key = ChunkKey(cx, cy, half);
            auto it = chunks.find(key);
            if (it == chunks.end() && baked < kBakesPerFrame) {
                // Spread the cost: a few chunks per frame, nearest first by
                // virtue of the scan order, so scrolling never stalls.
                TerrainChunk chunk = BakeChunk(sdl_r, grid, cx, cy, half);
                if (chunk.texture) {
                    ++baked;
                    it = chunks.emplace(key, chunk).first;
                }
            }
            if (it == chunks.end()) it = chunks.find(ChunkKey(cx, cy, !half));
            if (it == chunks.end()) continue;
            it->second.last_used = g_frame;
            ++in_view;

            // Each tile's diamond is centred on its world position, the
            // convention every other part of the engine uses for a tile. The
            // chunk was baked with tile (x, y)'s image box starting at its
            // world point, so the whole chunk moves up half a tile to centre
            // it.
            const float sx = it->second.screen_bounds.x - renderer.camera_x;
            const float sy = it->second.screen_bounds.y - renderer.camera_y
                           - (Core::IsometricMath::TILE_HEIGHT / 2.0f);
            SDL_FRect dest = {sx, sy, it->second.screen_bounds.w, it->second.screen_bounds.h};
            SDL_RenderCopyF(sdl_r, it->second.texture, nullptr, &dest);
        }
    }

    // Evict the least recently seen chunks once the cache is over budget.
    const size_t budget = std::max(kMaxLiveChunks, in_view + in_view / 2);
    if (chunks.size() > budget) {
        static std::vector<std::pair<uint64_t, uint64_t>> aged; // last_used, key
        aged.clear();
        for (const auto& entry : chunks) {
            aged.emplace_back(entry.second.last_used, entry.first);
        }
        std::sort(aged.begin(), aged.end());
        const size_t excess = chunks.size() - budget;
        for (size_t i = 0; i < excess; ++i) {
            if (aged[i].first == g_frame) break;   // never evict what is on screen
            auto it = chunks.find(aged[i].second);
            if (it != chunks.end()) {
                if (it->second.texture) SDL_DestroyTexture(it->second.texture);
                chunks.erase(it);
            }
        }
    }
}

} // namespace Rendering
