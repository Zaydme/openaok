#include "Text.h"

#include "core/Strings.h"

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_TRUETYPE_TABLES_H

#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <unordered_map>
#include <vector>

namespace Rendering::Text {

namespace {

// The font table's first string, and how many strings each entry takes.
constexpr int kTableFirstString = 110;
constexpr int kStringsPerFont = 3;

bool VdmxMetrics(FT_Face face, int ppem, int& ascent, int& descent);

// GDI's MulDiv(points, 96, 72): rounds half away from zero.
int PixelsForPoints(int points) { return (points * 96 + 36) / 72; }

// One face file per family and style. The CD ships every family the table
// names but Arial; a family missing a style is emboldened or slanted the way
// GDI would simulate it.
struct FaceFile {
    const char* family;
    const char* regular;
    const char* bold;
    const char* italic;
    const char* bold_italic;
};
constexpr FaceFile kFaceFiles[] = {
    {"Georgia", "GEORGIA.TTF", "GEORGIAB.TTF", "GEORGIAI.TTF", "GEORGIAZ.TTF"},
    {"Lucida Blackletter", "LBLACK.TTF", nullptr, nullptr, nullptr},
    {"Lucida Bright", "LBRITE.TTF", "LBRITED.TTF", "LBRITEI.TTF", "LBRITEDI.TTF"},
    {"Lucida Calligraphy", "LCALLIG.TTF", nullptr, nullptr, nullptr},
    {"Arial Narrow", "ARIALN.TTF", "ARIALNB.TTF", "ARIALNI.TTF", "ARIALNBI.TTF"},
    {"Comic Sans MS", "COMIC.TTF", "COMICBD.TTF", nullptr, nullptr},
    {"Papyrus", "PAPYRUS.TTF", nullptr, nullptr, nullptr},
    {"Poor Richard", "POORICH.TTF", nullptr, nullptr, nullptr},
    {"Viner Hand ITC", "VINERITC.TTF", nullptr, nullptr, nullptr},
};

// Arial was a Windows system font, so it is looked for where systems keep it:
// macOS's own, the Microsoft core fonts on Linux, and Liberation Sans -- drawn
// to Arial's metrics -- as the last resort. A phone has none of these: the
// Android build carries its own copy beside the game's data (game/fonts).
constexpr const char* kArialRegular[] = {
    "../fonts/Arial.ttf",
    "/System/Library/Fonts/Supplemental/Arial.ttf",
    "/Library/Fonts/Arial.ttf",
    "/usr/share/fonts/truetype/msttcorefonts/Arial.ttf",
    "/usr/share/fonts/truetype/msttcorefonts/arial.ttf",
    "/usr/share/fonts/TTF/arial.ttf",
    "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
    "/usr/share/fonts/liberation-sans/LiberationSans-Regular.ttf",
};
constexpr const char* kArialBold[] = {
    "../fonts/Arial Bold.ttf",
    "/System/Library/Fonts/Supplemental/Arial Bold.ttf",
    "/Library/Fonts/Arial Bold.ttf",
    "/usr/share/fonts/truetype/msttcorefonts/Arial_Bold.ttf",
    "/usr/share/fonts/truetype/msttcorefonts/arialbd.ttf",
    "/usr/share/fonts/TTF/arialbd.ttf",
    "/usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf",
    "/usr/share/fonts/liberation-sans/LiberationSans-Bold.ttf",
};

struct Glyph {
    SDL_Rect rect{0, 0, 0, 0};   // in the atlas; empty for a blank glyph
    int left = 0;                // bitmap's offset from the pen
    int top = 0;                 // bitmap's top above the baseline
    int advance = 0;
    bool ready = false;
};

struct Face {
    std::string family;
    int points = 0;
    int percent = 100;           // of the size the points make (Scaled)
    bool bold = false;
    bool italic = false;
    FT_Face face = nullptr;
    bool embolden = false;       // bold asked for, and the file has none
    int pixels = 0;
    int ascent = 0;
    int descent = 0;
    // What GDI reports for the face at this size (tmAscent, tmDescent): the
    // font's VDMX entry for the size where it has one, as Windows takes it.
    int gdi_ascent = 0;
    int gdi_descent = 0;
    // GDI's strike-out: its height above the baseline and its thickness.
    int strike_y = 0;
    int strike_h = 1;
    std::array<Glyph, 256> latin{};
    std::unordered_map<uint32_t, Glyph> other;
};

struct State {
    FT_Library library = nullptr;
    SDL_Renderer* renderer = nullptr;
    std::string font_dir;
    std::vector<Face> faces;     // the table first, derived styles after it
    SDL_Texture* atlas = nullptr;
    int atlas_w = 2048;
    int atlas_h = 2048;
    int shelf_x = 0;
    int shelf_y = 0;
    int shelf_h = 0;
};

State& S() {
    static State state;
    return state;
}

std::string FindFile(const std::string& family, bool bold, bool italic, bool& embolden) {
    embolden = false;
    if (family == "Arial") {
        for (const char* path : bold ? kArialBold : kArialRegular) {
            if (FILE* f = std::fopen(path, "rb")) { std::fclose(f); return path; }
        }
        if (bold) {
            embolden = true;
            for (const char* path : kArialRegular) {
                if (FILE* f = std::fopen(path, "rb")) { std::fclose(f); return path; }
            }
        }
        return {};
    }
    for (const FaceFile& file : kFaceFiles) {
        if (family != file.family) continue;
        const char* name = bold && italic ? file.bold_italic
                         : bold           ? file.bold
                         : italic         ? file.italic
                                          : file.regular;
        if (name == nullptr && bold) {
            // The missing weight is simulated, as GDI does.
            embolden = true;
            name = italic && file.italic ? file.italic : file.regular;
        }
        if (name == nullptr) name = file.regular;
        return S().font_dir + "/" + name;
    }
    return {};
}

bool OpenFace(Face& face) {
    bool embolden = false;
    const std::string path = FindFile(face.family, face.bold, face.italic, embolden);
    if (path.empty() || FT_New_Face(S().library, path.c_str(), 0, &face.face) != 0) {
        std::cerr << "[Text] No face for " << face.family << " " << face.points << std::endl;
        face.face = nullptr;
        return false;
    }
    face.embolden = embolden;
    face.pixels = std::max(1, (PixelsForPoints(face.points) * face.percent + 50) / 100);
    FT_Set_Pixel_Sizes(face.face, 0, static_cast<FT_UInt>(face.pixels));
    // GDI's cell comes from the font's Windows metrics, rounded at this size.
    const auto* os2 = static_cast<const TT_OS2*>(FT_Get_Sfnt_Table(face.face, FT_SFNT_OS2));
    const int units = face.face->units_per_EM > 0 ? face.face->units_per_EM : 2048;
    if (os2 != nullptr) {
        face.ascent = (os2->usWinAscent * face.pixels + units / 2) / units;
        face.descent = (os2->usWinDescent * face.pixels + units / 2) / units;
        face.strike_y = (os2->yStrikeoutPosition * face.pixels + units / 2) / units;
        face.strike_h = std::max(1, (os2->yStrikeoutSize * face.pixels + units / 2) / units);
    } else {
        face.strike_y = face.ascent / 3;
        face.ascent = static_cast<int>(face.face->size->metrics.ascender >> 6);
        face.descent = static_cast<int>(-face.face->size->metrics.descender >> 6);
    }
    if (!VdmxMetrics(face.face, face.pixels, face.gdi_ascent, face.gdi_descent)) {
        face.gdi_ascent = face.ascent;
        face.gdi_descent = face.descent;
    }
    return true;
}

// The font's VDMX entry for `ppem` -- the yMax and -yMin Windows uses for
// tmAscent and tmDescent at that size -- if it has one.
bool VdmxMetrics(FT_Face face, int ppem, int& ascent, int& descent) {
    const FT_ULong tag = FT_MAKE_TAG('V', 'D', 'M', 'X');
    FT_ULong length = 0;
    if (FT_Load_Sfnt_Table(face, tag, 0, nullptr, &length) != 0 || length < 6) return false;
    std::vector<uint8_t> table(length);
    if (FT_Load_Sfnt_Table(face, tag, 0, table.data(), &length) != 0) return false;
    auto u16 = [&](size_t at) { return at + 2 <= table.size() ? static_cast<int>(table[at] << 8 | table[at + 1]) : 0; };
    auto s16 = [&](size_t at) { return static_cast<int16_t>(u16(at)); };
    const int ratios = u16(4);
    for (int r = 0; r < ratios; ++r) {
        const size_t group = static_cast<size_t>(u16(6 + 4 * static_cast<size_t>(ratios) + 2 * static_cast<size_t>(r)));
        const int records = u16(group);
        for (int i = 0; i < records; ++i) {
            const size_t rec = group + 4 + 6 * static_cast<size_t>(i);
            if (u16(rec) != ppem) continue;
            ascent = s16(rec + 2);
            descent = -s16(rec + 4);
            return true;
        }
    }
    return false;
}

// Rasterises a glyph into the shared atlas, one bit per pixel, white.
Glyph Render(Face& face, uint32_t codepoint) {
    Glyph glyph;
    glyph.ready = true;
    if (face.face == nullptr) return glyph;
    if (FT_Load_Char(face.face, codepoint,
                     FT_LOAD_RENDER | FT_LOAD_TARGET_MONO | FT_LOAD_MONOCHROME) != 0) {
        return glyph;
    }
    const FT_GlyphSlot slot = face.face->glyph;
    const FT_Bitmap& bitmap = slot->bitmap;
    glyph.advance = static_cast<int>((slot->advance.x + 32) >> 6);
    glyph.left = slot->bitmap_left;
    glyph.top = slot->bitmap_top;
    const int extra = face.embolden ? 1 : 0;
    glyph.advance += extra;
    const int w = static_cast<int>(bitmap.width) + extra;
    const int h = static_cast<int>(bitmap.rows);
    if (w <= extra || h <= 0) return glyph;

    State& s = S();
    if (s.shelf_x + w > s.atlas_w) {
        s.shelf_x = 0;
        s.shelf_y += s.shelf_h + 1;
        s.shelf_h = 0;
    }
    if (s.shelf_y + h > s.atlas_h) return glyph;   // atlas full: a blank
    std::vector<uint32_t> pixels(static_cast<size_t>(w) * h, 0);
    for (int y = 0; y < h; ++y) {
        const uint8_t* row = bitmap.buffer + y * bitmap.pitch;
        for (int x = 0; x < static_cast<int>(bitmap.width); ++x) {
            if (!(row[x >> 3] & (0x80 >> (x & 7)))) continue;
            pixels[static_cast<size_t>(y) * w + x] = 0xFFFFFFFFu;
            if (extra) pixels[static_cast<size_t>(y) * w + x + 1] = 0xFFFFFFFFu;
        }
    }
    glyph.rect = {s.shelf_x, s.shelf_y, w, h};
    SDL_UpdateTexture(s.atlas, &glyph.rect, pixels.data(), w * 4);
    s.shelf_x += w + 1;
    s.shelf_h = std::max(s.shelf_h, h);
    return glyph;
}

const Glyph& GlyphOf(Face& face, uint32_t codepoint) {
    if (codepoint < 256) {
        Glyph& g = face.latin[codepoint];
        if (!g.ready) g = Render(face, codepoint);
        return g;
    }
    auto it = face.other.find(codepoint);
    if (it == face.other.end()) it = face.other.emplace(codepoint, Render(face, codepoint)).first;
    return it->second;
}

// The next code point of a UTF-8 string, advancing `i`. Malformed bytes read
// as themselves, so a stray Latin-1 byte still prints.
uint32_t Next(std::string_view text, size_t& i) {
    const auto c = static_cast<uint8_t>(text[i++]);
    if (c < 0x80) return c;
    int extra = (c & 0xE0) == 0xC0 ? 1 : (c & 0xF0) == 0xE0 ? 2 : (c & 0xF8) == 0xF0 ? 3 : 0;
    if (extra == 0 || i + extra > text.size()) return c;
    uint32_t cp = c & (0x3F >> extra);
    for (int k = 0; k < extra; ++k) {
        const auto b = static_cast<uint8_t>(text[i]);
        if ((b & 0xC0) != 0x80) return c;
        cp = (cp << 6) | (b & 0x3F);
        ++i;
    }
    return cp;
}

Face* FaceOf(int font) {
    auto& faces = S().faces;
    if (font < 0 || font >= static_cast<int>(faces.size())) return nullptr;
    return faces[font].face != nullptr ? &faces[font] : nullptr;
}

// The face `font` names, made bold or not and at `percent` of its points'
// size: one already open, or opened now after the table.
int Derived(int font, bool bold, int percent) {
    auto& faces = S().faces;
    const std::string family = faces[font].family;
    const int points = faces[font].points;
    const bool italic = faces[font].italic;
    for (int i = 0; i < static_cast<int>(faces.size()); ++i) {
        if (faces[i].family == family && faces[i].points == points && faces[i].percent == percent &&
            faces[i].bold == bold && faces[i].italic == italic && faces[i].face != nullptr) {
            return i;
        }
    }
    Face derived;
    derived.family = family;
    derived.points = points;
    derived.percent = percent;
    derived.bold = bold;
    derived.italic = italic;
    if (!OpenFace(derived)) return font;
    faces.push_back(std::move(derived));
    return static_cast<int>(faces.size()) - 1;
}

} // namespace

bool Init(SDL_Renderer* renderer, const std::string& font_dir) {
    State& s = S();
    if (s.library == nullptr && FT_Init_FreeType(&s.library) != 0) {
        std::cerr << "[Text] FreeType failed to start" << std::endl;
        return false;
    }
    s.renderer = renderer;
    s.font_dir = font_dir;
    if (s.atlas == nullptr) {
        s.atlas = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
                                    SDL_TEXTUREACCESS_STATIC, s.atlas_w, s.atlas_h);
        if (s.atlas == nullptr) return false;
        SDL_SetTextureBlendMode(s.atlas, SDL_BLENDMODE_BLEND);
        std::vector<uint32_t> clear(static_cast<size_t>(s.atlas_w) * s.atlas_h, 0);
        SDL_UpdateTexture(s.atlas, nullptr, clear.data(), s.atlas_w * 4);
    }
    s.faces.clear();
    s.faces.resize(kFontCount);
    int opened = 0;
    for (int i = 0; i < kFontCount; ++i) {
        const int base = kTableFirstString + i * kStringsPerFont;
        Face& face = s.faces[i];
        face.family = Core::Strings::Get(base);
        face.points = std::atoi(Core::Strings::Get(base + 1).c_str());
        const std::string& style = Core::Strings::Get(base + 2);
        face.bold = style.find('B') != std::string::npos;
        face.italic = style.find('I') != std::string::npos;
        if (face.family.empty() || face.points <= 0) continue;
        if (OpenFace(face)) ++opened;
    }
    std::cout << "[Text] " << opened << " of " << kFontCount << " fonts open" << std::endl;
    return opened > 0;
}

int Height(int font) {
    const Face* face = FaceOf(font);
    return face != nullptr ? face->ascent + face->descent : 0;
}

int GdiHeight(int font) {
    const Face* face = FaceOf(font);
    return face != nullptr ? face->gdi_ascent + face->gdi_descent : 0;
}

int GdiAscent(int font) {
    const Face* face = FaceOf(font);
    return face != nullptr ? face->gdi_ascent : 0;
}

int Ascent(int font) {
    const Face* face = FaceOf(font);
    return face != nullptr ? face->ascent : 0;
}

int Width(int font, std::string_view text) {
    Face* face = FaceOf(font);
    if (face == nullptr) return 0;
    int width = 0;
    for (size_t i = 0; i < text.size();) {
        const uint32_t cp = Next(text, i);
        if (cp == '\n') break;
        width += GlyphOf(*face, cp).advance;
    }
    return width;
}

namespace {

// Glyphs' quads in the atlas, each with its colour, drawn in one call in the
// order they were added.
struct Quads {
    std::vector<SDL_Vertex> vertices;
    std::vector<int> indices;
};

Quads& Pending() {
    static Quads quads;
    return quads;
}

void AddGlyphs(Face& face, int x, int y, std::string_view text, SDL_Color colour, Quads& quads) {
    const State& s = S();
    const float aw = static_cast<float>(s.atlas_w);
    const float ah = static_cast<float>(s.atlas_h);
    const int baseline = y + face.ascent;
    int pen = x;
    for (size_t i = 0; i < text.size();) {
        const uint32_t cp = Next(text, i);
        if (cp == '\n') break;
        const Glyph& glyph = GlyphOf(face, cp);
        if (glyph.rect.w > 0) {
            const float x0 = static_cast<float>(pen + glyph.left);
            const float y0 = static_cast<float>(baseline - glyph.top);
            const float x1 = x0 + static_cast<float>(glyph.rect.w);
            const float y1 = y0 + static_cast<float>(glyph.rect.h);
            const float u0 = static_cast<float>(glyph.rect.x) / aw;
            const float v0 = static_cast<float>(glyph.rect.y) / ah;
            const float u1 = static_cast<float>(glyph.rect.x + glyph.rect.w) / aw;
            const float v1 = static_cast<float>(glyph.rect.y + glyph.rect.h) / ah;
            const int base = static_cast<int>(quads.vertices.size());
            quads.vertices.push_back({{x0, y0}, colour, {u0, v0}});
            quads.vertices.push_back({{x1, y0}, colour, {u1, v0}});
            quads.vertices.push_back({{x1, y1}, colour, {u1, v1}});
            quads.vertices.push_back({{x0, y1}, colour, {u0, v1}});
            for (int k : {0, 1, 2, 0, 2, 3}) quads.indices.push_back(base + k);
        }
        pen += glyph.advance;
    }
}

void Flush(SDL_Renderer* renderer, Quads& quads) {
    if (!quads.vertices.empty()) {
        SDL_Texture* atlas = S().atlas;
        SDL_SetTextureColorMod(atlas, 255, 255, 255);
        SDL_SetTextureAlphaMod(atlas, 255);
        SDL_RenderGeometry(renderer, atlas, quads.vertices.data(), static_cast<int>(quads.vertices.size()),
                           quads.indices.data(), static_cast<int>(quads.indices.size()));
    }
    quads.vertices.clear();
    quads.indices.clear();
}

} // namespace

void Draw(SDL_Renderer* renderer, int font, int x, int y, std::string_view text,
          SDL_Color colour) {
    Face* face = FaceOf(font);
    if (face == nullptr || text.empty()) return;
    Quads& quads = Pending();
    AddGlyphs(*face, x, y, text, colour, quads);
    Flush(renderer, quads);
}

void DrawShadowed(SDL_Renderer* renderer, int font, int x, int y, std::string_view text,
                  SDL_Color colour, SDL_Color shadow) {
    Face* face = FaceOf(font);
    if (face == nullptr || text.empty()) return;
    Quads& quads = Pending();
    // Down and to the left.
    AddGlyphs(*face, x - 1, y + 1, text, shadow, quads);
    AddGlyphs(*face, x, y, text, colour, quads);
    Flush(renderer, quads);
}

void DrawOutlined(SDL_Renderer* renderer, int font, int x, int y, std::string_view text,
                  SDL_Color colour, SDL_Color outline) {
    Face* face = FaceOf(font);
    if (face == nullptr || text.empty()) return;
    Quads& quads = Pending();
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            if (dx != 0 || dy != 0) AddGlyphs(*face, x + dx, y + dy, text, outline, quads);
        }
    }
    AddGlyphs(*face, x, y, text, colour, quads);
    Flush(renderer, quads);
}

void StrikeOut(SDL_Renderer* renderer, int font, int x, int y, int width, SDL_Color colour) {
    const Face* face = FaceOf(font);
    if (face == nullptr || width <= 0) return;
    SDL_SetRenderDrawColor(renderer, colour.r, colour.g, colour.b, colour.a);
    const SDL_Rect bar = {x, y + face->ascent - face->strike_y, width, face->strike_h};
    SDL_RenderFillRect(renderer, &bar);
}

int GameFont(int game_font) {
    // Each of the game's 36 fonts is a LANGUAGE.DLL font entry, string 110 +
    // 3 x the entry -- the entry is this table's index. Two are struck out.
    static constexpr int kEntry[36] = {0,  1,  8,  9,  2,  -1, 7,  3,  6,  4,  5,  6,
                                       10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21,
                                       22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33};
    return game_font >= 0 && game_font < 36 && kEntry[game_font] >= 0 ? kEntry[game_font] : kArial9;
}

bool GameFontStruck(int game_font) { return game_font == 8 || game_font == 28; }

void Wrap(int font, std::string_view text, int width, std::vector<std::string_view>& lines) {
    lines.clear();
    size_t start = 0;
    while (start <= text.size()) {
        const size_t hard = text.find('\n', start);
        const size_t stop = hard == std::string_view::npos ? text.size() : hard;
        size_t cursor = start;
        if (cursor == stop) lines.push_back(text.substr(cursor, 0));
        while (cursor < stop) {
            // The longest run of whole words that fits; a word too long for
            // the width on its own gets a line to itself.
            size_t line_end = cursor;
            size_t scan = cursor;
            while (scan < stop) {
                size_t word_end = text.find(' ', scan);
                if (word_end == std::string_view::npos || word_end > stop) word_end = stop;
                if (line_end > cursor &&
                    Width(font, text.substr(cursor, word_end - cursor)) > width) {
                    break;
                }
                line_end = word_end;
                scan = word_end + 1;
            }
            lines.push_back(text.substr(cursor, line_end - cursor));
            cursor = line_end;
            while (cursor < stop && text[cursor] == ' ') ++cursor;
        }
        if (hard == std::string_view::npos) break;
        start = hard + 1;
    }
}

int BoldOf(int font) {
    auto& faces = S().faces;
    if (font < 0 || font >= static_cast<int>(faces.size())) return font;
    if (faces[font].bold) return font;
    return Derived(font, true, faces[font].percent);
}

int Scaled(int font, int percent) {
    auto& faces = S().faces;
    if (font < 0 || font >= static_cast<int>(faces.size()) || faces[font].face == nullptr) return font;
    percent = std::clamp(percent, 25, 400);
    if (faces[font].percent == percent) return font;
    return Derived(font, faces[font].bold, percent);
}

} // namespace Rendering::Text
