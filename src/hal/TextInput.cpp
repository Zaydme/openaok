#include "hal/TextInput.h"
#include "hal/Window.h"

#include <SDL2/SDL.h>

#ifdef __SWITCH__
#include <switch.h>
// SDL's own way in for keys and typed text (events/SDL_keyboard_c.h), which
// its Switch port uses for a USB keyboard. SDL is linked in whole on the
// Switch.
extern "C" int SDL_SendKeyboardKey(Uint8 state, SDL_Scancode scancode);
extern "C" int SDL_SendKeyboardText(const char* text);
#endif

namespace HAL::TextInput {

namespace {

struct State {
    Kind wanted = Kind::None;
    bool asked = false;        // the keyboard has been up for this line
    bool fields_due = false;   // the player asked for the keyboard
    bool text_on = false;      // text input is on for what the keyboard gave
};
State g;

#ifdef __SWITCH__
constexpr int kMaxText = 255;

// The system's keyboard, until the player is done with it: true when they
// pressed OK, with what they typed.
bool Ask(char (&text)[kMaxText + 1]) {
    text[0] = '\0';
    SwkbdConfig keyboard;
    if (R_FAILED(swkbdCreate(&keyboard, 0))) return false;
    swkbdConfigMakePresetDefault(&keyboard);
    swkbdConfigSetStringLenMax(&keyboard, kMaxText);
    const Result shown = swkbdShow(&keyboard, text, sizeof text);
    swkbdClose(&keyboard);
    return R_SUCCEEDED(shown);
}

void Tap(SDL_Scancode key) {
    SDL_SendKeyboardKey(SDL_PRESSED, key);
    SDL_SendKeyboardKey(SDL_RELEASED, key);
}

// The keyboard, and what it gave as though typed; a line is finished with
// Enter, or with Escape when the player backed out.
void Type(bool line) {
    char text[kMaxText + 1];
    const bool ok = Ask(text);
    if (ok && text[0] != '\0') {
        // Text input is on just long enough for the characters to be read:
        // while it is, SDL's Switch port leaves the controller unread.
        SDL_EventState(SDL_TEXTINPUT, SDL_ENABLE);
        g.text_on = true;
        SDL_SendKeyboardText(text);
    }
    if (line) Tap(ok ? SDL_SCANCODE_RETURN : SDL_SCANCODE_ESCAPE);
}
#endif

} // namespace

void Want(Kind kind) {
#ifdef __SWITCH__
    if (kind == g.wanted) return;
    g.wanted = kind;
    g.asked = false;
    g.fields_due = false;
#else
    g.wanted = kind;
    const bool on = kind != Kind::None;
    if (on != (SDL_IsTextInputActive() == SDL_TRUE)) {
        if (on) {
            SDL_StartTextInput();
        } else {
            SDL_StopTextInput();
        }
    }
#endif
}

void At(const SDL_Rect& field) {
    static SDL_Rect last{0, 0, 0, 0};
    const Window* window = MainWindow();
    if (window == nullptr) return;
    SDL_Rect at = window->ToWindow(field);
    if (at.x == last.x && at.y == last.y && at.w == last.w && at.h == last.h) return;
    last = at;
    SDL_SetTextInputRect(&at);
#ifdef __ANDROID__
    // The keyboard takes the place only as it comes up: up already, it is
    // brought up again for the new field.
    if (SDL_IsTextInputActive() == SDL_TRUE) {
        SDL_StopTextInput();
        SDL_StartTextInput();
    }
#endif
}

bool ShowForFields() {
#ifdef __SWITCH__
    if (g.wanted != Kind::Fields) return false;
    g.fields_due = true;
    return true;
#else
    return false;
#endif
}

void Update() {
#ifdef __SWITCH__
    // What the keyboard gave last frame has been read by now.
    if (g.text_on) {
        SDL_EventState(SDL_TEXTINPUT, SDL_DISABLE);
        g.text_on = false;
    }
    if (g.wanted == Kind::Line && !g.asked) {
        g.asked = true;
        Type(true);
    } else if (g.fields_due) {
        g.fields_due = false;
        Type(false);
    }
#endif
}

} // namespace HAL::TextInput
