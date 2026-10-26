#pragma once
#include <SDL2/SDL.h>
#include <string>
#include <string_view>
#include <vector>

#include "screens/Widgets.h"

// The scenario editor's controls: text buttons, labels, radio buttons and
// check boxes (the btn5 art's frames 4 and 6, 0 and 2), drop-down lists, text
// fields, and lists with their scroll bars, all in the screen's font 10 and
// its theme's colours.
//
// Immediate mode: a panel is one function that names each control where it
// stands, run once to draw and once per event to hit-test, so a control is
// always where it is drawn. Plain data and free functions, as screens/Widgets.
namespace Editor {

struct Ui {
    // Drawing when `r` is set; otherwise handling `event`.
    SDL_Renderer* r = nullptr;
    const SDL_Event* event = nullptr;
    bool taken = false;          // the event was a control's
    int mx = -1, my = -1;
    bool held = false;
    const Screens::Theme* theme = nullptr;
    int font = 0;
    int screen_h = 600;

    // What persists between frames: the drop-down that is open, with what it
    // offered as last drawn; the text field with the caret; a list's scroll.
    const void* open_drop = nullptr;
    std::vector<std::string> open_items;
    SDL_Rect open_rect{0, 0, 0, 0};
    int open_scroll = 0;
    const void* focus = nullptr;
    // A choice made in the open drop-down, for its Drop to take up in the
    // event pass that follows.
    const void* pending_key = nullptr;
    int pending_choice = -1;
    // The help string of the control under the pointer, as last drawn.
    int hover_help = -1;
};

bool Drawing(const Ui& ui);
// A left click on `rect` in this event.
bool Clicked(Ui& ui, const SDL_Rect& rect);

void Label(Ui& ui, const SDL_Rect& rect, std::string_view text, bool centred = false);
// A text button; true when clicked. `down` draws it pressed in (a tab, a
// mode button held down).
bool Button(Ui& ui, const SDL_Rect& rect, std::string_view text, bool enabled = true,
            bool down = false);
// A radio button: the art's empty face, or its marked one when `on`. True
// when clicked (the caller sets what it stands for).
bool Radio(Ui& ui, const SDL_Rect& rect, bool on, bool enabled = true);
// A check box; flips `on` when clicked, and says so.
bool Check(Ui& ui, const SDL_Rect& rect, bool& on, bool enabled = true);
// A drop-down list; true when the choice changed.
bool Drop(Ui& ui, const SDL_Rect& rect, const std::vector<std::string>& items, int& selected,
          bool enabled = true);
// A one-line text field; true when its text changed. `digits` takes only a
// number, `max` characters at most.
bool Field(Ui& ui, const SDL_Rect& rect, std::string& text, size_t max, bool digits = false);
// Several lines of text, wrapped, Enter starting a new one.
bool TextBox(Ui& ui, const SDL_Rect& rect, std::string& text, size_t max);
// A list of lines to pick from, with its scroll bar when it overflows; true
// when the choice changed.
bool List(Ui& ui, const SDL_Rect& rect, const std::vector<std::string>& items, int& selected,
          int& scroll);

// A control's help (LANGUAGE.DLL), shown while the pointer is over `rect`.
void Help(Ui& ui, const SDL_Rect& rect, int string_id);

// The open drop-down's choices, over everything: drawn last, clicked first.
void DrawOpenDrop(Ui& ui);
// A click while a drop-down is open; true when it was the drop-down's.
bool ClickOpenDrop(Ui& ui, int x, int y, int* chosen);

// The row height of a list in the screen's font.
int RowHeight(const Ui& ui);

} // namespace Editor
