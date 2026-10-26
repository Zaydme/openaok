#pragma once
#include <SDL2/SDL.h>
#include <algorithm>
#include <cstdint>

namespace Rendering {

// Where every battlefield interface element sits, for each of the three
// screen sizes, over the interface art (interfac.drs 51101+, 51121+, 51141+;
// one frame per civilisation).
struct HudLayout {
    int width;
    int height;
    uint32_t art_base;           // the first civilisation's frame; civ n is base + n - 1
    int top_bar_h;               // the top bar's rows
    int panel_top;               // the bottom panel's first row

    // Top bar: each stockpile's number ends at its slot's right edge; the age
    // is centred in the band between the population slot's right edge and the
    // first button (x 553 at 1024).
    int resource_right[5];       // wood, food, gold, stone, population
    int text_y;                  // top of the readout's text cell
    int age_y;                   // and of the age's
    int age_left;
    int age_right;
    SDL_Point bar_button[5];     // objectives, tech tree, chat, diplomacy, menu
    // The food symbol (53010) is drawn over the art's own icon.
    SDL_Point food_icon;

    // The command grid: button 1's top-left, and the step to the next.
    SDL_Point grid;
    int pitch_x;
    int pitch_y;

    // The selected object, in the parchment.
    SDL_Point name;
    SDL_Point portrait;
    SDL_Point hp_text;
    SDL_Point stat_icon;
    SDL_Point stat_text;
    int stat_pitch;
    SDL_Point civ;               // civilisation, player, diplomatic stance
    int civ_pitch;

    // Many selected: their cards fill this area, from the parchment's left
    // edge -- six pixels left of the name, two below it -- to its right edge.
    SDL_Rect selection;

    // The mini-map's diamond, and the buttons around it.
    SDL_Rect minimap;
    uint32_t minimap_slp;
    SDL_Point minimap_button[8];

    // Rollover help: left edge, and the bottom of its last line.
    int help_x;
    int help_bottom;
};

// The buttons round the mini-map, in kLayouts' order.
enum MinimapButton : int {
    kSignalAllies = 0,
    kIdleVillager = 1,
    kStatistics = 2,       // the scores over the map, on or off
    kHelpToggle = 3,       // the rollover help in the lower-left corner
    kAdvancedCommands = 4,
    kNormalMode = 5,
    kEconomicMode = 6,
    kCombatMode = 7,
    kMinimapButtonCount = 8,
};

// Each button's frames in its sheet: the button, then it pressed or lit.
constexpr int kMinimapButtonFrame[kMinimapButtonCount] = {2, 12, 6, 0, 14, 8, 10, 4};

// The top bar's buttons, in kLayouts' order, and their frames in 50726: the
// button, then pressed.
enum BarButton : int {
    kObjectives = 0,
    kTechTree = 1,
    kChat = 2,
    kDiplomacy = 3,
    kGameMenu = 4,
    kBarButtonCount = 5,
};
constexpr uint32_t kBarButtonSlp = 50726;
constexpr int kBarButtonFrame[kBarButtonCount] = {8, 4, 2, 6, 0};
// The objectives button while there is news in them, flashing.
constexpr int kObjectivesLitFrame = 10;

constexpr HudLayout kLayouts[] = {
    // 800x600.
    {800, 600, 51101, 25, 455,
     {75, 152, 229, 306, 383}, 5, 5, 384, 507,
     {{506, 3}, {566, 3}, {626, 3}, {686, 3}, {745, 3}}, {86, 4},
     {5, 465}, 41, 41,
     {224, 469}, {226, 490}, {266, 524}, {225, 539}, {267, 542}, 20,
     {360, 501}, 20,
     {218, 471, 285, 124},
     {511, 465, 265, 133}, 50789,
     {{516, 477}, {516, 554}, {719, 477}, {749, 477}, {768, 499}, {768, 542},
      {719, 565}, {748, 563}},
     4, 441},
    // 1024x768.
    {1024, 768, 51121, 25, 593,
     {75, 152, 229, 306, 383}, 5, 5, 384, 722,
     {{721, 4}, {781, 4}, {841, 4}, {901, 4}, {960, 4}}, {86, 4},
     {37, 624}, 41, 41,
     {301, 621}, {303, 639}, {344, 673}, {301, 688}, {343, 691}, 20,
     {436, 650}, 20,
     {295, 623, 375, 124},
     {690, 600, 322, 163}, 50788,
     {{716, 614}, {715, 719}, {928, 612}, {955, 606}, {964, 631}, {963, 709},
      {922, 729}, {950, 733}},
     6, 577},
    // 1280x1024. The readout's icons and numbers sit six rows lower than at
    // 1024; the age's box stays at y 3 on every size. The object panel is
    // 1024's size, so its contents keep their offsets in it.
    {1280, 1024, 51141, 32, 806,
     {75, 152, 229, 306, 383}, 11, 5, 521, 843,
     {{978, 6}, {1038, 6}, {1098, 6}, {1158, 6}, {1217, 6}}, {86, 10},
     {44, 845}, 41, 41,
     {354, 832}, {356, 850}, {397, 884}, {354, 899}, {396, 902}, 20,
     {489, 861}, 20,
     {348, 834, 375, 124},
     {870, 818, 390, 197}, 50790,
     {{887, 829}, {886, 956}, {1148, 826}, {1187, 826}, {1210, 856}, {1211, 946},
      {1145, 979}, {1186, 975}},
     8, 790},
};

// The frame being drawn, as the renderer last set it. At the three 4:3 sizes
// the table is used as it is; a widescreen frame (the Options screen's
// Widescreen) is wider than its height's 4:3 size, and its interface is that
// size's carried out to the frame's edges (Edged), the map filling the rest.
struct Frame {
    int w = 0;
    int h = 0;
};
inline Frame& CurrentFrame() {
    static Frame frame;
    return frame;
}
inline void SetFrame(int w, int h) { CurrentFrame() = {w, h}; }

// The 4:3 table for a frame `height` rows tall.
inline const HudLayout& LayoutForHeight(int height) {
    if (height <= 600) return kLayouts[0];
    if (height <= 768) return kLayouts[1];
    return kLayouts[2];
}

// Where the interface art is cut to carry it out to a wider frame's edges:
// the top bar in the plain stretch beside the age, the panel between the
// parchment and the mini-map's frame. What lies left of the cut stays at the
// left edge, what lies right of it goes to the right edge, and the art's own
// plain stretch -- the band beside the age, the carving behind the command
// grid -- is repeated across the gap.
inline int BarCut(const HudLayout& base) { return base.age_right - 60; }
inline int PanelCut(const HudLayout& base) { return base.selection.x + base.selection.w + 2; }
inline SDL_Rect BarBand(const HudLayout& base) { return {BarCut(base), 0, 50, base.top_bar_h}; }
inline SDL_Rect PanelBand(const HudLayout& base) {
    return {base.grid.x + 60, base.panel_top, 40, base.height - base.panel_top};
}
// The row between the bar's art and the panel's, in the transparent middle:
// what is above it is the top bar's, what is below it the panel's.
inline int HudSplit(const HudLayout& base) { return (base.top_bar_h + base.panel_top) / 2; }

// `base` carried out to a frame `width` wide: the readout, the command grid,
// the parchment and the help stay at the left edge; the bar's buttons, the
// right end of the age's box, and the mini-map with its buttons go to the
// right edge.
inline HudLayout Edged(const HudLayout& base, int width) {
    HudLayout l = base;
    const int dx = std::max(0, width - base.width);
    l.width = base.width + dx;
    l.age_right += dx;
    for (SDL_Point& p : l.bar_button) p.x += dx;
    l.minimap.x += dx;
    for (SDL_Point& p : l.minimap_button) p.x += dx;
    return l;
}

// The table for a frame of this width and the last set height: the 4:3
// table for its own width, carried out to the edges (Edged) for any other.
// With no frame set yet, the 4:3 size nearest the width.
inline const HudLayout& LayoutFor(int width) {
    const Frame& f = CurrentFrame();
    if (f.h <= 0) {
        if (width <= 800) return kLayouts[0];
        if (width <= 1024) return kLayouts[1];
        return kLayouts[2];
    }
    const HudLayout& base = LayoutForHeight(f.h);
    if (width == base.width) return base;
    // A frame and its interface's parts (UIRenderer's HudSpace) ask for three
    // widths at most a frame; each is worked out once, and kept for a good
    // while after, so a layout handed out this frame is never written over
    // in it -- not even the frame the Options change a part's size.
    struct Cached {
        int width = -1;
        int height = -1;
        HudLayout layout{};
    };
    constexpr int kKept = 12;
    static Cached cache[kKept];
    static int next = 0;
    for (const Cached& c : cache) {
        if (c.width == width && c.height == f.h) return c.layout;
    }
    Cached& slot = cache[next];
    next = (next + 1) % kKept;
    slot.width = width;
    slot.height = f.h;
    slot.layout = Edged(base, width);
    return slot.layout;
}

} // namespace Rendering
