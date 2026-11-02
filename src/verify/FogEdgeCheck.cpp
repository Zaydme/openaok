#include "verify/Harness.h"

#include <SDL2/SDL.h>

#include <cstdint>
#include <cstdio>
#include <vector>

#include "core/IsometricMath.h"
#include "ecs/Simulation.h"
#include "rendering/FogRenderer.h"
#include "rendering/Renderer.h"

namespace Verify {

namespace {

constexpr int kFrameW = 640;
constexpr int kFrameH = 480;
constexpr float kZoom = 0.43f;
// How far past the map's edge, in tiles, the black must reach: farther than
// the ground's edge drawn zoomed, short of the ring's own far edge.
constexpr float kPast = 0.7f;
// How far into the map the view's tiles are unexplored: the top corner.
constexpr float kInto = 3.0f;

} // namespace

// The map's top corner, never explored, zoomed out: the fog lays black over
// it and half a tile and more past the map's edge, so no ground drawn there
// shows past the black.
bool CheckFogEdge() {
    const char* verdict = nullptr;
    int checked = 0, open = 0;
    SDL_Surface* screen = SDL_CreateRGBSurfaceWithFormat(0, kFrameW, kFrameH, 32, SDL_PIXELFORMAT_RGBA32);
    SDL_Renderer* r = screen != nullptr ? SDL_CreateSoftwareRenderer(screen) : nullptr;
    SDL_Texture* frame = r != nullptr
        ? SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, kFrameW, kFrameH)
        : nullptr;
    if (frame == nullptr) {
        verdict = "no renderer";
    } else {
        ECS::Simulation sim;
        sim.Init();
        sim.Update(16);
        Rendering::Renderer view;
        view.view_w = static_cast<float>(kFrameW);
        view.view_h = static_cast<float>(kFrameH);
        view.camera_zoom = view.target_camera_zoom = kZoom;
        // The map's top corner, (-0.5, -0.5), in the middle of the frame.
        view.camera_x = -view.view_w / 2.0f / kZoom;
        view.camera_y = -Core::IsometricMath::TILE_HEIGHT / 2.0f - view.view_h / 2.0f / kZoom;
        // Ground everywhere, past the map's edge too.
        SDL_SetRenderTarget(r, frame);
        SDL_SetRenderDrawColor(r, 0, 255, 0, 255);
        SDL_RenderClear(r);
        Rendering::SetZoom(r, kZoom);
        Rendering::FogRenderer::Draw(sim, view, r);
        Rendering::SetZoom(r, 1.0f);
        std::vector<uint32_t> pixels(static_cast<size_t>(kFrameW) * kFrameH);
        SDL_RenderReadPixels(r, nullptr, SDL_PIXELFORMAT_RGBA32, pixels.data(), kFrameW * 4);
        SDL_SetRenderTarget(r, nullptr);
        for (int y = 0; y < kFrameH; ++y) {
            for (int x = 0; x < kFrameW; ++x) {
                const auto [wx, wy] = Core::IsometricMath::ScreenToWorld(
                    view.camera_x + (static_cast<float>(x) + 0.5f) / kZoom,
                    view.camera_y + (static_cast<float>(y) + 0.5f) / kZoom);
                if (wx < -0.5f - kPast || wy < -0.5f - kPast || wx > kInto || wy > kInto) continue;
                ++checked;
                const uint32_t p = pixels[static_cast<size_t>(y) * kFrameW + x];
                const uint8_t* c = reinterpret_cast<const uint8_t*>(&p);
                if (c[0] != 0 || c[1] != 0 || c[2] != 0) ++open;
            }
        }
        if (checked == 0) {
            verdict = "the view holds none of the map's corner";
        } else if (open > 0) {
            verdict = "ground shows past the black at the map's unexplored edge";
        }
    }
    Rendering::FogRenderer::Forget();
    if (frame != nullptr) SDL_DestroyTexture(frame);
    if (r != nullptr) SDL_DestroyRenderer(r);
    if (screen != nullptr) SDL_FreeSurface(screen);
    std::printf("fogedge=%s checked=%d open=%d\n", verdict == nullptr ? "ok" : verdict, checked, open);
    return verdict == nullptr;
}

} // namespace Verify
