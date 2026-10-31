#include "Renderer.h"
#include "Text.h"
#include "FogRenderer.h"
#include "TerrainRenderer.h"
#include "EntityRenderer.h"
#include "UIRenderer.h"
#include "Messages.h"
#include "HudLayout.h"
#include "core/ConfigManager.h"
#include "core/InputManager.h"
#include "core/IsometricMath.h"
#include "ecs/components/Owner.h"
#include "ecs/components/Sprite.h"
#include "hal/GraphicCache.h"
#include "hal/TextureManager.h"
#include <iostream>
#include <cstdlib>
#include <algorithm>
#include <cmath>
#include <chrono>

namespace Rendering {

bool Renderer::Init(HAL::Window& window) {
    std::cout << "[Renderer] Initialized." << std::endl;
    SDL_ShowCursor(SDL_DISABLE);
    
    if (!HAL::TextureManager::Get().Init(window.GetSDLRenderer())) {
        return false;
    }
    
    camera_zoom = 1.0f;
    return true;
}

void Renderer::Warm(ECS::Simulation& sim) {
    auto& graphics = HAL::GraphicCache::Get();
    auto& cfg = Core::ConfigManager::Get();
    auto& registry = sim.GetRegistry();
    auto sprites = registry.view<ECS::Components::Sprite>();
    for (auto e : sprites) {
        const auto& sprite = sprites.get<ECS::Components::Sprite>(e);
        if (sprite.slp_id < 0) continue;
        const auto slp = static_cast<uint32_t>(sprite.slp_id);
        if (graphics.GetAtlas(slp) == nullptr) continue;
        int owner = 0;
        if (const auto* o = registry.try_get<ECS::Components::Owner>(e)) owner = o->player_id;
        graphics.GetPlayerLayer(slp, owner);
        graphics.GetOutlineLayer(slp);
        const int shadow = cfg.GetShadowSlp(sprite.slp_id);
        if (shadow >= 0) {
            graphics.GetAtlas(static_cast<uint32_t>(shadow));
            graphics.GetShadowLayer(static_cast<uint32_t>(shadow));
        }
    }
    int textures = 0;
    int64_t bytes = 0;
    graphics.Count(textures, bytes);
    std::cout << "[Renderer] Art ready: " << textures << " textures, " << bytes / (1024 * 1024)
              << " MB" << std::endl;
}

void SetZoom(SDL_Renderer* sdl_r, float zoom) {
    SDL_RenderSetScale(sdl_r, zoom, zoom);
    int w = 0, h = 0;
    if (SDL_Texture* target = SDL_GetRenderTarget(sdl_r)) {
        SDL_QueryTexture(target, nullptr, nullptr, &w, &h);
    } else {
        SDL_GetRendererOutputSize(sdl_r, &w, &h);
    }
    const SDL_Rect whole{0, 0, static_cast<int>(std::ceil(w / zoom)), static_cast<int>(std::ceil(h / zoom))};
    SDL_RenderSetViewport(sdl_r, &whole);
}

void Renderer::ZoomBy(float factor, float x, float y, bool at_once) {
    const float from = target_camera_zoom;
    const float to = std::clamp(from * factor, kZoomOut, kZoomIn);
    if (to == from) return;
    // The world point under (x, y) stays under it.
    target_camera_x += x / from - x / to;
    target_camera_y += y / from - y / to;
    target_camera_zoom = to;
    if (at_once) {
        camera_x += x / camera_zoom - x / to;
        camera_y += y / camera_zoom - y / to;
        camera_zoom = to;
    }
}

void Renderer::Render(ECS::Simulation& sim, HAL::Window& window, float alpha) {
    SDL_Renderer* sdl_r = window.GetSDLRenderer();
    int win_w = window.GetWidth();
    int win_h = window.GetHeight();
    // The interface lays itself out for this frame (HudLayout.h).
    SetFrame(win_w, win_h);
    
    // Update dynamic viewport size from the actual window
    view_w = static_cast<float>(win_w);
    view_h = static_cast<float>(win_h);
    
    // Clamp Camera to Map Bounds
    auto& grid = sim.GetGrid();
    float gw = static_cast<float>(grid.GetWidth());
    float gh = static_cast<float>(grid.GetHeight());
    auto p_left = Core::IsometricMath::WorldToScreen(Core::Fixed32(0), Core::Fixed32(gh));
    auto p_right = Core::IsometricMath::WorldToScreen(Core::Fixed32(gw), Core::Fixed32(0));
    auto p_top = Core::IsometricMath::WorldToScreen(Core::Fixed32(0), Core::Fixed32(0));
    auto p_bottom = Core::IsometricMath::WorldToScreen(Core::Fixed32(gw), Core::Fixed32(gh));
    
    // Account for zoom when computing how much of the map the viewport shows
    float scaled_half_w = (view_w / camera_zoom) / 2.0f;
    float scaled_half_h = (view_h / camera_zoom) / 2.0f;
    
    float min_cx = p_left.x - scaled_half_w;
    float max_cx = p_right.x - scaled_half_w;
    float min_cy = p_top.y - scaled_half_h;
    float max_cy = p_bottom.y - scaled_half_h;
    
    target_camera_x = std::max(min_cx, std::min(max_cx, target_camera_x));
    target_camera_y = std::max(min_cy, std::min(max_cy, target_camera_y));
    camera_x = std::max(min_cx, std::min(max_cx, camera_x));
    camera_y = std::max(min_cy, std::min(max_cy, camera_y));
    
    // Animate water phase (single increment, cycles through 100 texture variants)
    water_phase += 0.15f;
    if (water_phase >= 100.0f) water_phase -= 100.0f;
    
    window.BeginFrame();
    window.Clear(0, 0, 0);   // black beyond the map's edge
    
    SetZoom(sdl_r, camera_zoom);

    using Clock = std::chrono::steady_clock;
    auto since = [](Clock::time_point& mark) {
        const Clock::time_point now = Clock::now();
        const int64_t us = std::chrono::duration_cast<std::chrono::microseconds>(now - mark).count();
        mark = now;
        return us;
    };
    Clock::time_point mark = Clock::now();
    TerrainRenderer::DrawWaterLayer(sim, *this, sdl_r);
    TerrainRenderer::DrawTerrain(sim, *this, sdl_r);
    cost.terrain = since(mark);
    // The fog goes over the ground and what lies on it; what stands on seen
    // ground is drawn whole over the fog, even where it reaches over fog or
    // black.
    EntityRenderer::DrawGroundLevel(sim, *this, sdl_r, alpha);
    cost.ground = since(mark);
    FogRenderer::Draw(sim, *this, sdl_r);
    cost.fog = since(mark);
    EntityRenderer::DrawStanding(*this, sdl_r);
    cost.standing = since(mark);
    // --show-grid: every blocked tile outlined, exactly as the simulation's
    // walkability reads it, to check the ground against the art standing on it.
    if (show_walkability) {
        auto& g = sim.GetGrid();
        SDL_SetRenderDrawBlendMode(sdl_r, SDL_BLENDMODE_BLEND);
        for (int y = 0; y < g.GetHeight(); ++y) {
            for (int x = 0; x < g.GetWidth(); ++x) {
                if (g.IsWalkable(x, y)) continue;
                auto c = [&](float wx, float wy) {
                    auto p = Core::IsometricMath::WorldToScreen(Core::Fixed32(wx), Core::Fixed32(wy));
                    return SDL_FPoint{p.x - camera_x, p.y - camera_y};
                };
                const float fx = x - 0.5f, fy = y - 0.5f;
                const SDL_FPoint q[5] = {c(fx, fy), c(fx + 1.0f, fy), c(fx + 1.0f, fy + 1.0f), c(fx, fy + 1.0f), c(fx, fy)};
                SDL_SetRenderDrawColor(sdl_r, 255, 0, 0, 255);
                SDL_RenderDrawLinesF(sdl_r, q, 5);
            }
        }
    }
    
    SetZoom(sdl_r, 1.0f);
    Messages::Draw(sdl_r, win_w, win_h);
    UIRenderer::DrawTimePanels(sim, sdl_r, win_w, win_h);
    UIRenderer::DrawScores(sim, sdl_r, win_w, win_h);
    UIRenderer::DrawInterface(sim, *this, sdl_r, win_w, win_h);
    // Box-select outline, in window pixels so zoom does not thicken it.
    SDL_Rect sel;
    if (Core::InputManager::Get().GetSelectionRect(sel)) {
        // A plain one-pixel white rectangle.
        SDL_SetRenderDrawColor(sdl_r, 255, 255, 255, 255);
        SDL_RenderDrawRect(sdl_r, &sel);
    }

    if (!banner.empty()) {
        // Across the top of the view.
        const int w = Text::Width(Text::kGeorgia9B, banner);
        Text::DrawShadowed(sdl_r, Text::kGeorgia9B, (win_w - w) / 2, 48, banner,
                           SDL_Color{255, 255, 255, 255}, SDL_Color{0, 0, 0, 255});
    }

    if (show_fps) UIRenderer::DrawFPS(sdl_r, current_fps, win_w);
    cost.interface = since(mark);
}

void Renderer::Finish(ECS::Simulation& sim, HAL::Window& window) {
    const auto start = std::chrono::steady_clock::now();
    UIRenderer::DrawCursor(sim, *this, window.GetSDLRenderer());
    window.Present();
    cost.finish = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - start).count();
}

} // namespace Rendering
