#include "FogRenderer.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <vector>

#include "Ground.h"
#include "Renderer.h"
#include "core/IsometricMath.h"
#include "ecs/Vision.h"
#include "hal/EdgeMasks.h"

namespace Rendering {

namespace {

constexpr int kMaskW = 97;
constexpr int kMaskH = 49;
// Each mask sits in a cell with a transparent pixel round it, so a scaled
// draw never picks up its neighbour's edge.
constexpr int kCellW = kMaskW + 2;
constexpr int kCellH = kMaskH + 2;
constexpr int kAtlasColumns = 12;
constexpr int kPatterns = 47;
// Cells 0..46: the dithered part of a seen tile, TileEdge[2k+1] -- except
// cell 0, which holds TileEdge[0], the whole tile (TileEdge[1] is empty, so
// pattern 0 is never drawn dithered). Cells 47..93: BlkEdge[k].
constexpr int kWholeTile = 0;
constexpr int kBlackBase = kPatterns;
constexpr int kCells = 2 * kPatterns;
constexpr int kAtlasW = kAtlasColumns * kCellW;
constexpr int kAtlasH = ((kCells + kAtlasColumns - 1) / kAtlasColumns) * kCellH;

// The neighbour bits, and where each neighbour is in the world: screen x
// runs with x - y, screen y with x + y.
constexpr int kLeftCorner = 0x01;     // (x-1, y+1)
constexpr int kBottomCorner = 0x02;   // (x+1, y+1)
constexpr int kRightCorner = 0x04;    // (x+1, y-1)
constexpr int kTopCorner = 0x08;      // (x-1, y-1)
constexpr int kLowerLeft = 0x10;      // (x,   y+1)
constexpr int kLowerRight = 0x20;     // (x+1, y)
constexpr int kUpperRight = 0x40;     // (x,   y-1)
constexpr int kUpperLeft = 0x80;      // (x-1, y)

enum : uint8_t { kUnexplored = 0, kFogged = 1, kSeen = 2, kOffMap = 3 };

struct State {
    bool tried = false;
    bool masks_ok = false;
    SDL_Texture* atlas = nullptr;
    SDL_Texture* checker = nullptr;
    SDL_Texture* layer = nullptr;
    int layer_w = 0;
    int layer_h = 0;
    uint8_t pattern[256] = {};
    // Reused every frame: no allocation once warm.
    std::vector<uint8_t> tiles;
    std::vector<SDL_Vertex> dither_vertices;
    std::vector<int> dither_indices;
    std::vector<SDL_Vertex> black_vertices;
    std::vector<int> black_indices;
};

State g;

// The 256 neighbour patterns folded onto the 47 masks: a pattern whose
// corner sits beside a set edge is not a shape of its own, the edge already
// covering that corner; the others are numbered in order.
void BuildPatternTable(uint8_t table[256]) {
    std::fill(table, table + 256, uint8_t{0xff});
    int next = 0;
    for (int c = 0; c < 256; ++c) {
        const bool redundant =
            ((c & kUpperLeft) && (c & (kLeftCorner | kTopCorner))) ||
            ((c & kUpperRight) && (c & (kRightCorner | kTopCorner))) ||
            ((c & kLowerRight) && (c & (kBottomCorner | kRightCorner))) ||
            ((c & kLowerLeft) && (c & (kLeftCorner | kBottomCorner)));
        if (!redundant) table[c] = static_cast<uint8_t>(next++);
    }
    for (int c = 0; c < 256; ++c) {
        if (table[c] != 0xff) continue;
        int corners = c & 0x0f;
        if (c & kUpperLeft) corners &= ~(kLeftCorner | kTopCorner);
        if (c & kUpperRight) corners &= ~(kRightCorner | kTopCorner);
        if (c & kLowerRight) corners &= ~(kBottomCorner | kRightCorner);
        if (c & kLowerLeft) corners &= ~(kLeftCorner | kBottomCorner);
        table[c] = table[(c & 0xf0) | corners];
    }
}

void PaintMask(std::vector<uint32_t>& pixels, int cell, const std::vector<HAL::EdgeSpan>& spans) {
    const int ox = (cell % kAtlasColumns) * kCellW + 1;
    const int oy = (cell / kAtlasColumns) * kCellH + 1;
    for (const HAL::EdgeSpan& s : spans) {
        if (s.y >= kMaskH) continue;
        for (int x = s.x0; x <= s.x1 && x < kMaskW; ++x) {
            pixels[static_cast<size_t>((oy + s.y) * kAtlasW + ox + x)] = 0xFFFFFFFFu;
        }
    }
}

// The whole tile when the mask files cannot be read: the tile diamond, so the
// fog still hides what it must, only with stepped edges.
std::vector<HAL::EdgeSpan> FallbackTile() {
    std::vector<HAL::EdgeSpan> spans;
    for (int y = 0; y < kMaskH; ++y) {
        const int half = 2 * std::min(y, kMaskH - 1 - y);
        spans.push_back(HAL::EdgeSpan{static_cast<uint8_t>(y), static_cast<uint8_t>(48 - half),
                                      static_cast<uint8_t>(48 + half)});
    }
    return spans;
}

void Init(SDL_Renderer* sdl_r) {
    g.tried = true;
    BuildPatternTable(g.pattern);

    HAL::EdgeMasks tile_edge;
    HAL::EdgeMasks black_edge;
    g.masks_ok = tile_edge.Load("../assets/original/Data/TileEdge.Dat", 2 * kPatterns) &&
                 black_edge.Load("../assets/original/Data/BlkEdge.Dat", kPatterns);
    if (!g.masks_ok) {
        std::cerr << "[Fog] TileEdge.Dat/BlkEdge.Dat unreadable; fog edges will be stepped"
                  << std::endl;
    }

    std::vector<uint32_t> pixels(static_cast<size_t>(kAtlasW) * kAtlasH, 0u);
    if (g.masks_ok) {
        PaintMask(pixels, kWholeTile, tile_edge.Mask(0, 0));
        for (int k = 1; k < kPatterns; ++k) {
            PaintMask(pixels, k, tile_edge.Mask(0, 2 * k + 1));
            PaintMask(pixels, kBlackBase + k, black_edge.Mask(0, k));
        }
    } else {
        PaintMask(pixels, kWholeTile, FallbackTile());
    }
    SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormatFrom(
        pixels.data(), kAtlasW, kAtlasH, 32, kAtlasW * 4, SDL_PIXELFORMAT_RGBA32);
    if (surface) {
        g.atlas = SDL_CreateTextureFromSurface(sdl_r, surface);
        SDL_FreeSurface(surface);
    }
    if (g.atlas) {
        SDL_SetTextureBlendMode(g.atlas, SDL_BLENDMODE_BLEND);
        SDL_SetTextureScaleMode(g.atlas, SDL_ScaleModeNearest);
    }
}

// The frame-sized layer the fog is composed in, and the screen checkerboard:
// made once, again only if the game's size changes.
bool EnsureLayer(SDL_Renderer* sdl_r, int w, int h) {
    if (g.layer && g.layer_w == w && g.layer_h == h) return true;
    if (g.layer) SDL_DestroyTexture(g.layer);
    if (g.checker) SDL_DestroyTexture(g.checker);
    g.layer = SDL_CreateTexture(sdl_r, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, w, h);
    g.layer_w = w;
    g.layer_h = h;

    // Opaque where the dither paints black: x + y even.
    std::vector<uint32_t> pixels(static_cast<size_t>(w) * static_cast<size_t>(h));
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            pixels[static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)] =
                ((x + y) & 1) == 0 ? 0xFFFFFFFFu : 0u;
        }
    }
    SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormatFrom(
        pixels.data(), w, h, 32, w * 4, SDL_PIXELFORMAT_RGBA32);
    g.checker = surface ? SDL_CreateTextureFromSurface(sdl_r, surface) : nullptr;
    if (surface) SDL_FreeSurface(surface);
    if (!g.layer || !g.checker) return false;

    SDL_SetTextureBlendMode(g.layer, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(g.layer, SDL_ScaleModeNearest);
    // Multiplies the layer's alpha by the checkerboard's, colour untouched.
    static const SDL_BlendMode multiply_alpha = SDL_ComposeCustomBlendMode(
        SDL_BLENDFACTOR_ZERO, SDL_BLENDFACTOR_ONE, SDL_BLENDOPERATION_ADD,
        SDL_BLENDFACTOR_ZERO, SDL_BLENDFACTOR_SRC_ALPHA, SDL_BLENDOPERATION_ADD);
    SDL_SetTextureBlendMode(g.checker, multiply_alpha);
    return true;
}

// Lays one mask cell over tile (x, y): a plain rectangle on flat ground, or
// the terrain's own hill fan, so the shape follows the ground as drawn.
void AddTile(std::vector<SDL_Vertex>& vertices, std::vector<int>& indices,
             const Physics::Grid& grid, int x, int y, int cell,
             float cam_x, float cam_y, float zoom) {
    const float cu = static_cast<float>((cell % kAtlasColumns) * kCellW + 1);
    const float cv = static_cast<float>((cell / kAtlasColumns) * kCellH + 1);
    auto vertex = [&](float wx, float wy, float tx, float ty) {
        SDL_Vertex v;
        v.position = SDL_FPoint{(wx - cam_x) * zoom, (wy - cam_y) * zoom};
        v.color = SDL_Color{0, 0, 0, 255};
        v.tex_coord = SDL_FPoint{(cu + tx) / static_cast<float>(kAtlasW),
                                 (cv + ty) / static_cast<float>(kAtlasH)};
        return v;
    };
    const float sx = static_cast<float>((x - y) * (Core::IsometricMath::TILE_WIDTH / 2));
    const float sy = static_cast<float>((x + y) * (Core::IsometricMath::TILE_HEIGHT / 2));
    const float h_top = Ground::CornerLevel(grid, x, y);
    const float h_right = Ground::CornerLevel(grid, x + 1, y);
    const float h_bottom = Ground::CornerLevel(grid, x + 1, y + 1);
    const float h_left = Ground::CornerLevel(grid, x, y + 1);
    const float h_mid = static_cast<float>(grid.GetTile(x, y).elevation);
    const int base = static_cast<int>(vertices.size());

    if (h_top == 0.0f && h_right == 0.0f && h_bottom == 0.0f && h_left == 0.0f && h_mid == 0.0f) {
        // The 97x49 image box, as TerrainRenderer lays a flat tile. At 1:1
        // the masks meet their neighbours' exactly; scaled, a texel lands on
        // a pixel or not by rounding, so each box is let out a pixel to close
        // the pinholes that would leave where four tiles meet.
        const float e = zoom == 1.0f ? 0.0f : 1.0f / zoom;
        const float l = sx - 48.0f - e;
        const float t = sy - 24.0f - e;
        const float r = sx - 48.0f + kMaskW + e;
        const float b = sy - 24.0f + kMaskH + e;
        vertices.push_back(vertex(l, t, 0.0f, 0.0f));
        vertices.push_back(vertex(r, t, kMaskW, 0.0f));
        vertices.push_back(vertex(r, b, kMaskW, kMaskH));
        vertices.push_back(vertex(l, b, 0.0f, kMaskH));
        const int quad[6] = {0, 1, 2, 0, 2, 3};
        for (int i : quad) indices.push_back(base + i);
        return;
    }
    // The corners raised to the ground's height and pushed out a pixel, the
    // same fan TerrainRenderer bakes a hillside tile with.
    const float lift = Ground::kLevelPx;
    constexpr float kBleed = 1.0f;
    vertices.push_back(vertex(sx, sy - 24.0f - h_top * lift - kBleed, 48.0f, 0.0f));
    vertices.push_back(vertex(sx + 48.0f + 2.0f * kBleed, sy - h_right * lift, 96.0f, 24.0f));
    vertices.push_back(vertex(sx, sy + 24.0f - h_bottom * lift + kBleed, 48.0f, 48.0f));
    vertices.push_back(vertex(sx - 48.0f - 2.0f * kBleed, sy - h_left * lift, 0.0f, 24.0f));
    vertices.push_back(vertex(sx, sy - h_mid * lift, 48.0f, 24.0f));
    const int fan[12] = {0, 1, 4, 1, 2, 4, 2, 3, 4, 3, 0, 4};
    for (int i : fan) indices.push_back(base + i);
}

} // namespace

void FogRenderer::Draw(ECS::Simulation& sim, Renderer& renderer, SDL_Renderer* sdl_r) {
    const auto& registry = sim.GetRegistry();
    const auto* map = registry.ctx().find<ECS::Vision::Map>();
    if (map == nullptr || map->reveal == Core::Reveal::AllVisible) return;
    if (!g.tried) Init(sdl_r);
    if (!g.atlas) return;
    const Core::PlayerId viewer = Core::LocalPlayer();
    const auto& grid = sim.GetGrid();
    const int w = map->width;
    const int h = map->height;
    // With the map revealed nothing is unexplored, so no black edge either,
    // not even at the map's border.
    const bool all_explored =
        map->reveal == Core::Reveal::Explored ||
        (viewer < 16 && ((map->explore_all | map->see_all) & (1u << viewer)) != 0);

    // The tiles on screen: the world rectangle under the four corners of the
    // view, widened so hills raised into view from below are not missed.
    const float zoom = renderer.camera_zoom;
    const float inv = 1.0f / zoom;
    const float vw = renderer.view_w * inv;
    const float vh = renderer.view_h * inv;
    const float cx = renderer.camera_x;
    const float cy = renderer.camera_y;
    const std::pair<float, float> corners[4] = {
        Core::IsometricMath::ScreenToWorld(cx, cy),
        Core::IsometricMath::ScreenToWorld(cx + vw, cy),
        Core::IsometricMath::ScreenToWorld(cx, cy + vh),
        Core::IsometricMath::ScreenToWorld(cx + vw, cy + vh),
    };
    float min_x = corners[0].first, max_x = min_x, min_y = corners[0].second, max_y = min_y;
    for (const auto& c : corners) {
        min_x = std::min(min_x, c.first);
        max_x = std::max(max_x, c.first);
        min_y = std::min(min_y, c.second);
        max_y = std::max(max_y, c.second);
    }
    const int x0 = std::max(0, static_cast<int>(std::floor(min_x)) - 2);
    const int y0 = std::max(0, static_cast<int>(std::floor(min_y)) - 2);
    const int x1 = std::min(w, static_cast<int>(std::ceil(max_x)) + 5);
    const int y1 = std::min(h, static_cast<int>(std::ceil(max_y)) + 5);
    if (x0 >= x1 || y0 >= y1) return;

    // Every tile's state once, with a ring of neighbours round the range.
    const int cols = x1 - x0 + 2;
    const int rows = y1 - y0 + 2;
    g.tiles.resize(static_cast<size_t>(cols) * static_cast<size_t>(rows));
    for (int j = 0; j < rows; ++j) {
        for (int i = 0; i < cols; ++i) {
            const int tx = x0 - 1 + i;
            const int ty = y0 - 1 + j;
            uint8_t s = kOffMap;
            if (tx >= 0 && ty >= 0 && tx < w && ty < h) {
                s = ECS::Vision::Visible(registry, viewer, tx, ty)    ? kSeen
                    : ECS::Vision::Explored(registry, viewer, tx, ty) ? kFogged
                                                                      : kUnexplored;
            }
            g.tiles[static_cast<size_t>(j * cols + i)] = s;
        }
    }
    auto state = [&](int tx, int ty) {
        return g.tiles[static_cast<size_t>((ty - y0 + 1) * cols + (tx - x0 + 1))];
    };
    // The neighbour pattern of tile (x, y) for a test. Off the map an edge
    // neighbour takes `off_map`, a corner never counts.
    auto pattern = [&](int x, int y, bool (*hidden)(uint8_t), bool off_map) {
        auto edge = [&](int nx, int ny) {
            const uint8_t s = state(nx, ny);
            return s == kOffMap ? off_map : hidden(s);
        };
        auto corner = [&](int nx, int ny) {
            const uint8_t s = state(nx, ny);
            return s != kOffMap && hidden(s);
        };
        int bits = 0;
        if (corner(x - 1, y + 1)) bits |= kLeftCorner;
        if (corner(x + 1, y + 1)) bits |= kBottomCorner;
        if (corner(x + 1, y - 1)) bits |= kRightCorner;
        if (corner(x - 1, y - 1)) bits |= kTopCorner;
        if (edge(x, y + 1)) bits |= kLowerLeft;
        if (edge(x + 1, y)) bits |= kLowerRight;
        if (edge(x, y - 1)) bits |= kUpperRight;
        if (edge(x - 1, y)) bits |= kUpperLeft;
        return static_cast<int>(g.pattern[bits]);
    };
    bool (*not_seen)(uint8_t) = [](uint8_t s) { return s != kSeen; };
    bool (*unexplored)(uint8_t) = [](uint8_t s) { return s == kUnexplored; };

    g.dither_vertices.clear();
    g.dither_indices.clear();
    g.black_vertices.clear();
    g.black_indices.clear();
    const float half_w = Core::IsometricMath::TILE_WIDTH / 2.0f;
    const float view_x1 = cx + vw;
    const float view_y1 = cy + vh;
    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
            const float sx = (static_cast<float>(x) - static_cast<float>(y)) * half_w;
            const float sy = (static_cast<float>(x) + static_cast<float>(y)) *
                             (Core::IsometricMath::TILE_HEIGHT / 2.0f);
            // Up to eight levels of hill can lift a tile into view.
            if (sx + 50.0f < cx || sx - 50.0f > view_x1 || sy + 26.0f < cy ||
                sy - 26.0f - 8.0f * Ground::kLevelPx > view_y1) {
                continue;
            }
            const uint8_t s = state(x, y);
            if (s == kUnexplored) {
                AddTile(g.black_vertices, g.black_indices, grid, x, y, kWholeTile, cx, cy, zoom);
                continue;
            }
            if (s == kSeen) {
                const int k = pattern(x, y, not_seen, false);
                if (k != 0) AddTile(g.dither_vertices, g.dither_indices, grid, x, y, k, cx, cy, zoom);
            } else {
                AddTile(g.dither_vertices, g.dither_indices, grid, x, y, kWholeTile, cx, cy, zoom);
            }
            if (!all_explored && g.masks_ok) {
                const int k = pattern(x, y, unexplored, true);
                if (k != 0) {
                    AddTile(g.black_vertices, g.black_indices, grid, x, y, kBlackBase + k,
                            cx, cy, zoom);
                }
            }
        }
    }
    if (g.dither_indices.empty() && g.black_indices.empty()) return;

    const int fw = static_cast<int>(renderer.view_w);
    const int fh = static_cast<int>(renderer.view_h);
    if (!EnsureLayer(sdl_r, fw, fh)) return;

    SDL_Texture* frame = SDL_GetRenderTarget(sdl_r);
    SDL_SetRenderTarget(sdl_r, g.layer);
    SDL_RenderSetScale(sdl_r, 1.0f, 1.0f);
    SDL_SetRenderDrawBlendMode(sdl_r, SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(sdl_r, 0, 0, 0, 0);
    SDL_RenderClear(sdl_r);
    if (!g.dither_indices.empty()) {
        SDL_RenderGeometry(sdl_r, g.atlas, g.dither_vertices.data(),
                           static_cast<int>(g.dither_vertices.size()), g.dither_indices.data(),
                           static_cast<int>(g.dither_indices.size()));
        // Keep every other pixel of the dithered shapes, fixed to the screen.
        SDL_RenderCopy(sdl_r, g.checker, nullptr, nullptr);
    }
    if (!g.black_indices.empty()) {
        SDL_RenderGeometry(sdl_r, g.atlas, g.black_vertices.data(),
                           static_cast<int>(g.black_vertices.size()), g.black_indices.data(),
                           static_cast<int>(g.black_indices.size()));
    }
    SDL_SetRenderTarget(sdl_r, frame);
    SDL_RenderSetScale(sdl_r, 1.0f, 1.0f);
    SDL_RenderCopy(sdl_r, g.layer, nullptr, nullptr);
    SDL_RenderSetScale(sdl_r, zoom, zoom);
    SDL_SetRenderDrawBlendMode(sdl_r, SDL_BLENDMODE_BLEND);
}

namespace {
SDL_Texture* g_objects_frame = nullptr;
} // namespace

bool FogRenderer::BeginObjects(Renderer& renderer, SDL_Renderer* sdl_r) {
    if (!EnsureLayer(sdl_r, static_cast<int>(renderer.view_w), static_cast<int>(renderer.view_h))) {
        return false;
    }
    g_objects_frame = SDL_GetRenderTarget(sdl_r);
    SDL_SetRenderTarget(sdl_r, g.layer);
    SDL_RenderSetScale(sdl_r, 1.0f, 1.0f);
    SDL_SetRenderDrawBlendMode(sdl_r, SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(sdl_r, 0, 0, 0, 0);
    SDL_RenderClear(sdl_r);
    SDL_SetRenderDrawBlendMode(sdl_r, SDL_BLENDMODE_BLEND);
    SDL_RenderSetScale(sdl_r, renderer.camera_zoom, renderer.camera_zoom);
    return true;
}

void FogRenderer::EndObjects(Renderer& renderer, SDL_Renderer* sdl_r) {
    SDL_RenderSetScale(sdl_r, 1.0f, 1.0f);
    SDL_RenderCopy(sdl_r, g.checker, nullptr, nullptr);
    SDL_SetRenderTarget(sdl_r, g_objects_frame);
    SDL_RenderSetScale(sdl_r, 1.0f, 1.0f);
    SDL_RenderCopy(sdl_r, g.layer, nullptr, nullptr);
    SDL_RenderSetScale(sdl_r, renderer.camera_zoom, renderer.camera_zoom);
    g_objects_frame = nullptr;
}

SDL_BlendMode FogRenderer::EraseBlend() {
    static const SDL_BlendMode erase = SDL_ComposeCustomBlendMode(
        SDL_BLENDFACTOR_ZERO, SDL_BLENDFACTOR_ONE, SDL_BLENDOPERATION_ADD,
        SDL_BLENDFACTOR_ZERO, SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA, SDL_BLENDOPERATION_ADD);
    return erase;
}

} // namespace Rendering
