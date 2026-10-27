#include "hal/Touch.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>

#include "hal/Pad.h"
#include "hal/Pointer.h"
#include "hal/Window.h"

#ifdef __ANDROID__
#include <android/log.h>
#include <jni.h>
#endif

namespace HAL::Touch {

namespace {

// How long a finger stays put to right-click, and how far it may wander
// meanwhile -- of the screen's height -- and still be a tap or a hold.
constexpr Uint32 kHoldMs = 300;
constexpr float kSlop = 0.02f;
constexpr int kMaxFingers = 10;

enum class Mode : uint8_t {
    Idle,      // no finger down
    Pending,   // one finger, not yet a tap, a hold or a drag
    Dragging,  // one finger that moved: the left button is down
    Held,      // one finger that stayed: its right click is given
    Aiming,    // one finger moving the pointer, to click where it lifts
    Panning,   // two fingers or more: the map follows them
};

struct Finger {
    SDL_FingerID id = 0;
    float x = 0.0f;   // window pixels
    float y = 0.0f;
};

struct State {
    bool enabled = false;
    bool aiming = false;
    Uint32 hold_event = 0;
    Mode mode = Mode::Idle;
    Finger fingers[kMaxFingers];
    int count = 0;
    SDL_FingerID primary = 0;
    float start_x = 0.0f;   // where the first finger went down
    float start_y = 0.0f;
    Uint32 serial = 0;      // which touch the hold timer is timing
    float pan_x = 0.0f;     // the fingers' middle, last seen
    float pan_y = 0.0f;
    float pan_dx = 0.0f;    // the drag waiting for TakePan, frame pixels
    float pan_dy = 0.0f;
    SDL_Haptic* haptic = nullptr;
    bool haptic_tried = false;
};
State g;

// Written by the system's thread (GameActivity), read by the game's.
std::atomic<bool> g_mouse{true};

void Click(Uint8 button, float x, float y) {
    Pointer::Move(x, y, true);
    Pointer::Click(button);
}

bool WindowSize(int& w, int& h) {
    Window* window = MainWindow();
    if (window == nullptr || window->GetSDLWindow() == nullptr) return false;
    SDL_GetWindowSize(window->GetSDLWindow(), &w, &h);
    return w > 0 && h > 0;
}

// Window pixels to one of the frame's, as the frame is scaled into the window.
float FrameScale() {
    Window* window = MainWindow();
    int w = 0, h = 0;
    if (!WindowSize(w, h) || window->GetWidth() <= 0 || window->GetHeight() <= 0) return 1.0f;
    return std::min(static_cast<float>(w) / window->GetWidth(),
                    static_cast<float>(h) / window->GetHeight());
}

Finger* Find(SDL_FingerID id) {
    for (int i = 0; i < g.count; ++i) {
        if (g.fingers[i].id == id) return &g.fingers[i];
    }
    return nullptr;
}

void Remove(SDL_FingerID id) {
    for (int i = 0; i < g.count; ++i) {
        if (g.fingers[i].id == id) {
            g.fingers[i] = g.fingers[--g.count];
            return;
        }
    }
}

void Middle(float& x, float& y) {
    x = y = 0.0f;
    for (int i = 0; i < g.count; ++i) {
        x += g.fingers[i].x;
        y += g.fingers[i].y;
    }
    if (g.count > 0) {
        x /= g.count;
        y /= g.count;
    }
}

// A short tick of the phone's vibrator, or the controller's, so a hold is
// felt to have clicked.
void Tick() {
    if (!g.haptic_tried) {
        g.haptic_tried = true;
        if (SDL_InitSubSystem(SDL_INIT_HAPTIC) == 0 && SDL_NumHaptics() > 0) {
            g.haptic = SDL_HapticOpen(0);
            if (g.haptic != nullptr && SDL_HapticRumbleInit(g.haptic) != 0) {
                SDL_HapticClose(g.haptic);
                g.haptic = nullptr;
            }
        }
    }
    if (g.haptic != nullptr) {
        SDL_HapticRumblePlay(g.haptic, 0.6f, 25);
    } else {
        Pad::Rumble();
    }
}

// The timer's own thread: it only posts, and the game's thread decides.
Uint32 HoldElapsed(Uint32, void* touch) {
    SDL_Event event{};
    event.type = g.hold_event;
    event.user.code = static_cast<Sint32>(reinterpret_cast<uintptr_t>(touch));
    SDL_PushEvent(&event);
    return 0;
}

void Down(SDL_FingerID id, float x, float y) {
    if (Find(id) == nullptr && g.count < kMaxFingers) g.fingers[g.count++] = Finger{id, x, y};
    if (g.count == 1) {
        g.primary = id;
        g.start_x = x;
        g.start_y = y;
        // The pointer goes to the finger, so what is under it shows as under
        // a mouse: the cursor, the help, a building waiting to be placed.
        Pointer::Move(x, y, true);
        if (g.aiming) {
            g.mode = Mode::Aiming;
            return;
        }
        g.mode = Mode::Pending;
        ++g.serial;
        SDL_AddTimer(kHoldMs, HoldElapsed, reinterpret_cast<void*>(static_cast<uintptr_t>(g.serial)));
        return;
    }
    // Another finger: whatever the first was doing gives way to moving the map.
    if (g.mode == Mode::Dragging) {
        const Finger* first = Find(g.primary);
        Pointer::Move(first != nullptr ? first->x : x, first != nullptr ? first->y : y, true);
        Pointer::Release(SDL_BUTTON_LEFT);
    }
    if (g.mode != Mode::Panning) {
        g.pan_dx = 0.0f;
        g.pan_dy = 0.0f;
    }
    g.mode = Mode::Panning;
    Middle(g.pan_x, g.pan_y);
}

void Motion(SDL_FingerID id, float x, float y, int window_h) {
    Finger* finger = Find(id);
    if (finger == nullptr) return;
    finger->x = x;
    finger->y = y;
    switch (g.mode) {
    case Mode::Pending:
        if (id != g.primary || std::hypot(x - g.start_x, y - g.start_y) <= kSlop * window_h) return;
        // It moved: a drag, pressed where the finger went down.
        g.mode = Mode::Dragging;
        Pointer::Move(g.start_x, g.start_y, true);
        Pointer::Press(SDL_BUTTON_LEFT);
        Pointer::Move(x, y, true);
        return;
    case Mode::Dragging:
    case Mode::Aiming:
        if (id == g.primary) Pointer::Move(x, y, true);
        return;
    case Mode::Panning: {
        float mx = 0.0f, my = 0.0f;
        Middle(mx, my);
        const float scale = FrameScale();
        g.pan_dx += (mx - g.pan_x) / scale;
        g.pan_dy += (my - g.pan_y) / scale;
        g.pan_x = mx;
        g.pan_y = my;
        return;
    }
    default:
        return;
    }
}

void Up(SDL_FingerID id, float x, float y) {
    const bool first = id == g.primary;
    Remove(id);
    switch (g.mode) {
    case Mode::Pending:
        // A tap: clicked where it went down, which a shaking finger may have
        // wandered a little from.
        if (first) Click(SDL_BUTTON_LEFT, g.start_x, g.start_y);
        break;
    case Mode::Dragging:
        if (first) {
            Pointer::Move(x, y, true);
            Pointer::Release(SDL_BUTTON_LEFT);
        }
        break;
    case Mode::Aiming:
        if (first) Click(SDL_BUTTON_LEFT, x, y);
        break;
    case Mode::Panning:
        // The fingers still down go on moving the map, from where they are.
        if (g.count > 0) {
            Middle(g.pan_x, g.pan_y);
            return;
        }
        break;
    default:
        break;
    }
    if (g.count == 0) g.mode = Mode::Idle;
}

} // namespace

void Enable() {
    g.enabled = true;
    g_mouse = false;   // until the system says one is plugged in
    g.hold_event = SDL_RegisterEvents(1);
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
    // Two taps on one unit land further apart than two clicks of a mouse.
    SDL_SetHint(SDL_HINT_MOUSE_DOUBLE_CLICK_RADIUS, "64");
}

bool Enabled() { return g.enabled; }

bool MouseAttached() { return g_mouse || !Pointer::ByFinger(); }

void SetAiming(bool aiming) { g.aiming = aiming; }

void TakePan(float& dx, float& dy) {
    dx = g.pan_dx;
    dy = g.pan_dy;
    g.pan_dx = 0.0f;
    g.pan_dy = 0.0f;
}

bool Handle(const SDL_Event& event) {
    if (!g.enabled) return false;
    if (g.hold_event != 0 && g.hold_event != static_cast<Uint32>(-1) && event.type == g.hold_event) {
        if (g.mode == Mode::Pending && static_cast<Uint32>(event.user.code) == g.serial) {
            g.mode = Mode::Held;
            Click(SDL_BUTTON_RIGHT, g.start_x, g.start_y);
            Tick();
        }
        return true;
    }
    if (event.type != SDL_FINGERDOWN && event.type != SDL_FINGERMOTION && event.type != SDL_FINGERUP) {
        return false;
    }
    // The screen itself, not a touchpad, nor a mouse's own made-up fingers.
    if (event.tfinger.touchId == SDL_MOUSE_TOUCHID ||
        SDL_GetTouchDeviceType(event.tfinger.touchId) != SDL_TOUCH_DEVICE_DIRECT) {
        return false;
    }
    int w = 0, h = 0;
    if (!WindowSize(w, h)) return true;
    const float x = event.tfinger.x * w;
    const float y = event.tfinger.y * h;
    switch (event.type) {
    case SDL_FINGERDOWN:
        Down(event.tfinger.fingerId, x, y);
        break;
    case SDL_FINGERMOTION:
        Motion(event.tfinger.fingerId, x, y, h);
        break;
    default:
        Up(event.tfinger.fingerId, x, y);
        break;
    }
    return true;
}

} // namespace HAL::Touch

#ifdef __ANDROID__
// GameActivity, whenever a device comes or goes: whether one of them is a mouse.
extern "C" JNIEXPORT void JNICALL Java_org_openaok_game_GameActivity_nativeMouseAttached(
    JNIEnv*, jclass, jboolean attached) {
    HAL::Touch::g_mouse = attached == JNI_TRUE;
    __android_log_print(ANDROID_LOG_INFO, "OpenAoK", "[Touch] mouse %s",
                        attached == JNI_TRUE ? "plugged in" : "not plugged in");
}
#endif
