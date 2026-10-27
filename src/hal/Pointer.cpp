#include "hal/Pointer.h"

#ifdef __ANDROID__
#include <jni.h>
// SDL's own way in for a mouse on Android (SDL_android.c): the calls a mouse
// plugged into the phone makes. The JNI arguments go unused.
extern "C" JNIEXPORT void JNICALL Java_org_libsdl_app_SDLActivity_onNativeMouse(
    JNIEnv* env, jclass cls, jint button, jint action, jfloat x, jfloat y, jboolean relative);
#endif

#ifdef __SWITCH__
#include "hal/Window.h"
// SDL's own way in for a mouse (events/SDL_mouse_c.h), which its Switch port
// uses for a USB mouse. SDL is linked in whole on the Switch, so these are
// within reach. The mouse's id is SDL's SDL_MouseID.
extern "C" int SDL_SendMouseMotion(SDL_Window* window, Uint32 mouse, int relative, int x, int y);
extern "C" int SDL_SendMouseButton(SDL_Window* window, Uint32 mouse, Uint8 state, Uint8 button);
#endif

namespace HAL::Pointer {

namespace {

struct State {
    float x = 0.0f;
    float y = 0.0f;
    bool by_finger = true;
    int held = 0;   // the buttons down, as Android's MotionEvent numbers them
};
State g;

#ifdef __ANDROID__
// Android's actions and buttons (MotionEvent).
constexpr int kDown = 0;
constexpr int kUp = 1;
constexpr int kMove = 2;

int AndroidButton(Uint8 button) {
    return button == SDL_BUTTON_RIGHT ? 2 : 1;   // BUTTON_SECONDARY, BUTTON_PRIMARY
}
#endif

#ifdef __SWITCH__
SDL_Window* Target() {
    Window* window = MainWindow();
    return window != nullptr ? window->GetSDLWindow() : nullptr;
}
#endif

} // namespace

void Move(float x, float y, bool by_finger) {
    g.x = x;
    g.y = y;
    g.by_finger = by_finger;
#if defined(__ANDROID__)
    Java_org_libsdl_app_SDLActivity_onNativeMouse(nullptr, nullptr, g.held, kMove, x, y, JNI_FALSE);
#elif defined(__SWITCH__)
    if (SDL_Window* window = Target()) {
        SDL_SendMouseMotion(window, 0, 0, static_cast<int>(x), static_cast<int>(y));
    }
#endif
}

void Press(Uint8 button) {
#if defined(__ANDROID__)
    g.held |= AndroidButton(button);
    Java_org_libsdl_app_SDLActivity_onNativeMouse(nullptr, nullptr, g.held, kDown, g.x, g.y, JNI_FALSE);
#elif defined(__SWITCH__)
    if (SDL_Window* window = Target()) SDL_SendMouseButton(window, 0, SDL_PRESSED, button);
#else
    (void)button;
#endif
}

void Release(Uint8 button) {
#if defined(__ANDROID__)
    g.held &= ~AndroidButton(button);
    Java_org_libsdl_app_SDLActivity_onNativeMouse(nullptr, nullptr, g.held, kUp, g.x, g.y, JNI_FALSE);
#elif defined(__SWITCH__)
    if (SDL_Window* window = Target()) SDL_SendMouseButton(window, 0, SDL_RELEASED, button);
#else
    (void)button;
#endif
}

void Click(Uint8 button) {
    Press(button);
    Release(button);
}

void Position(float& x, float& y) {
    x = g.x;
    y = g.y;
}

bool ByFinger() { return g.by_finger; }

} // namespace HAL::Pointer
