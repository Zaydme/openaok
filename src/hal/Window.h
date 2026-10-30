#pragma once
#include <SDL2/SDL.h>
#include <string>

/**
 * Hardware Abstraction Layer: window management.
 *
 * The game draws into a frame of its own screen size, and the window shows
 * that frame scaled to fit, letterboxed. Everything above this layer works in
 * the frame's pixels: the interface art is laid out on them 1:1, the mouse is
 * reported in them, and a saved frame is the frame itself.
 */

namespace HAL {

// The screen sizes offered, smallest first.
struct ScreenSize {
    int width;
    int height;
};
inline constexpr ScreenSize kScreenSizes[] = {{800, 600}, {1024, 768}, {1280, 1024}};

class Window {
public:
    Window() = default;
    ~Window() { Shutdown(); }

    // Non-copyable, non-movable (owns SDL resources)
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    struct Config {
        std::string title = "OpenAoK";
        // The game's own screen size: one of kScreenSizes.
        int game_width = 1024;
        int game_height = 768;
        bool resizable = true;
        bool vsync = true;
        // Desktop fullscreen, the frame scaled into the display at its native
        // resolution. Windowed, the window opens at the frame's own size and
        // shows it 1:1.
        bool fullscreen = true;
    };

    bool Init(const Config& config);
    void Shutdown();

    // Changes the game's screen size while running: the options screen.
    bool SetGameSize(int width, int height);
    // Whether the frame fills the desktop rather than a window of its size.
    bool Fullscreen() const;
    // A widescreen frame's width for `height` rows: the screen's own aspect
    // (the fullscreen window's, else the desktop's), never narrower than 4:3.
    int WideWidth(int height) const;

    // Starts a frame: everything after this draws into the game's frame.
    void BeginFrame();
    void Clear(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255);
    // Shows the frame in the window, scaled to fit and letterboxed.
    void Present();
    // Writes the frame drawn since BeginFrame to a BMP, at the game's size.
    bool CaptureFrame(const std::string& path);

    // The game's screen size. Every layout above this layer uses these.
    int GetWidth() const { return game_w; }
    int GetHeight() const { return game_h; }
    void GetSize(int& w, int& h) const { w = game_w; h = game_h; }

    // Maps a window position (an SDL event's, or the mouse's) into the frame.
    void ToGame(int wx, int wy, int& gx, int& gy) const;
    // And a rectangle of the frame into the window.
    SDL_Rect ToWindow(const SDL_Rect& in_game) const;
    // Rewrites a mouse event's coordinates into the frame's pixels, so input
    // never sees the window's own.
    void TranslateEvent(SDL_Event& event) const;
    // The mouse, in the frame's pixels.
    void MouseState(int& x, int& y) const;
    // A scripted pointer: from now on MouseState reports this point of the
    // frame instead of the real mouse.
    void HoldMouse(int gx, int gy) {
        held_mouse = true;
        held_x = gx;
        held_y = gy;
    }

    // The SDL handles, for the rendering layer to draw with.
    SDL_Renderer* GetSDLRenderer() { return renderer; }
    SDL_Window*   GetSDLWindow()   { return window; }

private:
    // Where the frame lands in the window, in window points (for the mouse)
    // and in output pixels (for drawing), largest that fits at its own aspect.
    void Fit(int outer_w, int outer_h, SDL_Rect& out) const;
    // Fullscreen, keeps the pointer on the game's picture; windowed, frees it.
    void ConfineMouse() const;

    SDL_Window*   window   = nullptr;
    SDL_Renderer* renderer = nullptr;
    SDL_Texture*  frame    = nullptr;
    int game_w = 1024;
    int game_h = 768;
    bool held_mouse = false;
    int held_x = 0;
    int held_y = 0;
};

// The one window, for the layers that must turn mouse positions into frame
// pixels but are not handed the window: the interface polls the mouse.
Window* MainWindow();

} // namespace HAL
