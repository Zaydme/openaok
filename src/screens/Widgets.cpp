#include "Widgets.h"

#include "core/Profile.h"
#include "hal/GraphicCache.h"
#include "hal/TextInput.h"
#include "rendering/Text.h"

#include <algorithm>

namespace Screens {

namespace Text = Rendering::Text;

namespace {

// The nearest colour of a palette, as a 256-colour screen would show it.
SDL_Color Snap(const std::vector<HAL::PaletteColor>& palette, SDL_Color c) {
    if (palette.empty()) return c;
    int best = 0;
    int best_d = 1 << 30;
    for (size_t i = 0; i < palette.size(); ++i) {
        const int dr = palette[i].r - c.r, dg = palette[i].g - c.g, db = palette[i].b - c.b;
        const int d = dr * dr + dg * dg + db * db;
        if (d < best_d) { best_d = d; best = static_cast<int>(i); }
    }
    return SDL_Color{palette[best].r, palette[best].g, palette[best].b, 255};
}

SDL_Color Entry(const std::vector<HAL::PaletteColor>& palette, int index) {
    if (index < 0 || index >= static_cast<int>(palette.size())) return SDL_Color{0, 0, 0, 255};
    return SDL_Color{palette[index].r, palette[index].g, palette[index].b, 255};
}

void Line(SDL_Renderer* r, int x, int y, int w, int h, SDL_Color c) {
    if (w <= 0 || h <= 0) return;
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
    const SDL_Rect rect = {x, y, w, h};
    SDL_RenderFillRect(r, &rect);
}

// The background art's frame, drawn in a palette, a piece of it into `dst`.
void BlitBackground(SDL_Renderer* r, const Theme& theme, uint32_t palette_id,
                    const SDL_Rect& dst) {
    if (theme.background == 0) return;
    const HAL::GraphicAtlas* atlas =
        HAL::GraphicCache::Get().GetInterfaceAtlas(theme.background, palette_id);
    if (atlas == nullptr || atlas->base == nullptr || atlas->frames.empty()) return;
    const HAL::GraphicFrame& frame = atlas->frames[0];
    if (theme.tiled && frame.rect.w > 0 && frame.rect.h > 0) {
        // Each tile's piece under `dst`.
        const int ox = theme.origin.x - frame.hotspot_x;
        const int oy = theme.origin.y - frame.hotspot_y;
        auto floor_div = [](int a, int b) { return a >= 0 ? a / b : -((-a + b - 1) / b); };
        const int tx0 = floor_div(dst.x - ox, frame.rect.w);
        const int ty0 = floor_div(dst.y - oy, frame.rect.h);
        for (int ty = ty0; oy + ty * frame.rect.h < dst.y + dst.h; ++ty) {
            for (int tx = tx0; ox + tx * frame.rect.w < dst.x + dst.w; ++tx) {
                const SDL_Rect tile = {ox + tx * frame.rect.w, oy + ty * frame.rect.h,
                                       frame.rect.w, frame.rect.h};
                SDL_Rect piece;
                if (!SDL_IntersectRect(&tile, &dst, &piece)) continue;
                const SDL_Rect src = {frame.rect.x + piece.x - tile.x, frame.rect.y + piece.y - tile.y,
                                      piece.w, piece.h};
                SDL_RenderCopy(r, atlas->base, &src, &piece);
            }
        }
        return;
    }
    // The piece of the art under `dst`, clipped to the art.
    const int fx = dst.x - (theme.origin.x - frame.hotspot_x);
    const int fy = dst.y - (theme.origin.y - frame.hotspot_y);
    SDL_Rect src = {frame.rect.x + fx, frame.rect.y + fy, dst.w, dst.h};
    SDL_Rect out = dst;
    if (fx < 0) { src.x -= fx; src.w += fx; out.x -= fx; out.w += fx; }
    if (fy < 0) { src.y -= fy; src.h += fy; out.y -= fy; out.h += fy; }
    const int over_x = (fx + dst.w) - frame.rect.w;
    const int over_y = (fy + dst.h) - frame.rect.h;
    if (over_x > 0) { src.w -= over_x; out.w -= over_x; }
    if (over_y > 0) { src.h -= over_y; out.h -= over_y; }
    if (src.w <= 0 || src.h <= 0) return;
    SDL_RenderCopy(r, atlas->base, &src, &out);
}

} // namespace

Theme MakeTheme(uint32_t sin_id, int screen_w, int screen_h, bool centred) {
    Theme theme;
    theme.style = LoadStyle(sin_id);
    const int which = screen_w <= 800 ? 0 : screen_w <= 1024 ? 1 : 2;
    theme.background = theme.style.backgrounds[which];
    auto& graphics = HAL::GraphicCache::Get();
    const auto& palette = graphics.InterfacePalette(theme.style.palette);
    for (int i = 0; i < 6; ++i) theme.bevel[i] = Entry(palette, theme.style.bevel[i]);
    theme.text = Snap(palette, theme.style.text);
    theme.text_shadow = Snap(palette, theme.style.text_shadow);
    theme.focus = Snap(palette, theme.style.focus);
    theme.focus_shadow = Snap(palette, theme.style.focus_shadow);
    theme.state = Snap(palette, theme.style.state);
    theme.state_shadow = Snap(palette, theme.style.state_shadow);
    // A disabled label's grey.
    theme.disabled = Snap(palette, SDL_Color{145, 135, 140, 255});
    theme.screen = {screen_w, screen_h};
    const SDL_Point size = BackgroundSize(theme);
    if (centred) {
        theme.origin = {(screen_w - size.x) / 2, (screen_h - size.y) / 2};
    } else if (theme.style.background_position == 2 && size.x > 0 && size.y > 0 &&
               (size.x < screen_w || size.y < screen_h)) {
        theme.tiled = true;
    }
    return theme;
}

SDL_Point BackgroundSize(const Theme& theme) {
    const HAL::GraphicAtlas* atlas =
        HAL::GraphicCache::Get().GetInterfaceAtlas(theme.background, theme.style.palette);
    if (atlas == nullptr || atlas->frames.empty()) return SDL_Point{0, 0};
    return SDL_Point{atlas->frames[0].rect.w, atlas->frames[0].rect.h};
}

void DrawBackground(SDL_Renderer* r, const Theme& theme) {
    if (theme.background == 0) return;
    if (theme.tiled) {
        BlitBackground(r, theme, theme.style.palette, SDL_Rect{0, 0, theme.screen.x, theme.screen.y});
        return;
    }
    const HAL::GraphicAtlas* atlas =
        HAL::GraphicCache::Get().GetInterfaceAtlas(theme.background, theme.style.palette);
    if (atlas == nullptr || atlas->base == nullptr || atlas->frames.empty()) return;
    const HAL::GraphicFrame& frame = atlas->frames[0];
    const SDL_Rect dst = {theme.origin.x - frame.hotspot_x, theme.origin.y - frame.hotspot_y,
                          frame.rect.w, frame.rect.h};
    SDL_RenderCopy(r, atlas->base, &frame.rect, &dst);
}

void DrawBackgroundIn(SDL_Renderer* r, const Theme& theme, const SDL_Rect& rect) {
    BlitBackground(r, theme, theme.style.palette, rect);
}

void DrawShaded(SDL_Renderer* r, const Theme& theme, const SDL_Rect& rect) {
    BlitBackground(r, theme,
                   HAL::GraphicCache::ShadedPalette(theme.style.palette, theme.style.shade_percent),
                   rect);
}

void DrawBevel(SDL_Renderer* r, const Theme& theme, const SDL_Rect& rect, bool pressed) {
    // Lit outer, middle and inner along the top and right; shaded outer,
    // middle and inner along the bottom and left. Pressed, they trade places.
    const SDL_Color* lit = theme.bevel;           // 0 outer, 1 middle, 2 inner
    const SDL_Color shade[3] = {theme.bevel[5], theme.bevel[4], theme.bevel[3]};   // outer first
    const SDL_Color* top = pressed ? shade : lit;
    const SDL_Color* bottom = pressed ? lit : shade;
    const int x = rect.x, y = rect.y, w = rect.w, h = rect.h;
    for (int i = 0; i < 3; ++i) {
        // Left and bottom first, so the lit top and right win at the corners
        // they share.
        Line(r, x + i, y + i, 1, h - 2 * i, bottom[i]);
        Line(r, x + i, y + h - 1 - i, w - 2 * i, 1, bottom[i]);
        Line(r, x + i, y + i, w - 2 * i, 1, top[i]);
        Line(r, x + w - 1 - i, y + i, 1, h - 2 * i, top[i]);
    }
}

void DrawEtched(SDL_Renderer* r, const Theme& theme, const SDL_Rect& rect) {
    const SDL_Color outer = theme.bevel[5];
    const SDL_Color inner = theme.bevel[0];
    Line(r, rect.x, rect.y, rect.w, 1, outer);
    Line(r, rect.x, rect.y + rect.h - 1, rect.w, 1, outer);
    Line(r, rect.x, rect.y, 1, rect.h, outer);
    Line(r, rect.x + rect.w - 1, rect.y, 1, rect.h, outer);
    Line(r, rect.x + 1, rect.y + 1, rect.w - 2, 1, inner);
    Line(r, rect.x + 1, rect.y + rect.h - 2, rect.w - 2, 1, inner);
    Line(r, rect.x + 1, rect.y + 1, 1, rect.h - 2, inner);
    Line(r, rect.x + rect.w - 2, rect.y + 1, 1, rect.h - 2, inner);
}

bool Hit(const SDL_Rect& rect, int x, int y) {
    return x >= rect.x && x < rect.x + rect.w && y >= rect.y && y < rect.y + rect.h;
}

State StateOf(const Button& button, int mx, int my, bool held) {
    if (!button.enabled) return State::Disabled;
    if (!Hit(button.rect, mx, my)) return State::Normal;
    return held ? State::Pressed : State::Hover;
}

void DrawButton(SDL_Renderer* r, const Theme& theme, const Button& button, State state) {
    if (!button.visible) return;
    DrawShaded(r, theme, button.rect);
    if (state == State::Disabled) {
        DrawEtched(r, theme, button.rect);
    } else {
        DrawBevel(r, theme, button.rect, state == State::Pressed);
    }
    const SDL_Color colour = state == State::Disabled ? theme.disabled
                           : state == State::Hover ? theme.focus
                           : state == State::Pressed || state == State::Selected ? theme.state
                           : theme.text;
    const SDL_Color shadow = state == State::Hover ? theme.focus_shadow : theme.text_shadow;
    SDL_Rect inside = button.rect;
    if (state == State::Pressed) { inside.x += 1; inside.y += 1; }
    DrawTextIn(r, button.font, inside, button.label, colour, shadow,
               state != State::Disabled);
}

void DrawText(SDL_Renderer* r, int font, int x, int y, std::string_view text, SDL_Color colour,
              SDL_Color shadow, Align align, bool shadowed) {
    const int w = Text::Width(font, text);
    if (align == Align::Centre) x -= w / 2;
    else if (align == Align::Right) x -= w;
    if (shadowed) Text::DrawShadowed(r, font, x, y, text, colour, shadow);
    else Text::Draw(r, font, x, y, text, colour);
}

void DrawTextIn(SDL_Renderer* r, int font, const SDL_Rect& rect, std::string_view text,
                SDL_Color colour, SDL_Color shadow, bool shadowed) {
    const int w = Text::Width(font, text);
    const int h = Text::Height(font);
    const int x = rect.x + (rect.w - w) / 2;
    const int y = rect.y + (rect.h - h) / 2;
    if (shadowed) Text::DrawShadowed(r, font, x, y, text, colour, shadow);
    else Text::Draw(r, font, x, y, text, colour);
}

int DrawWrapped(SDL_Renderer* r, int font, int x, int y, int width, std::string_view text,
                SDL_Color colour, SDL_Color shadow, bool shadowed) {
    std::vector<std::string_view> lines;
    Text::Wrap(font, text, width, lines);
    const int line_h = Text::Height(font);
    int dy = 0;
    for (const auto& line : lines) {
        if (shadowed) Text::DrawShadowed(r, font, x, y + dy, line, colour, shadow);
        else Text::Draw(r, font, x, y + dy, line, colour);
        dy += line_h;
    }
    return dy;
}

void DrawButtonArt(SDL_Renderer* r, const Theme& theme, int frame, int x, int y) {
    if (theme.style.button_art == 0) return;
    const HAL::GraphicAtlas* atlas =
        HAL::GraphicCache::Get().GetInterfaceAtlas(theme.style.button_art, theme.style.palette);
    if (atlas == nullptr || atlas->base == nullptr || frame < 0 ||
        frame >= static_cast<int>(atlas->frames.size())) {
        return;
    }
    const HAL::GraphicFrame& f = atlas->frames[frame];
    const SDL_Rect dst = {x - f.hotspot_x, y - f.hotspot_y, f.rect.w, f.rect.h};
    SDL_RenderCopy(r, atlas->base, &f.rect, &dst);
}

void DrawSunken2(SDL_Renderer* r, const Theme& theme, const SDL_Rect& rect) {
    // The .sin's sixth and fifth colours, outer then inner, along the top and
    // right; its first and second along the bottom and left.
    const SDL_Color top[2] = {theme.bevel[5], theme.bevel[4]};
    const SDL_Color bottom[2] = {theme.bevel[0], theme.bevel[1]};
    const int x = rect.x, y = rect.y, w = rect.w, h = rect.h;
    for (int i = 0; i < 2; ++i) {
        Line(r, x + i, y + i, 1, h - 2 * i, bottom[i]);
        Line(r, x + i, y + h - 1 - i, w - 2 * i, 1, bottom[i]);
        Line(r, x + i, y + i, w - 2 * i, 1, top[i]);
        Line(r, x + w - 1 - i, y + i, 1, h - 2 * i, top[i]);
    }
}

void DrawCloseBox(SDL_Renderer* r, const Theme& theme, bool pressed) {
    DrawShaded(r, theme, kCloseBox);
    DrawBevel(r, theme, kCloseBox, pressed);
    DrawTextIn(r, Text::kArial8, SDL_Rect{kCloseBox.x + (pressed ? 1 : 0), kCloseBox.y,
                                          kCloseBox.w, kCloseBox.h},
               "X", theme.text, theme.text_shadow);
}

namespace {
constexpr int kPanelInset = 5;
constexpr int kWrapSlack = 2;
// How far below Text's placement of a cell GDI's glyphs sit.
int Drop(int font) { return Text::GdiAscent(font) - Text::Ascent(font); }
} // namespace

Ink SinInk(const Theme& theme) { return Ink{2, theme.text, theme.text_shadow}; }

int PanelPitch(int font) { return Text::GdiHeight(font) + 3; }

int PanelWrapWidth(int panel_w, int border) {
    return panel_w - 2 * (border + kPanelInset) - kWrapSlack;
}

std::vector<std::string_view> PanelLines(int font, std::string_view text, int width, bool wrap) {
    std::vector<std::string_view> lines;
    if (wrap) {
        Text::Wrap(font, text, width, lines);
    } else {
        size_t start = 0;
        while (start <= text.size()) {
            const size_t end = text.find('\n', start);
            lines.push_back(text.substr(start, end == std::string_view::npos ? std::string_view::npos
                                                                              : end - start));
            if (end == std::string_view::npos) break;
            start = end + 1;
        }
    }
    // A trailing newline adds no line.
    while (!lines.empty() && lines.back().empty()) lines.pop_back();
    return lines;
}

void DrawPanelLine(SDL_Renderer* r, int font, int x, int y, std::string_view text, const Ink& ink) {
    y += Drop(font);
    switch (ink.style) {
    case 0: Text::Draw(r, font, x, y, text, ink.first); break;
    case 1: Text::DrawShadowed(r, font, x, y, text, ink.second, ink.first); break;
    default: Text::DrawShadowed(r, font, x, y, text, ink.first, ink.second); break;
    }
}

int DrawPanelText(SDL_Renderer* r, int font, const SDL_Rect& rect, std::string_view text,
                  Align across, Down down, bool wrap, const Ink& ink, int first, int border) {
    const std::vector<std::string_view> lines =
        PanelLines(font, text, PanelWrapWidth(rect.w, border), wrap);
    const int lh = PanelPitch(font);
    const int count = static_cast<int>(lines.size());
    const int fits = std::max(1, (rect.h - 2 * border) / std::max(1, lh));
    const int shown = down == Down::Top ? std::max(0, std::min(count - first, fits)) : count;
    const int top = down == Down::Top
        ? rect.y + border
        : rect.y + rect.h / 2 - (shown * lh - (lh - Text::GdiHeight(font))) / 2;
    for (int i = 0; i < shown; ++i) {
        const std::string_view line = lines[static_cast<size_t>(first + i)];
        const int x = across == Align::Left ? rect.x + border + kPanelInset
                    : across == Align::Right ? rect.x + rect.w - border - kPanelInset - Text::Width(font, line)
                    : rect.x + rect.w / 2 - Text::Width(font, line) / 2;
        DrawPanelLine(r, font, x, top + i * lh, line, ink);
    }
    return count;
}

// The button art: a plain face, a check, an empty and a filled round, the two
// arrows, a plain face, and the drop-down arrow, each with its pressed frame.
namespace Art {
constexpr int kCheck = 2;
constexpr int kUp = 8;
constexpr int kDown = 10;
} // namespace Art

SDL_Rect DropDownItemRect(const DropDown& d, int i, int screen_h) {
    const int rows = std::min(kDropDownVisible, static_cast<int>(d.items.size()));
    const int h = d.rect.h;
    const bool above = d.rect.y + h + rows * h > screen_h;
    const int row = i - d.scroll;
    if (above) return SDL_Rect{d.rect.x, d.rect.y - (rows - row) * h, d.rect.w - kDropDownArrowW, h};
    return SDL_Rect{d.rect.x, d.rect.y + h + row * h, d.rect.w - kDropDownArrowW, h};
}

namespace {
void DrawArrowButton(SDL_Renderer* r, const Theme& theme, const SDL_Rect& arrow, bool enabled,
                     bool pressed) {
    DrawShaded(r, theme, arrow);
    if (!enabled) {
        DrawEtched(r, theme, arrow);
        return;
    }
    DrawBevel(r, theme, arrow, pressed);
    // The art's arrow is a small downward triangle, centred in its box.
    const int ax = arrow.x + (arrow.w - 10) / 2 + (pressed ? 1 : 0);
    const int ay = arrow.y + (arrow.h - 5) / 2 + (pressed ? 1 : 0);
    SDL_SetRenderDrawColor(r, theme.text.r, theme.text.g, theme.text.b, 255);
    for (int row = 0; row < 5; ++row) {
        SDL_RenderDrawLine(r, ax + row, ay + row, ax + 9 - row, ay + row);
    }
}
} // namespace

void DrawDropDown(SDL_Renderer* r, const Theme& theme, const DropDown& d, int mx, int my,
                  bool held, int screen_h) {
    if (d.boxed) {
        DrawShaded(r, theme, d.rect);
        DrawBevel(r, theme, d.rect, held && Hit(d.rect, mx, my));
        const std::string current = d.selected >= 0 && d.selected < static_cast<int>(d.items.size())
                                  ? d.items[d.selected] : std::string{};
        const int text_y = d.rect.y + (d.rect.h - Text::Height(d.font)) / 2;
        DrawText(r, d.font, d.rect.x + d.rect.w / 2, text_y, current,
                 d.coloured ? d.colour : theme.text, theme.text_shadow, Align::Centre,
                 !d.coloured);
        if (!d.open) return;
    } else if (d.plain) {
        // The arrow first, its own square, then the choice in the page's ink.
        const SDL_Rect arrow = {d.rect.x, d.rect.y, d.rect.h - 2, d.rect.h};
        DrawArrowButton(r, theme, arrow, d.enabled, held && Hit(arrow, mx, my));
        const std::string current = d.selected >= 0 && d.selected < static_cast<int>(d.items.size())
                                  ? d.items[d.selected] : std::string{};
        const int text_y = d.rect.y + (d.rect.h - Text::Height(d.font)) / 2;
        Text::Draw(r, d.font, arrow.x + arrow.w + 7, text_y, current, d.ink);
        if (!d.open) return;
    } else {
    const SDL_Rect field = {d.rect.x, d.rect.y, d.rect.w - kDropDownArrowW, d.rect.h};
    DrawShaded(r, theme, field);
    const std::string current = d.selected >= 0 && d.selected < static_cast<int>(d.items.size())
                              ? d.items[d.selected] : std::string{};
    const int text_y = field.y + (field.h - Text::Height(d.font)) / 2;
    if (d.coloured) {
        // A number in its player's colour, centred in its box.
        DrawText(r, d.font, field.x + field.w / 2, text_y, current, d.colour, theme.text_shadow,
                 Align::Centre, false);
    } else {
        DrawText(r, d.font, field.x + 6, text_y, current,
                 d.enabled ? theme.text : theme.disabled, theme.text_shadow, Align::Left, d.enabled);
    }
    // The arrow: pressed while held.
    const SDL_Rect arrow = {d.rect.x + d.rect.w - kDropDownArrowW, d.rect.y, kDropDownArrowW,
                            d.rect.h};
    DrawArrowButton(r, theme, arrow, d.enabled, held && Hit(arrow, mx, my));
    if (!d.open) return;
    }
    // The choices, on the darker shade, the one under the mouse lit.
    const int rows = std::min(kDropDownVisible, static_cast<int>(d.items.size()));
    for (int i = d.scroll; i < d.scroll + rows && i < static_cast<int>(d.items.size()); ++i) {
        const SDL_Rect item = DropDownItemRect(d, i, screen_h);
        Line(r, item.x, item.y, item.w, item.h, theme.bevel[5]);
        const bool hover = Hit(item, mx, my);
        DrawText(r, d.font, item.x + 6, item.y + (item.h - Text::Height(d.font)) / 2, d.items[i],
                 hover ? theme.focus : i == d.selected ? theme.state : theme.text,
                 theme.text_shadow);
    }
    const SDL_Rect first = DropDownItemRect(d, d.scroll, screen_h);
    DrawBevel(r, theme, SDL_Rect{first.x, first.y, first.w, rows * d.rect.h}, false);
}

bool ClickDropDown(DropDown& d, int x, int y, int screen_h) {
    if (!d.enabled) return false;
    if (d.open) {
        const int rows = std::min(kDropDownVisible, static_cast<int>(d.items.size()));
        for (int i = d.scroll; i < d.scroll + rows && i < static_cast<int>(d.items.size()); ++i) {
            if (Hit(DropDownItemRect(d, i, screen_h), x, y)) {
                d.selected = i;
                d.open = false;
                return true;
            }
        }
        d.open = false;
        return Hit(d.rect, x, y);
    }
    if (Hit(d.rect, x, y)) {
        d.open = true;
        // Open with the current choice in view.
        d.scroll = std::max(0, std::min(d.selected,
                                        static_cast<int>(d.items.size()) - kDropDownVisible));
        return true;
    }
    return false;
}

void DrawCheckBox(SDL_Renderer* r, const Theme& theme, const CheckBox& c, int mx, int my,
                  bool held) {
    // Sunk into the page, which shows through it; pressed, it rises.
    (void)mx;
    (void)my;
    (void)held;
    if (!c.enabled) {
        DrawEtched(r, theme, c.box);
    } else {
        DrawBevel(r, theme, c.box, true);
    }
    if (!c.checked) return;
    // The check mark: the button art's own, centred.
    if (theme.style.button_art != 0) {
        const HAL::GraphicAtlas* atlas = HAL::GraphicCache::Get().GetInterfaceAtlas(
            theme.style.button_art, theme.style.palette);
        if (atlas != nullptr && atlas->base != nullptr && Art::kCheck < static_cast<int>(atlas->frames.size())) {
            // Only the mark itself: the art's middle, without its face.
            const HAL::GraphicFrame& f = atlas->frames[Art::kCheck];
            const int inset = 9;
            SDL_Rect src = {f.rect.x + inset, f.rect.y + inset, f.rect.w - 2 * inset,
                            f.rect.h - 2 * inset};
            const SDL_Rect dst = {c.box.x + (c.box.w - src.w) / 2, c.box.y + (c.box.h - src.h) / 2,
                                  src.w, src.h};
            // The mark's pixels stand out from the face by being dark, so
            // they are drawn as they are over the shaded field.
            SDL_RenderCopy(r, atlas->base, &src, &dst);
            return;
        }
    }
    SDL_SetRenderDrawColor(r, theme.text.r, theme.text.g, theme.text.b, 255);
    SDL_RenderDrawLine(r, c.box.x + 5, c.box.y + c.box.h / 2, c.box.x + c.box.w / 2,
                       c.box.y + c.box.h - 6);
    SDL_RenderDrawLine(r, c.box.x + c.box.w / 2, c.box.y + c.box.h - 6, c.box.x + c.box.w - 5,
                       c.box.y + 5);
}

void DrawSlider(SDL_Renderer* r, const Theme& theme, const Slider& s) {
    // A sunken groove across the track, and the thumb a raised button on it.
    const SDL_Rect groove = {s.track.x, s.track.y + s.track.h / 2 - 3, s.track.w, 6};
    DrawShaded(r, theme, groove);
    DrawBevel(r, theme, groove, true);
    const int thumb_w = 12;
    const int x = s.track.x + (s.track.w - thumb_w) * std::clamp(s.value, 0, s.steps) /
                              std::max(1, s.steps);
    const SDL_Rect thumb = {x, s.track.y, thumb_w, s.track.h};
    DrawShaded(r, theme, thumb);
    DrawBevel(r, theme, thumb, false);
}

bool DragSlider(Slider& s, int x, int y) {
    if (!Hit(SDL_Rect{s.track.x - 6, s.track.y, s.track.w + 12, s.track.h}, x, y)) return false;
    const int rel = std::clamp(x - s.track.x, 0, s.track.w);
    s.value = (rel * s.steps + s.track.w / 2) / std::max(1, s.track.w);
    return true;
}

Slider SizeSlider(const SDL_Rect& track, int percent) {
    const int least = Core::Profile::kInterfaceSizeLeast;
    const int steps = (Core::Profile::kInterfaceSizeMost - least) / 10;
    return Slider{track, std::clamp((percent - least + 5) / 10, 0, steps), steps};
}

int SizeOf(const Slider& s) { return Core::Profile::kInterfaceSizeLeast + s.value * 10; }

void DrawSizeSlider(SDL_Renderer* r, const Theme& theme, const Slider& s, std::string_view label) {
    const SDL_Color ink = {0, 0, 0, 255};
    DrawText(r, Text::kGeorgia10B, s.track.x, s.track.y - 20, label, ink, ink, Align::Left, false);
    DrawSlider(r, theme, s);
    DrawText(r, Text::kArial8, s.track.x, s.track.y + 24,
             std::to_string(Core::Profile::kInterfaceSizeLeast) + "%", ink, ink, Align::Left, false);
    DrawText(r, Text::kGeorgia9B, s.track.x + s.track.w / 2, s.track.y + 22,
             std::to_string(SizeOf(s)) + "%", ink, ink, Align::Centre, false);
    DrawText(r, Text::kArial8, s.track.x + s.track.w, s.track.y + 24,
             std::to_string(Core::Profile::kInterfaceSizeMost) + "%", ink, ink, Align::Right, false);
}

int ListBoxRows(const ListBox& l) {
    const int line_h = std::max(1, Text::Height(l.font) + 2);
    return std::max(1, (l.rect.h - 4) / line_h);
}

void DrawListBox(SDL_Renderer* r, const Theme& theme, const ListBox& l, int mx, int my) {
    DrawShaded(r, theme, l.rect);
    DrawBevel(r, theme, l.rect, true);
    const int line_h = Text::Height(l.font) + 2;
    const int rows = ListBoxRows(l);
    const bool bar = static_cast<int>(l.items.size()) > rows;
    const int text_w = l.rect.w - (bar ? 24 : 0);
    for (int i = 0; i < rows && l.scroll + i < static_cast<int>(l.items.size()); ++i) {
        const int index = l.scroll + i;
        const SDL_Rect row = {l.rect.x + 3, l.rect.y + 3 + i * line_h, text_w - 6, line_h};
        if (index == l.selected) Line(r, row.x, row.y, row.w, row.h, theme.bevel[5]);
        const bool hover = Hit(row, mx, my);
        DrawText(r, l.font, row.x + 3, row.y + 1, l.items[index],
                 index == l.selected ? theme.state : hover ? theme.focus : theme.text,
                 theme.text_shadow);
    }
    if (!bar) return;
    // The scroll arrows: the button art's own.
    DrawButtonArt(r, theme, Art::kUp, l.rect.x + l.rect.w - 22, l.rect.y + 2);
    DrawButtonArt(r, theme, Art::kDown, l.rect.x + l.rect.w - 22, l.rect.y + l.rect.h - 22);
}

bool ClickListBox(ListBox& l, int x, int y) {
    if (!Hit(l.rect, x, y)) return false;
    const int rows = ListBoxRows(l);
    const bool bar = static_cast<int>(l.items.size()) > rows;
    if (bar && x >= l.rect.x + l.rect.w - 24) {
        if (y < l.rect.y + l.rect.h / 2) l.scroll = std::max(0, l.scroll - 1);
        else l.scroll = std::min(static_cast<int>(l.items.size()) - rows, l.scroll + 1);
        return true;
    }
    const int line_h = Text::Height(l.font) + 2;
    const int index = l.scroll + (y - l.rect.y - 3) / line_h;
    if (index >= 0 && index < static_cast<int>(l.items.size())) l.selected = index;
    return true;
}

void DrawTextField(SDL_Renderer* r, const Theme& theme, const TextField& f) {
    if (f.focused) HAL::TextInput::At(f.rect);
    DrawShaded(r, theme, f.rect);
    DrawBevel(r, theme, f.rect, true);
    const int y = f.rect.y + (f.rect.h - Text::Height(f.font)) / 2;
    DrawText(r, f.font, f.rect.x + 6, y, f.text, theme.text, theme.text_shadow);
    if (f.focused && (SDL_GetTicks() / 500) % 2 == 0) {
        const int cx = f.rect.x + 6 + Text::Width(f.font, f.text) + 1;
        Line(r, cx, y + 1, 1, Text::Height(f.font) - 2, theme.text);
    }
}

bool TypeIntoField(TextField& f, const SDL_Event& event) {
    if (!f.focused) return false;
    if (event.type == SDL_TEXTINPUT) {
        for (const char* c = event.text.text; *c; ++c) {
            if (f.text.size() < f.max_length) f.text += *c;
        }
        return true;
    }
    if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_BACKSPACE) {
        // Whole UTF-8 characters.
        while (!f.text.empty()) {
            const unsigned char last = static_cast<unsigned char>(f.text.back());
            f.text.pop_back();
            if ((last & 0xC0) != 0x80) break;
        }
        return true;
    }
    return false;
}

namespace {
// The message box's margins inside its art, and its button's size.
constexpr int kBoxMargin = 24;
constexpr int kBoxButtonW = 144;
constexpr int kBoxButtonH = 30;
}

void OpenNotice(Notice& box, const Theme& dialog, std::string text, std::string button) {
    const SDL_Point size = BackgroundSize(dialog);
    const SDL_Rect art = {dialog.origin.x, dialog.origin.y, size.x, size.y};
    box.open = true;
    box.text = std::move(text);
    box.ok = Button{SDL_Rect{art.x + (art.w - kBoxButtonW) / 2, art.y + art.h - kBoxMargin - kBoxButtonH,
                             kBoxButtonW, kBoxButtonH},
                    std::move(button), Text::kBlackletter14};
}

void DrawNotice(SDL_Renderer* r, const Theme& dialog, const Notice& box, const Pointer& p) {
    if (!box.open) return;
    DrawBackground(r, dialog);
    const SDL_Point size = BackgroundSize(dialog);
    const int width = size.x - 2 * kBoxMargin;
    const int font = Text::GameFont(11);
    std::vector<std::string_view> lines;
    Text::Wrap(font, box.text, width, lines);
    const int tall = static_cast<int>(lines.size()) * Text::Height(font);
    const int room = size.y - 3 * kBoxMargin - kBoxButtonH;
    int y = dialog.origin.y + kBoxMargin + std::max(0, (room - tall) / 2);
    for (const auto& line : lines) {
        const int w = Text::Width(font, line);
        Text::DrawShadowed(r, font, dialog.origin.x + (size.x - w) / 2, y, line, dialog.text, dialog.text_shadow);
        y += Text::Height(font);
    }
    DrawButton(r, dialog, box.ok, StateOf(box.ok, p.x, p.y, p.held));
}

} // namespace Screens
