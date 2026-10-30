#pragma once
#include <SDL2/SDL.h>
#include <string>
#include <string_view>
#include <vector>

#include "Sin.h"

// The screens' controls: a button is the screen's own background darkened
// by the .sin's shade amount, in a three-line bevel of the .sin's six
// colours, with its label centred in it; a disabled one is etched instead,
// its label greyed. Every colour is snapped to the screen's 256-colour
// palette.
//
// Plain data and free functions: a screen keeps its controls in arrays and
// calls these to draw them and hit-test them.
namespace Screens {

// The mouse as the screens see it.
struct Pointer {
    int x = -1;
    int y = -1;
    bool held = false;
};

// What a screen draws with: its style, resolved for the screen size it is
// shown at, with its colours snapped to its palette.
struct Theme {
    ScreenStyle style;
    uint32_t background = 0;      // the art for this screen size
    SDL_Point origin{0, 0};       // where that art's top-left is drawn
    SDL_Color bevel[6] = {};      // lit outer, lit middle, lit inner, shaded inner, middle, outer
    SDL_Color text{}, text_shadow{}, focus{}, focus_shadow{}, state{}, state_shadow{};
    SDL_Color disabled{};
    // The art repeated across the screen from `origin` rather than drawn
    // once (the .sin's background position 2 with art smaller than the
    // screen): the scenario editor's.
    bool tiled = false;
    SDL_Point screen{0, 0};       // the screen's size, which tiling fills
};

// A screen's theme from its .sin file. A dialog's art is centred on the
// screen; a full-screen background sits at the origin.
Theme MakeTheme(uint32_t sin_id, int screen_w, int screen_h, bool centred = false);

// The art's own size, for laying out against it.
SDL_Point BackgroundSize(const Theme& theme);

void DrawBackground(SDL_Renderer* r, const Theme& theme);
// The background's piece inside a rectangle, as it is under it.
void DrawBackgroundIn(SDL_Renderer* r, const Theme& theme, const SDL_Rect& rect);
// The background, darkened by the screen's shade, inside a rectangle.
void DrawShaded(SDL_Renderer* r, const Theme& theme, const SDL_Rect& rect);
// The three-line bevel round a rectangle: lit from the top right, pressed
// the other way about.
void DrawBevel(SDL_Renderer* r, const Theme& theme, const SDL_Rect& rect, bool pressed);
// A disabled control's border: a shaded line outside a lit one, all round.
void DrawEtched(SDL_Renderer* r, const Theme& theme, const SDL_Rect& rect);

enum class State { Normal, Hover, Pressed, Disabled, Selected };

// A text button.
struct Button {
    SDL_Rect rect{0, 0, 0, 0};
    std::string label;
    int font = 0;               // Rendering::Text font
    bool enabled = true;
    bool visible = true;
};

bool Hit(const SDL_Rect& rect, int x, int y);
State StateOf(const Button& button, int mx, int my, bool held);
void DrawButton(SDL_Renderer* r, const Theme& theme, const Button& button, State state);

// Text in the screen's colours.
enum class Align { Left, Centre, Right };
void DrawText(SDL_Renderer* r, int font, int x, int y, std::string_view text, SDL_Color colour,
              SDL_Color shadow, Align align = Align::Left, bool shadowed = true);
// Text centred in a rectangle, vertically by the font's cell.
void DrawTextIn(SDL_Renderer* r, int font, const SDL_Rect& rect, std::string_view text,
                SDL_Color colour, SDL_Color shadow, bool shadowed = true);
// A block of text wrapped to a width, from the top down; returns its height.
int DrawWrapped(SDL_Renderer* r, int font, int x, int y, int width, std::string_view text,
                SDL_Color colour, SDL_Color shadow, bool shadowed = true);

// Text panels: a string in a rectangle, in a font of the game's table,
// wrapped or not, each line placed across from the left, from the right or
// round the middle, and down from the top or round the middle. An Ink's
// style sets its colours: 0 the first colour alone; 1 the first a pixel down
// and left under the second; 2 the second under the first (the default, a
// screen's .sin text colour over its shadow).
//
// Placed in GDI's terms (Text::GdiAscent, GdiHeight): a left-aligned line
// five pixels in from the panel's edge and its border; the first cell's top
// on the panel's top inside its border; lines the font table's height (the
// cell and one) and two apart, 18 for font 11; a wrapped line fitting the
// width less those insets less two more.
enum class Down { Top, Centre };
struct Ink {
    int style = 2;
    SDL_Color first{0, 0, 0, 255};
    SDL_Color second{0, 0, 0, 255};
};
// A screen's own default: its .sin's text colour over its shadow.
Ink SinInk(const Theme& theme);
// Plain black, unshadowed.
constexpr Ink kBlackInk{0, {0, 0, 0, 255}, {0, 0, 0, 255}};

// A panel's line pitch, and the width its text wraps to.
int PanelPitch(int font);
int PanelWrapWidth(int panel_w, int border);
// The lines of a panel's text: wrapped to `width`, or as written.
std::vector<std::string_view> PanelLines(int font, std::string_view text, int width, bool wrap);
// One line with its GDI cell's top-left at (x, y), in the ink's style.
void DrawPanelLine(SDL_Renderer* r, int font, int x, int y, std::string_view text, const Ink& ink);
// Draws a panel's text; `first` is the first line shown (a scrolled panel),
// `border` the width of its bevel. Returns how many lines the text takes.
int DrawPanelText(SDL_Renderer* r, int font, const SDL_Rect& rect, std::string_view text,
                  Align across, Down down, bool wrap, const Ink& ink, int first = 0,
                  int border = 0);

// One frame of the screen's button art (the .sin's button file): the check
// box, the arrows of a list.
void DrawButtonArt(SDL_Renderer* r, const Theme& theme, int frame, int x, int y);

// The two-line sunken border of text boxes:
// the shaded side outermost first along the top and right, the lit side
// outermost first along the bottom and left.
void DrawSunken2(SDL_Renderer* r, const Theme& theme, const SDL_Rect& rect);

// The top right corner's close button, on every menu screen.
constexpr SDL_Rect kCloseBox = {782, 4, 15, 15};
void DrawCloseBox(SDL_Renderer* r, const Theme& theme, bool pressed);

// A drop-down list: its current choice in a shaded field, the arrow button
// beside it, and when open the choices below it.
struct DropDown {
    SDL_Rect rect{0, 0, 0, 0};        // the field and its arrow, together
    std::vector<std::string> items;
    int selected = 0;
    bool open = false;
    bool enabled = true;
    int font = 0;
    int scroll = 0;                   // first choice shown when open
    // The player table's look: the arrow on the left, the choice beside it in
    // the page's ink, with no field behind it.
    bool plain = false;
    SDL_Color ink{0, 0, 0, 255};
    // A raised box with its choice centred in it and no arrow: the player
    // table's player and team numbers.
    bool boxed = false;
    // Its choice drawn in this colour instead of the screen's: a player colour.
    bool coloured = false;
    SDL_Color colour{255, 255, 255, 255};
};
constexpr int kDropDownArrowW = 20;
constexpr int kDropDownVisible = 8;   // choices shown at once when open
// The rectangle of choice `i` while open, below (or above, near the bottom
// of the screen) the field.
SDL_Rect DropDownItemRect(const DropDown& d, int i, int screen_h);
void DrawDropDown(SDL_Renderer* r, const Theme& theme, const DropDown& d, int mx, int my,
                  bool held, int screen_h);
// A click at (x, y). Returns true when it was the drop-down's: opening it,
// choosing, or closing it again.
bool ClickDropDown(DropDown& d, int x, int y, int screen_h);

// A check box: the button art's check, with its label to the left.
struct CheckBox {
    SDL_Rect box{0, 0, 0, 0};
    bool checked = false;
    bool enabled = true;
};
void DrawCheckBox(SDL_Renderer* r, const Theme& theme, const CheckBox& c, int mx, int my, bool held);

// A slider: a track with a thumb, its value from 0 to `steps`.
struct Slider {
    SDL_Rect track{0, 0, 0, 0};
    int value = 0;
    int steps = 10;
};
void DrawSlider(SDL_Renderer* r, const Theme& theme, const Slider& s);
// Moves the thumb to the mouse; true when the point is on the track.
bool DragSlider(Slider& s, int x, int y);

// A slider for one of the interface's sizes
// (Profile::Options::top_bar_size and its like): 100% to 200% in tens.
Slider SizeSlider(const SDL_Rect& track, int percent);
int SizeOf(const Slider& s);
// With its name and its size over the track, and the ends under it.
void DrawSizeSlider(SDL_Renderer* r, const Theme& theme, const Slider& s, std::string_view label);

// A list of lines to pick from, with a scroll bar when it overflows.
struct ListBox {
    SDL_Rect rect{0, 0, 0, 0};
    std::vector<std::string> items;
    int selected = -1;
    int scroll = 0;
    int font = 0;
};
int ListBoxRows(const ListBox& l);
void DrawListBox(SDL_Renderer* r, const Theme& theme, const ListBox& l, int mx, int my);
// A click: picks the row under it, or scrolls with the arrows. True when it
// was the list's.
bool ClickListBox(ListBox& l, int x, int y);

// A single line of typed text.
struct TextField {
    SDL_Rect rect{0, 0, 0, 0};
    std::string text;
    size_t max_length = 32;
    bool focused = false;
    int font = 0;
};
void DrawTextField(SDL_Renderer* r, const Theme& theme, const TextField& f);
// Keys and typing for a focused field; true when the event was taken.
bool TypeIntoField(TextField& f, const SDL_Event& event);

// A message over a screen: its text wrapped in the dialog art's middle, the
// art centred on the screen (`dialog` is a centred theme), and one button
// under the text.
struct Notice {
    bool open = false;
    std::string text;
    Button ok;
};
void OpenNotice(Notice& box, const Theme& dialog, std::string text, std::string button);
void DrawNotice(SDL_Renderer* r, const Theme& dialog, const Notice& box, const Pointer& p);

} // namespace Screens
