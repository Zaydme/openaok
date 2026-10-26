#pragma once
#include <SDL2/SDL.h>
#include <string>
#include <string_view>
#include <vector>

namespace Rendering {

// The interface's text, in the game's own fonts.
//
// LANGUAGE.DLL names every font (strings 110 to 211: face, point size,
// style); the faces are on the CD (`original_game_1999/SYSTEM/FONTS`), all
// but Arial, a Windows system font. Text is drawn as GDI draws it on a 96 DPI
// screen with no smoothing: a size is MulDiv(points, 96, 72) pixels of em,
// hinted, one bit per pixel, which FreeType's TrueType interpreter gives in
// monochrome mode. Faces the CD has no bold for are emboldened as GDI
// simulates it, by overstriking one pixel to the right.
//
// Text is placed as GDI's TextOut places it: (x, y) is the top-left of
// the line's cell, the baseline sits `Ascent` below it, and lines are
// `Height` apart.
namespace Text {

// The font table, by position; the notes say where each is used.
enum Font : int {
    kBlackletter14 = 0,     // Lucida Blackletter 14: menu buttons
    kBlackletter21B = 1,    // Lucida Blackletter 21 bold: screen titles
    kBlackletter12B = 2,    // Lucida Blackletter 12 bold
    kGeorgia9B = 3,         // Georgia 9 bold: the resource readout, the age
    kGeorgia8B = 4,         // Georgia 8 bold
    kGeorgia9 = 5,          // Georgia 9
    kGeorgia9B_2 = 6,       // Georgia 9 bold: the selected unit's name
    kArial8 = 7,            // Arial 8: hit points and stats
    kBlackletter38B = 8,    // Lucida Blackletter 38 bold
    kBlackletter28B = 9,    // Lucida Blackletter 28 bold
    kArialNarrow9 = 10,     // Arial Narrow 9
    kArial9 = 11,           // Arial 9
    kBlackletter12 = 12,    // Lucida Blackletter 12
    kBlackletter12B_2 = 13, // Lucida Blackletter 12 bold
    kViner16 = 14,          // Viner Hand ITC 16
    kGeorgia9B_3 = 15,      // Georgia 9 bold
    kGeorgia9I = 16,        // Georgia 9 italic
    kGeorgia9BI = 17,       // Georgia 9 bold italic
    kCalligraphy14 = 18,    // Lucida Calligraphy 14
    kPoorRichard14 = 19,    // Poor Richard 14
    kGeorgia10B = 20,       // Georgia 10 bold
    kBright12BI = 21,       // Lucida Bright 12 bold italic
    kBlackletter14B = 22,   // Lucida Blackletter 14 bold
    kBlackletter12B_3 = 23, // Lucida Blackletter 12 bold
    kGeorgia11 = 24,        // Georgia 11
    kBlackletter12B_4 = 25, // Lucida Blackletter 12 bold
    kGeorgia10B_2 = 26,     // Georgia 10 bold
    kGeorgia9B_4 = 27,      // Georgia 9 bold
    kBlackletter11 = 28,    // Lucida Blackletter 11
    kPapyrus12 = 29,        // Papyrus 12
    kGeorgia9B_5 = 30,      // Georgia 9 bold
    kGeorgia11B = 31,       // Georgia 11 bold
    kGeorgia11I = 32,       // Georgia 11 italic
    kGeorgia11BI = 33,      // Georgia 11 bold italic
    kFontCount = 34,
};

// Opens every face in the table. `font_dir` holds the CD's fonts; Arial comes
// from the system. False only if no face at all could be opened.
bool Init(SDL_Renderer* renderer, const std::string& font_dir);

// The cell height of one line, and the baseline's depth into it, in pixels.
int Height(int font);
int Ascent(int font);

// What Windows reports for the face (tmHeight, tmAscent): the font's VDMX
// entry for its size, a pixel or two more than the scaled outline metrics
// above for most of the CD's faces (Georgia 9: ascent 12, not 11). A
// panel's layout -- its lines, their spacing -- measures with these; the
// glyphs themselves are drawn from Ascent.
int GdiHeight(int font);
int GdiAscent(int font);

// The advance of `text` drawn in `font`, in pixels. UTF-8; a newline ends the
// measurement.
int Width(int font, std::string_view text);

// Draws one line with its cell's top-left at (x, y).
void Draw(SDL_Renderer* renderer, int font, int x, int y, std::string_view text,
          SDL_Color colour);
// The same with a one-pixel drop shadow down and to the left: light text on
// art.
void DrawShadowed(SDL_Renderer* renderer, int font, int x, int y, std::string_view text,
                  SDL_Color colour, SDL_Color shadow);
// With a one-pixel outline all round: text over the world.
void DrawOutlined(SDL_Renderer* renderer, int font, int x, int y, std::string_view text,
                  SDL_Color colour, SDL_Color outline);

// A GDI strike-out through a line of `width` drawn at (x, y) in `font`.
void StrikeOut(SDL_Renderer* renderer, int font, int x, int y, int width, SDL_Color colour);

// The game's 36 font numbers (a panel's font) as this table's: they are the
// LANGUAGE.DLL entries in an order of their own. Fonts 8 and 28 are struck
// out.
int GameFont(int game_font);
bool GameFontStruck(int game_font);

// Breaks `text` into lines no wider than `width`, at spaces, honouring the
// text's own newlines. The views point into `text`.
void Wrap(int font, std::string_view text, int width, std::vector<std::string_view>& lines);

// The face with the same name and size, bold instead: what a <b> in the help
// text switches to. The font itself when the table has no such face.
int BoldOf(int font);

// The same face at `percent` of its size, rasterised afresh at that size the
// way the rest are -- not the smaller one's pixels enlarged: the Options'
// Text size. The font itself at 100, or when it cannot be opened.
int Scaled(int font, int percent);

} // namespace Text
} // namespace Rendering
