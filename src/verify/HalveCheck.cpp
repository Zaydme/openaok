#include "verify/Harness.h"

#include <SDL2/SDL.h>

#include <cstdint>
#include <cstdio>
#include <vector>

#include "rendering/TerrainRenderer.h"

namespace Verify {

namespace {

constexpr int kSide = 96;           // a piece's side at full size
constexpr int kHalf = kSide / 2;
constexpr int kStride = kSide + 1;  // with the clear column and row Halve needs

// One of two pieces of ground meeting along an isometric edge, as two chunks'
// tiles meet: the first solid before the edge, the second from a pixel short
// of it, so they overlap by a pixel. `down` slopes the edge the other way.
// Its pixels are RGBA8888, each a colour of its own.
std::vector<uint32_t> Piece(bool first, int edge, bool down) {
    std::vector<uint32_t> px(static_cast<size_t>(kStride) * kStride, 0);
    for (int y = 0; y < kSide; ++y) {
        for (int x = 0; x < kSide; ++x) {
            const int along = down ? x - 2 * y + 2 * kSide : x + 2 * y;
            if (first ? along >= edge : along < edge - 1) continue;
            const uint32_t r = static_cast<uint32_t>(x * 37 + y * 11) & 0xffu;
            const uint32_t g = static_cast<uint32_t>(x * 5 + y * 53) & 0xffu;
            const uint32_t b = first ? 40u : 200u;
            px[static_cast<size_t>(y) * kStride + x] = (r << 24) | (g << 16) | (b << 8) | 0xffu;
        }
    }
    return px;
}

// `piece` at half size, through Halve or a plain shrink.
std::vector<uint32_t> Shrunk(SDL_Renderer* r, const std::vector<uint32_t>& piece, bool halve) {
    std::vector<uint32_t> out(static_cast<size_t>(kHalf) * kHalf, 0);
    SDL_Texture* full = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_STATIC, kStride, kStride);
    SDL_Texture* half = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, kHalf, kHalf);
    if (full != nullptr && half != nullptr) {
        SDL_UpdateTexture(full, nullptr, piece.data(), kStride * 4);
        SDL_SetRenderTarget(r, half);
        SDL_SetRenderDrawColor(r, 0, 0, 0, 0);
        SDL_RenderClear(r);
        if (halve) {
            Rendering::TerrainRenderer::Halve(r, full, kSide, kSide);
        } else {
            SDL_SetTextureBlendMode(full, SDL_BLENDMODE_BLEND);
            const SDL_Rect src{0, 0, kSide, kSide};
            SDL_RenderCopy(r, full, &src, nullptr);
        }
        SDL_RenderReadPixels(r, nullptr, SDL_PIXELFORMAT_RGBA8888, out.data(), kHalf * 4);
        SDL_SetRenderTarget(r, nullptr);
    }
    if (full != nullptr) SDL_DestroyTexture(full);
    if (half != nullptr) SDL_DestroyTexture(half);
    return out;
}

bool Solid(const std::vector<uint32_t>& px, int x, int y) {
    return (px[static_cast<size_t>(y) * kHalf + x] & 0xffu) != 0;
}

} // namespace

// Two neighbouring chunks' ground, each shrunk to half size as the view
// zoomed far out draws it: wherever one of them is drawn a pixel out of place,
// as drawing them at a zoom can, the other still covers the ground, so no gap
// shows between them; and every pixel a plain shrink keeps as ground is that
// pixel still.
bool CheckHalve() {
    const char* verdict = nullptr;
    SDL_Surface* screen = SDL_CreateRGBSurfaceWithFormat(0, 8, 8, 32, SDL_PIXELFORMAT_RGBA32);
    SDL_Renderer* r = screen != nullptr ? SDL_CreateSoftwareRenderer(screen) : nullptr;
    int pairs = 0;
    if (r == nullptr) verdict = "no renderer";
    static const SDL_Point kAside[5] = {{0, 0}, {1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    for (const bool down : {false, true}) {
        for (int edge = 100; edge < 108 && verdict == nullptr; ++edge) {
            const std::vector<uint32_t> a = Piece(true, edge, down);
            const std::vector<uint32_t> b = Piece(false, edge, down);
            const std::vector<uint32_t> ha = Shrunk(r, a, true), hb = Shrunk(r, b, true);
            const std::vector<uint32_t> pa = Shrunk(r, a, false), pb = Shrunk(r, b, false);
            ++pairs;
            for (int y = 2; y < kHalf - 2 && verdict == nullptr; ++y) {
                for (int x = 2; x < kHalf - 2 && verdict == nullptr; ++x) {
                    for (const SDL_Point& d : kAside) {
                        if (!Solid(ha, x, y) && !Solid(hb, x + d.x, y + d.y)) {
                            verdict = "a gap opens between two halved chunks a pixel apart";
                            break;
                        }
                    }
                    const size_t i = static_cast<size_t>(y) * kHalf + x;
                    if (verdict == nullptr && ((Solid(pa, x, y) && ha[i] != pa[i]) ||
                                               (Solid(pb, x, y) && hb[i] != pb[i]))) {
                        verdict = "a halved chunk changed a pixel a plain shrink keeps";
                    }
                }
            }
        }
    }
    if (r != nullptr) SDL_DestroyRenderer(r);
    if (screen != nullptr) SDL_FreeSurface(screen);
    std::printf("halve=%s pairs=%d\n", verdict == nullptr ? "ok" : verdict, pairs);
    return verdict == nullptr;
}

} // namespace Verify
