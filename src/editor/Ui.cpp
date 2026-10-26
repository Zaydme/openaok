#include "editor/Ui.h"

#include <algorithm>

#include "hal/GraphicCache.h"
#include "rendering/Text.h"

namespace Editor {

namespace Text = Rendering::Text;
using Screens::Hit;

namespace {

// The btn5 art's faces (interfac.drs 50604, the editor's button file): a
// check box is 0 empty and 2 ticked, a radio button 4 empty and 6 crossed.
constexpr int kCheckOff = 0;
constexpr int kCheckOn = 2;
constexpr int kRadioOff = 4;
constexpr int kRadioOn = 6;

// A text panel's inset from its left edge.
constexpr int kLabelInset = 5;

bool LeftDown(const Ui& ui) {
    return ui.event != nullptr && ui.event->type == SDL_MOUSEBUTTONDOWN &&
           ui.event->button.button == SDL_BUTTON_LEFT;
}

void Face(Ui& ui, const SDL_Rect& rect, int frame) {
    if (ui.theme == nullptr || ui.theme->style.button_art == 0) return;
    const HAL::GraphicAtlas* atlas = HAL::GraphicCache::Get().GetInterfaceAtlas(
        ui.theme->style.button_art, ui.theme->style.palette);
    if (atlas == nullptr || atlas->base == nullptr || frame >= static_cast<int>(atlas->frames.size())) {
        return;
    }
    // The face centred in the control and cut to it: the art is 39 square,
    // the controls 20 to 26.
    const HAL::GraphicFrame& f = atlas->frames[static_cast<size_t>(frame)];
    SDL_Rect src = f.rect;
    SDL_Rect dst = {rect.x + (rect.w - f.rect.w) / 2, rect.y + (rect.h - f.rect.h) / 2, f.rect.w,
                    f.rect.h};
    if (dst.x < rect.x) { src.x += rect.x - dst.x; src.w -= rect.x - dst.x; dst.w = src.w; dst.x = rect.x; }
    if (dst.y < rect.y) { src.y += rect.y - dst.y; src.h -= rect.y - dst.y; dst.h = src.h; dst.y = rect.y; }
    src.w = std::min(src.w, rect.w);
    src.h = std::min(src.h, rect.h);
    dst.w = src.w;
    dst.h = src.h;
    SDL_RenderCopy(ui.r, atlas->base, &src, &dst);
}

} // namespace

bool Drawing(const Ui& ui) { return ui.r != nullptr; }

void Help(Ui& ui, const SDL_Rect& rect, int string_id) {
    if (Drawing(ui) && Hit(rect, ui.mx, ui.my)) ui.hover_help = string_id;
}

bool Clicked(Ui& ui, const SDL_Rect& rect) {
    if (ui.taken || !LeftDown(ui)) return false;
    if (!Hit(rect, ui.event->button.x, ui.event->button.y)) return false;
    ui.taken = true;
    return true;
}

int RowHeight(const Ui& ui) { return Text::Height(ui.font) + 2; }

void Label(Ui& ui, const SDL_Rect& rect, std::string_view text, bool centred) {
    if (!Drawing(ui) || text.empty()) return;
    const SDL_Color c = ui.theme->text;
    if (centred) {
        Screens::DrawTextIn(ui.r, ui.font, rect, text, c, ui.theme->text_shadow);
    } else {
        // Top-aligned, a text panel's default.
        Text::DrawShadowed(ui.r, ui.font, rect.x + kLabelInset, rect.y, text, c, ui.theme->text_shadow);
    }
}

bool Button(Ui& ui, const SDL_Rect& rect, std::string_view text, bool enabled, bool down) {
    if (Drawing(ui)) {
        Screens::Button b;
        b.rect = rect;
        b.label = std::string(text);
        b.font = ui.font;
        b.enabled = enabled;
        Screens::State s = Screens::StateOf(b, ui.mx, ui.my, ui.held);
        if (down && s != Screens::State::Disabled) s = Screens::State::Pressed;
        Screens::DrawButton(ui.r, *ui.theme, b, s);
        return false;
    }
    return enabled && Clicked(ui, rect);
}

bool Radio(Ui& ui, const SDL_Rect& rect, bool on, bool enabled) {
    if (Drawing(ui)) {
        Screens::DrawShaded(ui.r, *ui.theme, rect);
        Face(ui, rect, on ? kRadioOn : kRadioOff);
        if (enabled) Screens::DrawBevel(ui.r, *ui.theme, rect, on);
        else Screens::DrawEtched(ui.r, *ui.theme, rect);
        return false;
    }
    return enabled && Clicked(ui, rect);
}

bool Check(Ui& ui, const SDL_Rect& rect, bool& on, bool enabled) {
    if (Drawing(ui)) {
        Screens::DrawShaded(ui.r, *ui.theme, rect);
        Face(ui, rect, on ? kCheckOn : kCheckOff);
        if (enabled) Screens::DrawBevel(ui.r, *ui.theme, rect, on);
        else Screens::DrawEtched(ui.r, *ui.theme, rect);
        return false;
    }
    if (enabled && Clicked(ui, rect)) {
        on = !on;
        return true;
    }
    return false;
}

bool Drop(Ui& ui, const SDL_Rect& rect, const std::vector<std::string>& items, int& selected,
          bool enabled) {
    const void* key = &selected;
    if (Drawing(ui)) {
        // The choice in a shaded field, and the button beside it with the
        // art's down arrow.
        const SDL_Rect field = {rect.x, rect.y, rect.w - Screens::kDropDownArrowW, rect.h};
        const SDL_Rect arrow = {rect.x + rect.w - Screens::kDropDownArrowW, rect.y, Screens::kDropDownArrowW, rect.h};
        Screens::DrawShaded(ui.r, *ui.theme, field);
        if (selected >= 0 && selected < static_cast<int>(items.size())) {
            const int y = field.y + (field.h - Text::Height(ui.font)) / 2;
            if (enabled) {
                Text::DrawShadowed(ui.r, ui.font, field.x + 6, y, items[static_cast<size_t>(selected)],
                                   ui.theme->text, ui.theme->text_shadow);
            } else {
                Text::Draw(ui.r, ui.font, field.x + 6, y, items[static_cast<size_t>(selected)], ui.theme->disabled);
            }
        }
        const bool pressed = ui.held && Hit(arrow, ui.mx, ui.my);
        Screens::DrawShaded(ui.r, *ui.theme, arrow);
        Face(ui, arrow, 10 + (pressed ? 1 : 0));
        if (enabled) Screens::DrawBevel(ui.r, *ui.theme, arrow, pressed);
        else Screens::DrawEtched(ui.r, *ui.theme, arrow);
        if (ui.open_drop == key) {
            if (ui.open_items.size() != items.size()) ui.open_items = items;
            ui.open_rect = rect;
        }
        return false;
    }
    if (ui.pending_key == key) {
        ui.pending_key = nullptr;
        if (ui.pending_choice >= 0 && ui.pending_choice < static_cast<int>(items.size()) &&
            ui.pending_choice != selected) {
            selected = ui.pending_choice;
            return true;
        }
        return false;
    }
    if (enabled && Clicked(ui, rect)) {
        ui.open_drop = key;
        ui.open_items = items;
        ui.open_rect = rect;
        ui.open_scroll = std::max(0, std::min(selected, static_cast<int>(items.size()) -
                                                            Screens::kDropDownVisible));
        ui.focus = nullptr;
    }
    return false;
}

void DrawOpenDrop(Ui& ui) {
    if (ui.open_drop == nullptr || !Drawing(ui)) return;
    Screens::DropDown d;
    d.rect = ui.open_rect;
    d.items = ui.open_items;
    d.selected = -1;
    d.open = true;
    d.scroll = ui.open_scroll;
    d.font = ui.font;
    // The field itself was drawn with its panel; this is the list below it.
    const int rows = std::min(Screens::kDropDownVisible, static_cast<int>(d.items.size()));
    for (int i = d.scroll; i < d.scroll + rows && i < static_cast<int>(d.items.size()); ++i) {
        const SDL_Rect item = Screens::DropDownItemRect(d, i, ui.screen_h);
        SDL_SetRenderDrawColor(ui.r, ui.theme->bevel[5].r, ui.theme->bevel[5].g, ui.theme->bevel[5].b, 255);
        SDL_RenderFillRect(ui.r, &item);
        const bool hover = Hit(item, ui.mx, ui.my);
        Screens::DrawText(ui.r, ui.font, item.x + 6, item.y + (item.h - Text::Height(ui.font)) / 2,
                          d.items[static_cast<size_t>(i)], hover ? ui.theme->focus : ui.theme->text,
                          ui.theme->text_shadow);
    }
    if (rows > 0) {
        const SDL_Rect first = Screens::DropDownItemRect(d, d.scroll, ui.screen_h);
        Screens::DrawBevel(ui.r, *ui.theme, SDL_Rect{first.x, first.y, first.w, rows * d.rect.h}, false);
    }
    // More than fit: the arrow keys' and the wheel's to scroll.
}

bool ClickOpenDrop(Ui& ui, int x, int y, int* chosen) {
    if (ui.open_drop == nullptr) return false;
    Screens::DropDown d;
    d.rect = ui.open_rect;
    d.items = ui.open_items;
    d.scroll = ui.open_scroll;
    const int rows = std::min(Screens::kDropDownVisible, static_cast<int>(d.items.size()));
    for (int i = d.scroll; i < d.scroll + rows && i < static_cast<int>(d.items.size()); ++i) {
        if (Hit(Screens::DropDownItemRect(d, i, ui.screen_h), x, y)) {
            ui.pending_key = ui.open_drop;
            ui.pending_choice = i;
            if (chosen != nullptr) *chosen = i;
            ui.open_drop = nullptr;
            return true;
        }
    }
    ui.open_drop = nullptr;
    return true;   // a click anywhere else only closes it
}

bool Field(Ui& ui, const SDL_Rect& rect, std::string& text, size_t max, bool digits) {
    const void* key = &text;
    if (Drawing(ui)) {
        Screens::TextField f;
        f.rect = rect;
        f.text = text;
        f.focused = ui.focus == key;
        f.font = ui.font;
        Screens::DrawTextField(ui.r, *ui.theme, f);
        return false;
    }
    if (Clicked(ui, rect)) {
        ui.focus = key;
        return false;
    }
    if (ui.focus != key || ui.event == nullptr || ui.taken) return false;
    const SDL_Event& e = *ui.event;
    if (e.type == SDL_TEXTINPUT) {
        bool changed = false;
        for (const char* c = e.text.text; *c != '\0'; ++c) {
            if (digits && !((*c >= '0' && *c <= '9') || (*c == '-' && text.empty()))) continue;
            if (text.size() >= max) break;
            text += *c;
            changed = true;
        }
        ui.taken = true;
        return changed;
    }
    if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_BACKSPACE) {
        ui.taken = true;
        if (text.empty()) return false;
        while (!text.empty()) {
            const auto last = static_cast<unsigned char>(text.back());
            text.pop_back();
            if ((last & 0xC0) != 0x80) break;
        }
        return true;
    }
    if (e.type == SDL_KEYDOWN &&
        (e.key.keysym.sym == SDLK_RETURN || e.key.keysym.sym == SDLK_KP_ENTER || e.key.keysym.sym == SDLK_TAB)) {
        ui.focus = nullptr;
        ui.taken = true;
    }
    return false;
}

bool TextBox(Ui& ui, const SDL_Rect& rect, std::string& text, size_t max) {
    const void* key = &text;
    if (Drawing(ui)) {
        Screens::DrawShaded(ui.r, *ui.theme, rect);
        Screens::DrawBevel(ui.r, *ui.theme, rect, true);
        std::vector<std::string_view> lines;
        Text::Wrap(ui.font, text, std::max(10, rect.w - 12), lines);
        const int line_h = Text::Height(ui.font);
        const int rows = std::max(1, (rect.h - 6) / std::max(1, line_h));
        // The end in view, where the typing is.
        const int first = std::max(0, static_cast<int>(lines.size()) - rows);
        int y = rect.y + 3;
        for (int i = first; i < static_cast<int>(lines.size()); ++i) {
            Text::DrawShadowed(ui.r, ui.font, rect.x + 6, y, lines[static_cast<size_t>(i)],
                               ui.theme->text, ui.theme->text_shadow);
            y += line_h;
        }
        if (ui.focus == key && (SDL_GetTicks() / 500) % 2 == 0) {
            const std::string_view last = lines.empty() ? std::string_view{} : lines.back();
            const int cx = rect.x + 6 + Text::Width(ui.font, last) + 1;
            const int cy = rect.y + 3 + std::min(rows - 1, static_cast<int>(lines.size()) - 1 - first) * line_h;
            SDL_SetRenderDrawColor(ui.r, ui.theme->text.r, ui.theme->text.g, ui.theme->text.b, 255);
            SDL_Rect caret = {cx, std::max(rect.y + 3, cy) + 1, 1, line_h - 2};
            SDL_RenderFillRect(ui.r, &caret);
        }
        return false;
    }
    if (Clicked(ui, rect)) {
        ui.focus = key;
        return false;
    }
    if (ui.focus != key || ui.event == nullptr || ui.taken) return false;
    const SDL_Event& e = *ui.event;
    if (e.type == SDL_TEXTINPUT) {
        for (const char* c = e.text.text; *c != '\0' && text.size() < max; ++c) text += *c;
        ui.taken = true;
        return true;
    }
    if (e.type == SDL_KEYDOWN) {
        if (e.key.keysym.sym == SDLK_BACKSPACE) {
            ui.taken = true;
            if (text.empty()) return false;
            while (!text.empty()) {
                const auto last = static_cast<unsigned char>(text.back());
                text.pop_back();
                if ((last & 0xC0) != 0x80) break;
            }
            return true;
        }
        if ((e.key.keysym.sym == SDLK_RETURN || e.key.keysym.sym == SDLK_KP_ENTER) && text.size() < max) {
            text += '\n';
            ui.taken = true;
            return true;
        }
    }
    return false;
}

// A list: a field sunk into the screen, one line per row of the font's
// height, and down its right side, when the lines overflow, a 20-wide scroll
// bar: up and down buttons on the art's arrow faces, the thumb between.
namespace {
constexpr int kBarW = 20;
constexpr int kArtUp = 8;
constexpr int kArtDown = 10;

int ListRows(const Ui& ui, const SDL_Rect& rect) {
    return std::max(1, (rect.h - 4) / std::max(1, Text::GdiHeight(ui.font)));
}

struct Bar {
    bool shown = false;
    SDL_Rect up, down, track, thumb;
};

Bar BarOf(const Ui& ui, const SDL_Rect& rect, int count, int scroll) {
    Bar b;
    const int rows = ListRows(ui, rect);
    if (count <= rows) return b;
    b.shown = true;
    const int x = rect.x + rect.w - kBarW;
    b.up = SDL_Rect{x, rect.y, kBarW, kBarW};
    b.down = SDL_Rect{x, rect.y + rect.h - kBarW, kBarW, kBarW};
    b.track = SDL_Rect{x, rect.y + kBarW, kBarW, std::max(0, rect.h - 2 * kBarW)};
    const int thumb_h = std::max(8, b.track.h * rows / count);
    const int most = count - rows;
    const int y = b.track.y + (b.track.h - thumb_h) * std::clamp(scroll, 0, most) / std::max(1, most);
    b.thumb = SDL_Rect{x, y, kBarW, thumb_h};
    return b;
}
} // namespace

bool List(Ui& ui, const SDL_Rect& rect, const std::vector<std::string>& items, int& selected,
          int& scroll) {
    const int count = static_cast<int>(items.size());
    const int rows = ListRows(ui, rect);
    const int most = std::max(0, count - rows);
    scroll = std::clamp(scroll, 0, most);
    const Bar bar = BarOf(ui, rect, count, scroll);
    const SDL_Rect field = {rect.x, rect.y, rect.w - (bar.shown ? kBarW : 0), rect.h};
    const int line_h = Text::GdiHeight(ui.font);
    if (Drawing(ui)) {
        Screens::DrawShaded(ui.r, *ui.theme, field);
        Screens::DrawBevel(ui.r, *ui.theme, field, true);
        for (int i = 0; i < rows && scroll + i < count; ++i) {
            const int index = scroll + i;
            const SDL_Rect row = {field.x + 3, field.y + 2 + i * line_h, field.w - 6, line_h};
            if (index == selected) {
                const SDL_Color c = ui.theme->bevel[5];
                SDL_SetRenderDrawColor(ui.r, c.r, c.g, c.b, 255);
                SDL_RenderFillRect(ui.r, &row);
            }
            Text::DrawShadowed(ui.r, ui.font, row.x + 2, row.y, items[static_cast<size_t>(index)],
                               index == selected ? ui.theme->state
                               : Hit(row, ui.mx, ui.my) ? ui.theme->focus : ui.theme->text,
                               ui.theme->text_shadow);
        }
        if (bar.shown) {
            for (const SDL_Rect* b : {&bar.up, &bar.down}) {
                const bool pressed = ui.held && Hit(*b, ui.mx, ui.my);
                Screens::DrawShaded(ui.r, *ui.theme, *b);
                Face(ui, *b, (b == &bar.up ? kArtUp : kArtDown) + (pressed ? 1 : 0));
                Screens::DrawBevel(ui.r, *ui.theme, *b, pressed);
            }
            Screens::DrawShaded(ui.r, *ui.theme, bar.track);
            Screens::DrawBevel(ui.r, *ui.theme, bar.track, true);
            Screens::DrawShaded(ui.r, *ui.theme, bar.thumb);
            Screens::DrawBevel(ui.r, *ui.theme, bar.thumb, false);
        }
        return false;
    }
    if (ui.taken || ui.event == nullptr) return false;
    if (ui.event->type == SDL_MOUSEWHEEL) {
        // The wheel scrolls a list under the mouse.
        if (Hit(rect, ui.mx, ui.my)) {
            scroll = std::clamp(scroll - ui.event->wheel.y * 3, 0, most);
            ui.taken = true;
        }
        return false;
    }
    if (!LeftDown(ui) || !Hit(rect, ui.event->button.x, ui.event->button.y)) return false;
    ui.taken = true;
    const int x = ui.event->button.x, y = ui.event->button.y;
    if (bar.shown && x >= field.x + field.w) {
        if (Hit(bar.up, x, y)) scroll = std::max(0, scroll - 1);
        else if (Hit(bar.down, x, y)) scroll = std::min(most, scroll + 1);
        else if (y < bar.thumb.y) scroll = std::max(0, scroll - rows);
        else if (y >= bar.thumb.y + bar.thumb.h) scroll = std::min(most, scroll + rows);
        return false;
    }
    const int index = scroll + (y - field.y - 2) / std::max(1, line_h);
    if (index < 0 || index >= count || index == selected) return false;
    selected = index;
    return true;
}

} // namespace Editor
