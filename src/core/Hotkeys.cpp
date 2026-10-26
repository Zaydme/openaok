#include "core/Hotkeys.h"

#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <vector>

namespace Core::Hotkeys {

namespace {

struct Binding {
    int group = -1;
    int item = -1;
    int key = 0;          // Windows virtual-key code, or a MouseKey
    bool ctrl = false;
    bool alt = false;
    bool shift = false;
    int name_string = -1;
};

std::vector<Binding>& Table() {
    static std::vector<Binding> table;
    return table;
}

// The SDL key a Windows virtual-key code names. Letters and digits are their
// own codes; the rest are the handful the defaults use.
SDL_Keycode KeycodeOf(int vk) {
    if (vk >= 'A' && vk <= 'Z') return SDLK_a + (vk - 'A');
    if (vk >= '0' && vk <= '9') return SDLK_0 + (vk - '0');
    if (vk >= 0x70 && vk <= 0x7B) return SDLK_F1 + (vk - 0x70);
    switch (vk) {
    case 0x0D: return SDLK_RETURN;
    case 0x20: return SDLK_SPACE;
    case 0x21: return SDLK_PAGEUP;
    case 0x22: return SDLK_PAGEDOWN;
    case 0x24: return SDLK_HOME;
    case 0x25: return SDLK_LEFT;
    case 0x26: return SDLK_UP;
    case 0x27: return SDLK_RIGHT;
    case 0x28: return SDLK_DOWN;
    case 0x2E: return SDLK_DELETE;
    case 0x6B: return SDLK_KP_PLUS;
    case 0x6D: return SDLK_KP_MINUS;
    case 0xBC: return SDLK_COMMA;
    case 0xBE: return SDLK_PERIOD;
    case 0xBF: return SDLK_SLASH;
    default: return SDLK_UNKNOWN;
    }
}

bool Matches(const Binding& b, SDL_Keycode sym, Uint16 mod) {
    if (b.key >= kWheelDown) return false;
    SDL_Keycode want = KeycodeOf(b.key);
    if (want == SDLK_UNKNOWN) return false;
    // Enter on the number pad is Enter.
    if (sym == SDLK_KP_ENTER) sym = SDLK_RETURN;
    if (sym != want) return false;
    if (CtrlHeld(mod) != b.ctrl) return false;
    if (((mod & KMOD_ALT) != 0) != b.alt) return false;
    if (b.shift && (mod & KMOD_SHIFT) == 0) return false;
    return true;
}

} // namespace

bool Load(const std::string& path) {
    std::ifstream in(path);
    if (!in.is_open()) {
        std::cerr << "[Hotkeys] Failed to open " << path << std::endl;
        return false;
    }
    nlohmann::json data;
    try {
        in >> data;
    } catch (...) {
        return false;
    }
    auto& table = Table();
    table.clear();
    for (const auto& d : data["defaults"]) {
        Binding b;
        b.group = d.value("group", -1);
        b.item = d.value("item", -1);
        b.key = d.value("key", 0);
        b.ctrl = d.value("ctrl", false);
        b.alt = d.value("alt", false);
        b.shift = d.value("shift", false);
        b.name_string = d.value("string", -1);
        table.push_back(b);
    }
    return !table.empty();
}

bool Is(const SDL_KeyboardEvent& key, int group, int item) {
    for (const auto& b : Table()) {
        if (b.group == group && b.item == item && Matches(b, key.keysym.sym, key.keysym.mod)) {
            return true;
        }
    }
    return false;
}

bool IsMouse(int mouse_key, int group, int item) {
    for (const auto& b : Table()) {
        if (b.group == group && b.item == item && b.key == mouse_key) return true;
    }
    return false;
}

int ItemOf(const SDL_KeyboardEvent& key, int group) {
    for (const auto& b : Table()) {
        if (b.group == group && Matches(b, key.keysym.sym, key.keysym.mod)) return b.item;
    }
    return -1;
}

int NameString(int group, int item) {
    for (const auto& b : Table()) {
        if (b.group == group && b.item == item) return b.name_string;
    }
    return -1;
}

int Items(int group, int* out, int max_out) {
    int n = 0;
    for (const auto& b : Table()) {
        if (b.group != group || n >= max_out) continue;
        bool seen = false;
        for (int i = 0; i < n; ++i) seen = seen || out[i] == b.item;
        if (!seen) out[n++] = b.item;
    }
    return n;
}

bool CtrlHeld(Uint16 mod) {
    return (mod & (KMOD_CTRL | KMOD_GUI)) != 0;
}

} // namespace Core::Hotkeys
