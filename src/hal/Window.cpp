#include "Window.h"
#include "Pad.h"
#include "TextInput.h"
#include "Touch.h"
#include <algorithm>
#include <cmath>
#include <iostream>

namespace HAL {

namespace {
Window* g_main = nullptr;
}

Window* MainWindow() { return g_main; }

bool Window::Init(const Config& config) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0) {
        std::cerr << "[HAL] SDL_Init failed: " << SDL_GetError() << std::endl;
        return false;
    }
    game_w = config.game_width;
    game_h = config.game_height;

    // High-DPI on: the frame is scaled into the display's real pixels. The
    // mouse is mapped from window points separately, so clicks land where
    // they appear to at any density.
    Uint32 flags = SDL_WINDOW_SHOWN | SDL_WINDOW_ALLOW_HIGHDPI;
    if (config.resizable) flags |= SDL_WINDOW_RESIZABLE;
    if (config.fullscreen) flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;

    window = SDL_CreateWindow(
        config.title.c_str(),
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        game_w, game_h,
        flags
    );
    if (!window) {
        std::cerr << "[HAL] SDL_CreateWindow failed: " << SDL_GetError() << std::endl;
        return false;
    }

    Uint32 render_flags = SDL_RENDERER_ACCELERATED | SDL_RENDERER_TARGETTEXTURE;
    if (config.vsync) render_flags |= SDL_RENDERER_PRESENTVSYNC;

    renderer = SDL_CreateRenderer(window, -1, render_flags);
    if (!renderer) {
        std::cerr << "[HAL] SDL_CreateRenderer failed: " << SDL_GetError() << std::endl;
        return false;
    }

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

    SDL_RendererInfo info;
    if (SDL_GetRendererInfo(renderer, &info) == 0) {
        std::cout << "[HAL] Graphics Driver: " << info.name << std::endl;
        std::cout << "[HAL] Hardware Accelerated: " << ((info.flags & SDL_RENDERER_ACCELERATED) ? "Yes" : "No") << std::endl;
        std::cout << "[HAL] Max Texture Size: " << info.max_texture_width << "x" << info.max_texture_height << std::endl;
    }
    if (!SetGameSize(game_w, game_h)) return false;
    g_main = this;

    int actual_w = 0;
    int actual_h = 0;
    SDL_GetWindowSize(window, &actual_w, &actual_h);
    std::cout << "[HAL] Window initialized: " << actual_w << "x" << actual_h
              << (config.fullscreen ? " (fullscreen)" : "")
              << (config.vsync ? " vsync" : " vsync off")
              << ", game " << game_w << "x" << game_h << std::endl;
    return true;
}

bool Window::SetGameSize(int width, int height) {
    if (frame) SDL_DestroyTexture(frame);
    frame = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
                              SDL_TEXTUREACCESS_TARGET, width, height);
    if (!frame) {
        std::cerr << "[HAL] Game frame failed: " << SDL_GetError() << std::endl;
        return false;
    }
    game_w = width;
    game_h = height;
    // A windowed game keeps its window the frame's size, so it is shown 1:1.
    if (!(SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN_DESKTOP)) {
        SDL_SetWindowSize(window, width, height);
    }
    ConfineMouse();
    return true;
}

// Fullscreen, the pointer is kept on the game's picture: out of the letterbox
// bars, and off the strip beside a display's camera notch, which is outside
// the window, where the mouse is not heard and the map stops scrolling.
// Windowed, it is free to leave.
void Window::ConfineMouse() const {
    if (window == nullptr) return;
    if (!Fullscreen()) {
        SDL_SetWindowMouseRect(window, nullptr);
        return;
    }
    int win_w = 0;
    int win_h = 0;
    SDL_GetWindowSize(window, &win_w, &win_h);
    SDL_Rect area;
    Fit(win_w, win_h, area);
    if (area.w > 0 && area.h > 0 && SDL_SetWindowMouseRect(window, &area) != 0) {
        std::cerr << "[HAL] Mouse not confined to the picture: " << SDL_GetError() << std::endl;
    }
}

int Window::WideWidth(int height) const {
    int aspect_w = 4, aspect_h = 3;
    if (window != nullptr) {
        if (Fullscreen()) {
            SDL_GetWindowSize(window, &aspect_w, &aspect_h);
        } else {
            SDL_DisplayMode mode;
            if (SDL_GetDesktopDisplayMode(std::max(0, SDL_GetWindowDisplayIndex(window)), &mode) == 0) {
                aspect_w = mode.w;
                aspect_h = mode.h;
            }
        }
    }
    if (aspect_w <= 0 || aspect_h <= 0) return height * 4 / 3;
    const int wide = static_cast<int>(static_cast<long long>(height) * aspect_w / aspect_h) & ~1;
    return std::max(wide, height * 4 / 3);
}

bool Window::Fullscreen() const {
    return window != nullptr && (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN_DESKTOP) != 0;
}

void Window::Shutdown() {
    if (g_main == this) g_main = nullptr;
    if (frame)    { SDL_DestroyTexture(frame);     frame = nullptr; }
    if (renderer) { SDL_DestroyRenderer(renderer); renderer = nullptr; }
    if (window)   { SDL_DestroyWindow(window);     window = nullptr; }
    SDL_Quit();
}

void Window::BeginFrame() {
    SDL_SetRenderTarget(renderer, frame);
}

void Window::Clear(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    SDL_SetRenderDrawColor(renderer, r, g, b, a);
    SDL_RenderClear(renderer);
}

void Window::Fit(int outer_w, int outer_h, SDL_Rect& out) const {
    const double scale = std::min(static_cast<double>(outer_w) / game_w,
                                  static_cast<double>(outer_h) / game_h);
    out.w = static_cast<int>(std::lround(game_w * scale));
    out.h = static_cast<int>(std::lround(game_h * scale));
    out.x = (outer_w - out.w) / 2;
    out.y = (outer_h - out.h) / 2;
}

void Window::Present() {
    SDL_SetRenderTarget(renderer, nullptr);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    int out_w = 0;
    int out_h = 0;
    SDL_GetRendererOutputSize(renderer, &out_w, &out_h);
    SDL_Rect dest;
    Fit(out_w, out_h, dest);
    // A whole-number scale is drawn nearest-neighbour, keeping every pixel
    // square and sharp; any other scale is filtered.
    const bool whole = dest.w % game_w == 0 && dest.h % game_h == 0;
    SDL_SetTextureScaleMode(frame, whole ? SDL_ScaleModeNearest : SDL_ScaleModeLinear);
    SDL_RenderCopy(renderer, frame, nullptr, &dest);
    SDL_RenderPresent(renderer);
    // The controller's pointer, and the system keyboard when a line is due
    // (Switch), once a frame.
    Pad::Update();
    TextInput::Update();
}

bool Window::CaptureFrame(const std::string& path) {
    SDL_Texture* prev = SDL_GetRenderTarget(renderer);
    SDL_SetRenderTarget(renderer, frame);
    SDL_Surface* shot = SDL_CreateRGBSurfaceWithFormat(0, game_w, game_h, 32,
                                                       SDL_PIXELFORMAT_ARGB8888);
    bool ok = shot != nullptr &&
              SDL_RenderReadPixels(renderer, nullptr, SDL_PIXELFORMAT_ARGB8888,
                                   shot->pixels, shot->pitch) == 0 &&
              SDL_SaveBMP(shot, path.c_str()) == 0;
    if (!ok) std::cerr << "[HAL] Capture failed: " << SDL_GetError() << std::endl;
    if (shot) SDL_FreeSurface(shot);
    SDL_SetRenderTarget(renderer, prev);
    return ok;
}

void Window::ToGame(int wx, int wy, int& gx, int& gy) const {
    int win_w = 0;
    int win_h = 0;
    SDL_GetWindowSize(window, &win_w, &win_h);
    SDL_Rect area;
    Fit(win_w, win_h, area);
    if (area.w <= 0 || area.h <= 0) { gx = wx; gy = wy; return; }
    gx = static_cast<int>(std::floor((wx - area.x) * static_cast<double>(game_w) / area.w));
    gy = static_cast<int>(std::floor((wy - area.y) * static_cast<double>(game_h) / area.h));
}

void Window::TranslateEvent(SDL_Event& event) const {
    // A finger on a touch screen, or a controller's button: made into the
    // mouse and keys, which come as events of their own (hal/Touch, hal/Pad).
    if (Touch::Handle(event) || Pad::Handle(event)) return;
    switch (event.type) {
    case SDL_KEYDOWN:
    case SDL_KEYUP:
        // Android's back button is Escape, backing out of whatever is half
        // done; the system does not close the game.
        if (event.key.keysym.sym == SDLK_AC_BACK) {
            event.key.keysym.sym = SDLK_ESCAPE;
            event.key.keysym.scancode = SDL_SCANCODE_ESCAPE;
        }
        break;
    case SDL_WINDOWEVENT:
        // The picture moves when the window changes size or display, and a
        // confinement is only kept while the window has the focus.
        switch (event.window.event) {
        case SDL_WINDOWEVENT_SHOWN:
        case SDL_WINDOWEVENT_SIZE_CHANGED:
        case SDL_WINDOWEVENT_FOCUS_GAINED:
        case SDL_WINDOWEVENT_DISPLAY_CHANGED:
            ConfineMouse();
            break;
        default:
            break;
        }
        break;
    case SDL_MOUSEMOTION: {
        int gx = 0, gy = 0;
        ToGame(event.motion.x, event.motion.y, gx, gy);
        // The relative motion in frame pixels too, for drags that use it.
        int ox = 0, oy = 0;
        ToGame(event.motion.x - event.motion.xrel, event.motion.y - event.motion.yrel, ox, oy);
        event.motion.x = gx;
        event.motion.y = gy;
        event.motion.xrel = gx - ox;
        event.motion.yrel = gy - oy;
        break;
    }
    case SDL_MOUSEBUTTONDOWN:
    case SDL_MOUSEBUTTONUP: {
        int gx = 0, gy = 0;
        ToGame(event.button.x, event.button.y, gx, gy);
        event.button.x = gx;
        event.button.y = gy;
        break;
    }
    default:
        break;
    }
}

void Window::MouseState(int& x, int& y) const {
    if (held_mouse) {
        x = held_x;
        y = held_y;
        return;
    }
    int wx = 0;
    int wy = 0;
    SDL_GetMouseState(&wx, &wy);
    ToGame(wx, wy, x, y);
}

} // namespace HAL
