#include "hal/Pad.h"

#include <algorithm>
#include <cmath>

#include "hal/Pointer.h"
#include "hal/TextInput.h"
#include "hal/Window.h"

#ifdef __SWITCH__
// SDL's own way in for a key (events/SDL_keyboard_c.h), which its Switch port
// uses for a USB keyboard. SDL is linked in whole on the Switch.
extern "C" int SDL_SendKeyboardKey(Uint8 state, SDL_Scancode scancode);
#endif

namespace HAL::Pad {

namespace {

// The buttons as SDL's Switch port numbers them (joystick/switch).
enum Button : int {
    kA = 0,
    kB = 1,
    kX = 2,
    kY = 3,
    kL = 6,
    kR = 7,
    kZL = 8,
    kZR = 9,
    kPlus = 10,
    kMinus = 11,
    kLeft = 12,
    kUp = 13,
    kRight = 14,
    kDown = 15,
};

// How far a stick leans before it counts, of its full reach: a stick at rest
// is seldom quite at its middle.
constexpr float kDeadZone = 0.15f;
// The pointer's speed with the stick pushed all the way, in screen heights a
// second.
constexpr float kPointerSpeed = 1.4f;
constexpr Uint32 kRumbleMs = 30;

struct State {
    bool enabled = false;
    bool opened = false;
    SDL_Joystick* joystick = nullptr;
    float x = -1.0f;   // the pointer, window pixels; below zero until placed
    float y = -1.0f;
    Uint64 last = 0;   // SDL's performance counter at the last Update
};
State g;

void Key(SDL_Scancode key, bool down) {
#ifdef __SWITCH__
    SDL_SendKeyboardKey(down ? SDL_PRESSED : SDL_RELEASED, key);
#else
    (void)key;
    (void)down;
#endif
}

// The key a button presses, or SDL_SCANCODE_UNKNOWN.
SDL_Scancode KeyOf(int button) {
    switch (button) {
    case kX: return SDL_SCANCODE_PERIOD;
    case kY: return SDL_SCANCODE_ESCAPE;
    case kL: return SDL_SCANCODE_H;
    case kR: return SDL_SCANCODE_COMMA;
    case kZL: return SDL_SCANCODE_LSHIFT;
    case kZR: return SDL_SCANCODE_HOME;
    case kPlus: return SDL_SCANCODE_F10;
    case kMinus: return SDL_SCANCODE_RETURN;
    case kUp: return SDL_SCANCODE_F3;
    case kDown: return SDL_SCANCODE_SPACE;
    case kLeft: return SDL_SCANCODE_KP_MINUS;
    case kRight: return SDL_SCANCODE_KP_PLUS;
    default: return SDL_SCANCODE_UNKNOWN;
    }
}

void Open() {
    if (g.opened) return;
    g.opened = true;
    if (SDL_InitSubSystem(SDL_INIT_JOYSTICK) != 0 || SDL_NumJoysticks() < 1) return;
    g.joystick = SDL_JoystickOpen(0);
}

// A stick's lean, -1 to 1 each way: nothing inside the dead zone, and beyond
// it the square of how far, so that a small push moves finely.
void Lean(int axis_x, int axis_y, float& x, float& y) {
    x = 0.0f;
    y = 0.0f;
    if (g.joystick == nullptr) return;
    const float ax = SDL_JoystickGetAxis(g.joystick, axis_x) / 32767.0f;
    const float ay = SDL_JoystickGetAxis(g.joystick, axis_y) / 32767.0f;
    const float length = std::hypot(ax, ay);
    if (length <= kDeadZone) return;
    const float reach = std::min(1.0f, (length - kDeadZone) / (1.0f - kDeadZone));
    const float scale = reach * reach / length;
    x = std::clamp(ax * scale, -1.0f, 1.0f);
    y = std::clamp(ay * scale, -1.0f, 1.0f);
}

} // namespace

void Enable() { g.enabled = true; }

bool Enabled() { return g.enabled; }

bool Handle(const SDL_Event& event) {
    if (!g.enabled || (event.type != SDL_JOYBUTTONDOWN && event.type != SDL_JOYBUTTONUP)) return false;
    const bool down = event.type == SDL_JOYBUTTONDOWN;
    const int button = event.jbutton.button;
    if (button == kA || button == kB) {
        const Uint8 mouse = button == kA ? SDL_BUTTON_LEFT : SDL_BUTTON_RIGHT;
        if (down) {
            Pointer::Press(mouse);
        } else {
            Pointer::Release(mouse);
        }
        return true;
    }
    // On a screen with a name to type, - brings up the keyboard; elsewhere it
    // is Enter, which opens the chat line.
    if (button == kMinus && down && TextInput::ShowForFields()) return true;
    const SDL_Scancode key = KeyOf(button);
    if (key == SDL_SCANCODE_UNKNOWN) return true;
    // Shift is held as long as ZL is; every other key is pressed and let go
    // as its button goes down.
    if (key == SDL_SCANCODE_LSHIFT) {
        Key(key, down);
    } else if (down) {
        Key(key, true);
        Key(key, false);
    }
    return true;
}

void Update() {
    if (!g.enabled) return;
    Open();
    const Uint64 now = SDL_GetPerformanceCounter();
    const float dt = g.last == 0 ? 0.0f
        : std::min(0.1f, static_cast<float>(now - g.last) / static_cast<float>(SDL_GetPerformanceFrequency()));
    g.last = now;
    Window* window = MainWindow();
    if (window == nullptr || window->GetSDLWindow() == nullptr) return;
    int w = 0, h = 0;
    SDL_GetWindowSize(window->GetSDLWindow(), &w, &h);
    if (w <= 0 || h <= 0) return;
    // The pointer starts in the middle of the screen, and goes on from wherever
    // a finger or a mouse has since put it.
    if (g.x < 0.0f) {
        g.x = w / 2.0f;
        g.y = h / 2.0f;
        Pointer::Move(g.x, g.y, false);
    }
    int mx = 0, my = 0;
    SDL_GetMouseState(&mx, &my);
    if (static_cast<int>(g.x) != mx || static_cast<int>(g.y) != my) {
        g.x = static_cast<float>(mx);
        g.y = static_cast<float>(my);
    }
    float lx = 0.0f, ly = 0.0f;
    Lean(0, 1, lx, ly);
    if (lx == 0.0f && ly == 0.0f) return;
    const float step = kPointerSpeed * static_cast<float>(h) * dt;
    g.x = std::clamp(g.x + lx * step, 0.0f, static_cast<float>(w - 1));
    g.y = std::clamp(g.y + ly * step, 0.0f, static_cast<float>(h - 1));
    Pointer::Move(g.x, g.y, false);
}

void Scroll(float& x, float& y) {
    x = 0.0f;
    y = 0.0f;
    if (g.enabled) Lean(2, 3, x, y);
}

void Rumble() {
    if (g.joystick != nullptr) SDL_JoystickRumble(g.joystick, 0x6000, 0x6000, kRumbleMs);
}

} // namespace HAL::Pad
