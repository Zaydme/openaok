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
bool g_masks_tried = false;

// How many mask sets the sheet holds: one 49-pixel row each.
int g_mask_sets = 1;
int MaskSets() { return g_mask_sets; }

SDL_Texture* BlendMasks() {
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
    }
    return g_masks;
}

// A chunk's blends are each drawn through their mask into a slot of the blend
// sheet, and its tiles on hills drawn flat into slots of the hill sheet, all
// before the chunk itself is drawn: the renderer changes target a few times
// a chunk, not twice for every blend. A tile has at most eight blends, one
// for each neighbour.
constexpr int kBlendsAcross = 20;
constexpr int kBlendSlots = TerrainRenderer::CHUNK_SIZE * TerrainRenderer::CHUNK_SIZE * 8;
constexpr int kHillsAcross = TerrainRenderer::CHUNK_SIZE;
constexpr int kHillW = Core::IsometricMath::TILE_WIDTH + 1;
constexpr int kHillH = Core::IsometricMath::TILE_HEIGHT + 1;
// Slots are kept apart, so a hill tile's sampling never reaches the next.
constexpr int kHillGap = 2;

SDL_Texture* g_blend_sheet = nullptr;
SDL_Texture* g_hill_sheet = nullptr;

SDL_Texture* Sheet(SDL_Renderer* sdl_r, SDL_Texture*& sheet, int w, int h) {
    if (sheet == nullptr) {
        sheet = SDL_CreateTexture(sdl_r, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, w, h);
        if (sheet) SDL_SetTextureBlendMode(sheet, SDL_BLENDMODE_BLEND);
    }
    return sheet;
}

SDL_Texture* BlendSheet(SDL_Renderer* sdl_r) {
    const int rows = (kBlendSlots + kBlendsAcross - 1) / kBlendsAcross;
    return Sheet(sdl_r, g_blend_sheet, kBlendsAcross * (kMaskW + 1), rows * (kMaskH + 1));
}

SDL_Texture* HillSheet(SDL_Renderer* sdl_r) {
    const int rows = TerrainRenderer::CHUNK_SIZE * TerrainRenderer::CHUNK_SIZE / kHillsAcross;
    return Sheet(sdl_r, g_hill_sheet, kHillsAcross * (kHillW + kHillGap), rows * (kHillH + kHillGap));
}

SDL_Rect BlendSlot(size_t i) {
    const int n = static_cast<int>(i);
    return {(n % kBlendsAcross) * (kMaskW + 1), (n / kBlendsAcross) * (kMaskH + 1), kMaskW, kMaskH};
}

SDL_Rect HillSlot(int i) {
    return {(i % kHillsAcross) * (kHillW + kHillGap), (i / kHillsAcross) * (kHillH + kHillGap), kHillW, kHillH};
}

// One blend: a terrain tile drawn through one of the masks.
struct Blend {
    TerrainRenderer::TerrainTile tile;
    int mask = 0;
    int mode = 0;
};

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

// Draws each blend through its mask into its slot of the blend sheet, by
// multiplying the tile's alpha by the mask's.
void MaskBlends(SDL_Renderer* sdl_r, SDL_Texture* masks, const std::vector<Blend>& blends) {
    static const SDL_BlendMode multiply_alpha = SDL_ComposeCustomBlendMode(
        SDL_BLENDFACTOR_ZERO, SDL_BLENDFACTOR_ONE, SDL_BLENDOPERATION_ADD,
        SDL_BLENDFACTOR_ZERO, SDL_BLENDFACTOR_SRC_ALPHA, SDL_BLENDOPERATION_ADD);
    for (size_t i = 0; i < blends.size() && i < static_cast<size_t>(kBlendSlots); ++i) {
        const Blend& b = blends[i];
        const SDL_Rect slot = BlendSlot(i);
        SDL_SetTextureBlendMode(b.tile.atlas, SDL_BLENDMODE_NONE);
        SDL_RenderCopy(sdl_r, b.tile.atlas, &b.tile.src, &slot);
        const SDL_Rect msrc = {b.mask * kMaskW, b.mode * kMaskH, kMaskW, kMaskH};
        SDL_SetTextureBlendMode(masks, multiply_alpha);
        SDL_RenderCopy(sdl_r, masks, &msrc, &slot);
        SDL_SetTextureBlendMode(masks, SDL_BLENDMODE_BLEND);
        SDL_SetTextureBlendMode(b.tile.atlas, SDL_BLENDMODE_BLEND);
    }
}

// Draws blends [first, first + count) of the sheet over a tile at `dest`.
void DrawBlends(SDL_Renderer* sdl_r, size_t first, size_t count, const SDL_FRect& dest) {
    for (size_t i = first; i < first + count && i < static_cast<size_t>(kBlendSlots); ++i) {
        const SDL_Rect slot = BlendSlot(i);
        SDL_RenderCopyF(sdl_r, g_blend_sheet, &slot, &dest);
    }
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

// The blends drawn over a tile, in order: each higher-priority terrain
// touching it, through the masks its neighbours' pattern picks.
static void CollectBlends(
    const Physics::Grid& grid,
    int x, int y,
    Core::TerrainType my_terrain,
    uint8_t my_variant,
    std::vector<Blend>& out)
{
    const int my_prio = GetTerrainPriority(my_terrain);

    // Collect the distinct higher-priority terrains touching this tile, and
    // which of the eight neighbours each one occupies.
    std::pair<Core::TerrainType, uint8_t> influences[8];
    int influence_count = 0;
    for (int n = 0; n < 8; ++n) {
        const int nx = x + kNeighbourDx[n];
        const int ny = y + kNeighbourDy[n];
        if (!grid.InBounds(nx, ny)) continue;
        const Core::TerrainType nt = TerrainAt(grid, nx, ny);
        if (GetTerrainPriority(nt) <= my_prio) continue;
        auto* end = influences + influence_count;
        auto* it = std::find_if(influences, end, [nt](const auto& p) { return p.first == nt; });
        if (it == end) influences[influence_count++] = {nt, uint8_t(1u << n)};
        else it->second |= uint8_t(1u << n);
    }

    std::sort(influences, influences + influence_count,
              [](const auto& a, const auto& b) {
                  return GetTerrainPriority(a.first) < GetTerrainPriority(b.first);
              });

    // One of four interchangeable variants, chosen from the tile position so a
    // field of the same transition does not repeat one shape.
    const int variant = ((x * 7 + y * 13) & 3);

    for (int k = 0; k < influence_count; ++k) {
        const auto& inf = influences[k];
        const auto tile = TerrainRenderer::GetTerrainTile(inf.first, my_variant);
        if (!tile.atlas) continue;

        const uint8_t bits = inf.second;
        const uint8_t adjacent = bits & 0b10101010u;
        const int mode = GetBlendMode(my_terrain, inf.first, MaskSets());

        const int adj_mask = AdjacentMask(adjacent, variant);
        if (adj_mask >= 0) out.push_back({tile, adj_mask, mode});

        // Corners, skipped where an edge beside them already blends.
        static const int kCornerBit[4]  = {2, 4, 0, 6};
        static const int kCornerMask[4] = {16, 17, 18, 19};
        for (int c = 0; c < 4; ++c) {
            const int bit = kCornerBit[c];
            if (!(bits & (1u << bit))) continue;
            const uint8_t left = uint8_t(1u << ((bit + 7) % 8));
            const uint8_t right = uint8_t(1u << ((bit + 1) % 8));
            if (adjacent & (left | right)) continue;
            out.push_back({tile, kCornerMask[c], mode});
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

// Chunks are baked as the viewport first touches them, within a few
// milliseconds a frame so scrolling never hitches, and chunks out of view are
// evicted once the budget is exceeded: kMaxLiveChunks, or half as many again
// as are in view when that is more.
constexpr uint64_t kBakeBudgetUs = 4000;
// The view counts as jumped when more than one in this many of the chunks in
// sight have nothing baked.
constexpr int kJumpedShare = 4;
constexpr size_t kMaxLiveChunks = 64;
// Zoomed out further than this, chunks are baked at half their size: a
// quarter of the memory and the drawing, at no cost to what shows.
constexpr float kHalfZoom = 0.6f;
// Room above a chunk for hills: a tile on high ground is drawn higher up the
// screen than its flat position.
constexpr float kHeadroomPx = 8.0f * Ground::kLevelPx;

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

// A chunk's box in the world's screen space: its tiles' images, with room
// above for hills.
struct ChunkBox {
    float x, y;
    int w, h;
};

static ChunkBox BoxOf(int cx, int cy) {
    const int CS = TerrainRenderer::CHUNK_SIZE;
    auto top    = Core::IsometricMath::WorldToScreen(Core::Fixed32(cx * CS),      Core::Fixed32(cy * CS));
    auto right  = Core::IsometricMath::WorldToScreen(Core::Fixed32(cx * CS + CS), Core::Fixed32(cy * CS));
    auto bottom = Core::IsometricMath::WorldToScreen(Core::Fixed32(cx * CS + CS), Core::Fixed32(cy * CS + CS));
    auto left   = Core::IsometricMath::WorldToScreen(Core::Fixed32(cx * CS),      Core::Fixed32(cy * CS + CS));

    const float min_x = std::min({top.x, right.x, bottom.x, left.x});
    const float max_x = std::max({top.x, right.x, bottom.x, left.x});
    const float min_y = std::min({top.y, right.y, bottom.y, left.y}) - kHeadroomPx;
    const float max_y = std::max({top.y, right.y, bottom.y, left.y});
    return {min_x, min_y,
            static_cast<int>(std::ceil(max_x - min_x + Core::IsometricMath::TILE_WIDTH)),
            static_cast<int>(std::ceil(max_y - min_y + Core::IsometricMath::TILE_HEIGHT))};
}

// A scratch a chunk is drawn into whole at full size, to be shrunk.
static SDL_Texture* FullScratch(SDL_Renderer* sdl_r, int w, int h) {
    static SDL_Texture* full = nullptr;
    static int full_w = 0, full_h = 0;
    if (full == nullptr || full_w != w || full_h != h) {
        if (full != nullptr) SDL_DestroyTexture(full);
        full = SDL_CreateTexture(sdl_r, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, w, h);
        if (full != nullptr) SDL_SetTextureBlendMode(full, SDL_BLENDMODE_BLEND);
        full_w = w;
        full_h = h;
    }
    return full;
}

// The renderer's target, scale and drawing blend mode, put back after a
// bake: a chunk is baked at the scale of its own pixels, whatever the view's
// zoom.
struct RenderState {
    SDL_Texture* target;
    float zoom;
    SDL_BlendMode mode;
};

static RenderState Save(SDL_Renderer* sdl_r) {
    RenderState state{SDL_GetRenderTarget(sdl_r), 1.0f, SDL_BLENDMODE_BLEND};
    float unused = 1.0f;
    SDL_RenderGetScale(sdl_r, &state.zoom, &unused);
    SDL_GetRenderDrawBlendMode(sdl_r, &state.mode);
    return state;
}

static void Restore(SDL_Renderer* sdl_r, const RenderState& state) {
    SDL_SetRenderTarget(sdl_r, state.target);
    SetZoom(sdl_r, state.zoom);
    SDL_SetRenderDrawBlendMode(sdl_r, state.mode);
}

// Draws one chunk's tiles and blend overlays at full size into `target`, the
// box's top left at the target's, and leaves the target set.
static void Compose(SDL_Renderer* sdl_r, const Physics::Grid& grid, int cx, int cy,
                    const ChunkBox& box, SDL_Texture* target) {
    const int CS = TerrainRenderer::CHUNK_SIZE;
    const float min_x = box.x;
    const float min_y = box.y;
    const int start_x = cx * CS;
    const int start_y = cy * CS;
    const int end_x = std::min(start_x + CS, grid.GetWidth());
    const int end_y = std::min(start_y + CS, grid.GetHeight());
    const int tw = Core::IsometricMath::TILE_WIDTH + 1;
    const int th = Core::IsometricMath::TILE_HEIGHT + 1;

    // What each tile lays: its ground, its blends (a run of `blends`), and on
    // a hill, its slot in the hill sheet.
    struct Laid {
        int x, y;
        TerrainRenderer::TerrainTile art;
        float ox, oy;
        bool flat;
        size_t first_blend, blend_count;
        SDL_Rect hill;
    };
    static std::vector<Laid> laid;
    static std::vector<Blend> blends;
    laid.clear();
    blends.clear();
    SDL_Texture* masks = BlendMasks();
    SDL_Texture* blend_sheet = masks != nullptr ? BlendSheet(sdl_r) : nullptr;
    SDL_Texture* hill_sheet = HillSheet(sdl_r);
    int hills = 0;
    for (int y = start_y; y < end_y; ++y) {
        for (int x = start_x; x < end_x; ++x) {
            const auto& tile = grid.GetTile(x, y);
            const Core::TerrainType terrain = TerrainAt(grid, x, y);
            auto screen_pos = Core::IsometricMath::WorldToScreen(Core::Fixed32(x), Core::Fixed32(y));
            Laid l{x, y, TerrainRenderer::GetTerrainTile(terrain, tile.texture_variant),
                   screen_pos.x - min_x, screen_pos.y - min_y, true, blends.size(), 0, {}};
            l.flat = hill_sheet == nullptr ||
                     (Ground::CornerLevel(grid, x, y) == 0.0f && Ground::CornerLevel(grid, x + 1, y) == 0.0f &&
                      Ground::CornerLevel(grid, x + 1, y + 1) == 0.0f && Ground::CornerLevel(grid, x, y + 1) == 0.0f &&
                      tile.elevation == 0);
            if (blend_sheet != nullptr) {
                CollectBlends(grid, x, y, l.flat ? terrain : tile.terrain, tile.texture_variant, blends);
            }
            l.blend_count = blends.size() - l.first_blend;
            if (!l.flat) l.hill = HillSlot(hills++);
            laid.push_back(l);
        }
    }

    if (!blends.empty()) {
        SDL_SetRenderTarget(sdl_r, blend_sheet);
        SetZoom(sdl_r, 1.0f);
        MaskBlends(sdl_r, masks, blends);
    }
    // A hill tile and its blends drawn flat, to be laid onto the ground's
    // shape.
    if (hills > 0) {
        SDL_SetRenderTarget(sdl_r, hill_sheet);
        SetZoom(sdl_r, 1.0f);
        SDL_SetRenderDrawColor(sdl_r, 0, 0, 0, 0);
        SDL_RenderClear(sdl_r);
        for (const Laid& l : laid) {
            if (l.flat) continue;
            const SDL_FRect at = {static_cast<float>(l.hill.x), static_cast<float>(l.hill.y),
                                  static_cast<float>(tw), static_cast<float>(th)};
            if (l.art.atlas) {
                SDL_SetTextureColorMod(l.art.atlas, 255, 255, 255);
                SDL_SetTextureAlphaMod(l.art.atlas, 255);
                SDL_RenderCopyF(sdl_r, l.art.atlas, &l.art.src, &at);
            }
            DrawBlends(sdl_r, l.first_blend, l.blend_count, at);
        }
    }

    SDL_SetRenderTarget(sdl_r, target);
    SetZoom(sdl_r, 1.0f);
    SDL_SetRenderDrawColor(sdl_r, 0, 0, 0, 0);
    SDL_RenderClear(sdl_r);

    int hill_w = 0, hill_h = 0;
    if (hill_sheet != nullptr) SDL_QueryTexture(hill_sheet, nullptr, nullptr, &hill_w, &hill_h);
    for (const Laid& l : laid) {
        const int x = l.x;
        const int y = l.y;
        const float ox = l.ox;
        const float oy = l.oy;
        if (l.flat) {
            SDL_FRect dest = {
                std::round(ox - (Core::IsometricMath::TILE_WIDTH / 2.0f)),
                std::round(oy),
                static_cast<float>(tw), static_cast<float>(th)
            };
            // Every tile gets an opaque base, including water, so blend
            // overlays always paint onto something.
            if (l.art.atlas) {
                SDL_SetTextureColorMod(l.art.atlas, 255, 255, 255);
                SDL_SetTextureAlphaMod(l.art.atlas, 255);
                SDL_RenderCopyF(sdl_r, l.art.atlas, &l.art.src, &dest);
            }
            DrawBlends(sdl_r, l.first_blend, l.blend_count, dest);
            continue;
        }

        // On a hill: the tile drawn flat in its slot, laid onto the ground's
        // shape -- each corner raised to its height, and shaded where the
        // slope faces away from the light.
        const float h_top = Ground::CornerLevel(grid, x, y);
        const float h_right = Ground::CornerLevel(grid, x + 1, y);
        const float h_bottom = Ground::CornerLevel(grid, x + 1, y + 1);
        const float h_left = Ground::CornerLevel(grid, x, y + 1);
        const float h_mid = static_cast<float>(grid.GetTile(x, y).elevation);
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
        // The slot's place in the sheet, as texture coordinates.
        auto u = [&](float px) { return (static_cast<float>(l.hill.x) + px) / static_cast<float>(hill_w); };
        auto v = [&](float px) { return (static_cast<float>(l.hill.y) + px) / static_cast<float>(hill_h); };
        // The outer corners pushed a pixel outward, so neighbouring tiles
        // overlap the way flat ones do and no seam of background shows
        // between them.
        constexpr float kBleed = 1.0f;
        const SDL_Vertex corners[5] = {
            {{ox, oy - h_top * lift - kBleed}, shade(l_top), {u(half_w), v(0.0f)}},
            {{ox + half_w + kBleed * 2.0f, oy + half_h - h_right * lift}, shade(l_right),
             {u(2.0f * half_w), v(half_h)}},
            {{ox, oy + 2.0f * half_h - h_bottom * lift + kBleed}, shade(l_bottom),
             {u(half_w), v(2.0f * half_h)}},
            {{ox - half_w - kBleed * 2.0f, oy + half_h - h_left * lift}, shade(l_left),
             {u(0.0f), v(half_h)}},
            {{ox, oy + half_h - h_mid * lift}, shade(l_mid), {u(half_w), v(half_h)}},
        };
        static const int kFan[12] = {0, 1, 4, 1, 2, 4, 2, 3, 4, 3, 0, 4};
        SDL_RenderGeometry(sdl_r, hill_sheet, corners, 5, kFan, 12);
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

// Bakes one chunk's tiles and blend overlays into its own texture.
static TerrainChunk BakeChunk(SDL_Renderer* sdl_r, const Physics::Grid& grid,
                              int cx, int cy, bool half) {
    TerrainChunk chunk;
    chunk.grid_x = cx;
    chunk.grid_y = cy;
    const ChunkBox box = BoxOf(cx, cy);
    chunk.screen_bounds = {box.x, box.y, static_cast<float>(box.w), static_cast<float>(box.h)};
    const int shrink = half ? 2 : 1;
    chunk.texture = SDL_CreateTexture(sdl_r, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET,
                                      (box.w + shrink - 1) / shrink, (box.h + shrink - 1) / shrink);
    if (!chunk.texture) return chunk;
    SDL_SetTextureBlendMode(chunk.texture, SDL_BLENDMODE_BLEND);

    // A half-size chunk is baked whole at full size into a scratch, then
    // shrunk into its own texture, so it is as seamless as a full one.
    SDL_Texture* bake = half ? FullScratch(sdl_r, box.w, box.h) : chunk.texture;
    if (bake == nullptr) {
        SDL_DestroyTexture(chunk.texture);
        chunk.texture = nullptr;
        return chunk;
    }
    const RenderState saved = Save(sdl_r);
    Compose(sdl_r, grid, cx, cy, box, bake);
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
    Restore(sdl_r, saved);
    return chunk;
}

// The whole map drawn once at a fraction of its size, under the chunks: what
// shows where a chunk is not baked yet, as when the view jumps across the map
// by the minimap. It is filled a chunk at a time with what is left of a
// frame's budget, and a chunk of it drawn again where the ground changes.
namespace {

constexpr int kOverviewShrink = 8;

struct Overview {
    SDL_Texture* texture = nullptr;
    const Physics::Grid* grid = nullptr;
    int shrink = kOverviewShrink;
    float x = 0.0f, y = 0.0f;    // its top left, in the world's screen space
    int w = 0, h = 0;            // its size, in its own pixels
    int across = 0, down = 0;    // in chunks
    std::vector<uint8_t> drawn;  // by chunk: drawn into it as the ground is now
    size_t next = 0;             // where filling it goes on from
};
Overview g_overview;

// Ready for `grid`: made, the size its chunks need, when it is not.
bool ReadyOverview(SDL_Renderer* sdl_r, const Physics::Grid& grid) {
    const int CS = TerrainRenderer::CHUNK_SIZE;
    const int across = (grid.GetWidth() + CS - 1) / CS;
    const int down = (grid.GetHeight() + CS - 1) / CS;
    Overview& o = g_overview;
    if (o.texture != nullptr && o.grid == &grid && o.across == across && o.down == down) return true;
    if (o.texture != nullptr) SDL_DestroyTexture(o.texture);
    o = Overview{};
    // The chunks' boxes together: from the left corner's to the right's, the
    // top corner's (with its headroom) to the bottom's.
    const ChunkBox top = BoxOf(0, 0);
    const ChunkBox left = BoxOf(0, down - 1);
    const ChunkBox right = BoxOf(across - 1, 0);
    const ChunkBox bottom = BoxOf(across - 1, down - 1);
    const float wide = right.x + static_cast<float>(right.w) - left.x;
    const float high = bottom.y + static_cast<float>(bottom.h) - top.y;
    SDL_RendererInfo info{};
    SDL_GetRendererInfo(sdl_r, &info);
    const int most_w = info.max_texture_width > 0 ? info.max_texture_width : 4096;
    const int most_h = info.max_texture_height > 0 ? info.max_texture_height : 4096;
    while (std::ceil(wide / static_cast<float>(o.shrink)) > static_cast<float>(most_w) ||
           std::ceil(high / static_cast<float>(o.shrink)) > static_cast<float>(most_h)) {
        o.shrink *= 2;
    }
    o.w = static_cast<int>(std::ceil(wide / static_cast<float>(o.shrink)));
    o.h = static_cast<int>(std::ceil(high / static_cast<float>(o.shrink)));
    o.texture = SDL_CreateTexture(sdl_r, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, o.w, o.h);
    if (o.texture == nullptr) return false;
    SDL_SetTextureBlendMode(o.texture, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(o.texture, SDL_ScaleModeLinear);
    const RenderState saved = Save(sdl_r);
    SDL_SetRenderTarget(sdl_r, o.texture);
    SDL_SetRenderDrawColor(sdl_r, 0, 0, 0, 0);
    SDL_RenderClear(sdl_r);
    Restore(sdl_r, saved);
    o.grid = &grid;
    o.x = left.x;
    o.y = top.y;
    o.across = across;
    o.down = down;
    o.drawn.assign(static_cast<size_t>(across) * static_cast<size_t>(down), 0);
    return true;
}

// Draws one chunk into the overview, shrunk.
void DrawIntoOverview(SDL_Renderer* sdl_r, const Physics::Grid& grid, int cx, int cy) {
    Overview& o = g_overview;
    const ChunkBox box = BoxOf(cx, cy);
    SDL_Texture* bake = FullScratch(sdl_r, box.w, box.h);
    if (bake == nullptr) return;
    const RenderState saved = Save(sdl_r);
    Compose(sdl_r, grid, cx, cy, box, bake);
    SDL_SetRenderTarget(sdl_r, o.texture);
    SetZoom(sdl_r, 1.0f);
    const float s = static_cast<float>(o.shrink);
    const SDL_FRect dest = {(box.x - o.x) / s, (box.y - o.y) / s,
                            static_cast<float>(box.w) / s, static_cast<float>(box.h) / s};
    SDL_ScaleMode was = SDL_ScaleModeNearest;
    SDL_GetTextureScaleMode(bake, &was);
    SDL_SetTextureScaleMode(bake, SDL_ScaleModeLinear);
    SDL_RenderCopyF(sdl_r, bake, nullptr, &dest);
    SDL_SetTextureScaleMode(bake, was);
    Restore(sdl_r, saved);
}

// The chunk at (cx, cy) to be drawn into the overview again.
void Unseen(int cx, int cy) {
    Overview& o = g_overview;
    if (cx < 0 || cy < 0 || cx >= o.across || cy >= o.down) return;
    o.drawn[static_cast<size_t>(cy) * static_cast<size_t>(o.across) + static_cast<size_t>(cx)] = 0;
}

} // namespace

void TerrainRenderer::InvalidateChunks() {
    for (auto& entry : chunks) {
        if (entry.second.texture) SDL_DestroyTexture(entry.second.texture);
    }
    chunks.clear();
    if (g_overview.texture != nullptr) SDL_DestroyTexture(g_overview.texture);
    g_overview = Overview{};
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
                        Unseen(tx / CHUNK_SIZE, ty / CHUNK_SIZE);
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
    // What is in sight: the view, and the hills a chunk below it raises into
    // it.
    const float sight = kHeadroomPx + Core::IsometricMath::TILE_HEIGHT;
    const float see_x0 = renderer.camera_x - sight;
    const float see_y0 = renderer.camera_y - sight;
    const float see_x1 = renderer.camera_x + view_w + sight;
    const float see_y1 = renderer.camera_y + view_h + sight;
    // The chunks in the view and its margin, and how many of those in sight
    // have nothing baked.
    struct Spot {
        int cx, cy;
        bool seen;
    };
    static std::vector<Spot> spots;
    spots.clear();
    int in_sight = 0, missing = 0;
    for (int cy = 0; cy < chunks_y; ++cy) {
        for (int cx = 0; cx < chunks_x; ++cx) {
            const SDL_FRect b = chunk_bounds(cx, cy);
            if (b.x + b.w < vis_x0 || b.x > vis_x1 || b.y + b.h < vis_y0 || b.y > vis_y1) {
                continue;
            }
            const bool seen = !(b.x + b.w < see_x0 || b.x > see_x1 || b.y + b.h < see_y0 || b.y > see_y1);
            spots.push_back({cx, cy, seen});
            if (!seen) continue;
            ++in_sight;
            if (chunks.count(ChunkKey(cx, cy, half)) == 0 && chunks.count(ChunkKey(cx, cy, !half)) == 0) ++missing;
        }
    }

    // Chunks are baked within a time budget a frame: those in sight as they
    // are met, then those in the margin, to be ready as they scroll in. The
    // view jumped, with much of it missing, the budget is doubled.
    const uint64_t bake_budget = SDL_GetPerformanceFrequency() * kBakeBudgetUs / 1000000 *
                                 (missing * kJumpedShare > in_sight ? 2 : 1);
    uint64_t spent = 0;
    auto bake = [&](int cx, int cy) {
        const uint64_t start = SDL_GetPerformanceCounter();
        TerrainChunk chunk = BakeChunk(sdl_r, grid, cx, cy, half);
        spent += SDL_GetPerformanceCounter() - start;
        if (!chunk.texture) return chunks.end();
        chunk.last_used = g_frame;
        return chunks.emplace(ChunkKey(cx, cy, half), chunk).first;
    };

    // What is missing shows the overview meanwhile, drawn under the chunks.
    const bool overview = ReadyOverview(sdl_r, grid);
    const float centre_up = Core::IsometricMath::TILE_HEIGHT / 2.0f;
    if (overview && missing > 0) {
        const float s = static_cast<float>(g_overview.shrink);
        const SDL_FRect dest = {g_overview.x - renderer.camera_x, g_overview.y - renderer.camera_y - centre_up,
                                static_cast<float>(g_overview.w) * s, static_cast<float>(g_overview.h) * s};
        SDL_RenderCopyF(sdl_r, g_overview.texture, nullptr, &dest);
    }

    static std::vector<std::pair<int, int>> ahead;
    ahead.clear();
    size_t in_view = 0;
    for (const Spot& spot : spots) {
        auto it = chunks.find(ChunkKey(spot.cx, spot.cy, half));
        if (it == chunks.end()) {
            if (!spot.seen) {
                ahead.emplace_back(spot.cx, spot.cy);
            } else if (spent < bake_budget) {
                it = bake(spot.cx, spot.cy);
            }
        }
        if (it == chunks.end()) it = chunks.find(ChunkKey(spot.cx, spot.cy, !half));
        if (it == chunks.end()) continue;
        it->second.last_used = g_frame;
        ++in_view;
        if (!spot.seen) continue;

        // Each tile's diamond is centred on its world position, the
        // convention every other part of the engine uses for a tile. The
        // chunk was baked with tile (x, y)'s image box starting at its world
        // point, so the whole chunk moves up half a tile to centre it.
        const float sx = it->second.screen_bounds.x - renderer.camera_x;
        const float sy = it->second.screen_bounds.y - renderer.camera_y - centre_up;
        SDL_FRect dest = {sx, sy, it->second.screen_bounds.w, it->second.screen_bounds.h};
        SDL_RenderCopyF(sdl_r, it->second.texture, nullptr, &dest);
    }
    for (const auto& [cx, cy] : ahead) {
        if (spent >= bake_budget) break;
        if (bake(cx, cy) != chunks.end()) ++in_view;
    }
    // What is left of the budget fills in the overview.
    if (overview) {
        Overview& o = g_overview;
        const size_t count = o.drawn.size();
        for (size_t looked = 0; looked < count && spent < bake_budget; ++looked) {
            const size_t i = o.next;
            o.next = (o.next + 1) % count;
            if (o.drawn[i]) continue;
            const uint64_t start = SDL_GetPerformanceCounter();
            DrawIntoOverview(sdl_r, grid, static_cast<int>(i % static_cast<size_t>(o.across)),
                             static_cast<int>(i / static_cast<size_t>(o.across)));
            spent += SDL_GetPerformanceCounter() - start;
            o.drawn[i] = 1;
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
