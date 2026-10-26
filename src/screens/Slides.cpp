#include "Slides.h"

#include "hal/Audio.h"
#include "hal/GraphicCache.h"
#include "hal/SlpFile.h"
#include "hal/stb/stb_image.h"
#include "rendering/Text.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <vector>

namespace Screens::Slides {

namespace Text = Rendering::Text;

namespace {

constexpr int kW = 800;
constexpr int kH = 600;
constexpr int kLevels = 20;      // the .bln's tables
constexpr uint32_t kSlides = 1;  // the one entry of a loose SLP's archive

// The script's item types and an item's states.
enum Type : int { kText = 0, kPict = 1, kSnd = 2, kWnd = 3, kMult = 4, kNone = -1 };
enum Phase : int { kWaiting = 0, kFadingIn = 1, kShown = 2, kFadingOut = 3, kOver = 4 };

struct Item {
    int type = kNone;
    int x = 0, y = 0, w = 0, h = 0;
    int start = 0;
    int display = 0;
    int frame = 0;
    std::string text;            // UTF-8
    std::string sound;           // an SND line's file
    SDL_Color colour{0, 0, 0, 255};
    int phase = kWaiting;
    int alpha = 0;               // 0..100, in steps of ten
    uint32_t checked = 0;        // when a sound was last asked whether it plays
};

struct State {
    bool loaded = false;
    bool done = true;
    bool sound = false;
    int fade = 400;              // ms
    int mode = 1;                // 1 a region each, 2 one region
    int end = -1;                // the WND line's time
    std::vector<HAL::PaletteColor> palette;
    std::vector<uint8_t> blend;  // kLevels x 256 x 256
    std::vector<uint8_t> bak, pic, screen;   // palette indexes, kW x kH
    HAL::DrsArchive slides;
    std::vector<Item> items;
    SDL_Texture* texture = nullptr;
    std::vector<uint32_t> argb;
    bool dirty = true;
};

State& S() {
    static State state;
    return state;
}

std::string Upper(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return s;
}
std::string Lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

// Where a file of the show is looked for: the installed game's
// campaign/media, then the CD's GAME/CAMPAIGN/MEDIA. Either spelling, for
// case-sensitive disks.
std::string FindIn(const std::vector<std::string>& dirs, const std::string& name) {
    for (const std::string& dir : dirs) {
        for (const std::string& n : {Lower(name), Upper(name), name}) {
            std::error_code ignored;
            if (std::filesystem::exists(dir + n, ignored)) return dir + n;
        }
    }
    return {};
}
std::string Media(const std::string& name) {
    return FindIn({"../assets/original/Campaign/media/", "../original_game_1999/GAME/CAMPAIGN/MEDIA/"},
                  name);
}
// A script's sound: "campaign\c1s1.mp3" of the game's sound folder.
std::string Narration(const std::string& name) {
    return FindIn({"../assets/original/Sound/campaign/", "../original_game_1999/GAME/SOUND/CAMPAIGN/"},
                  name);
}

bool ReadAll(const std::string& path, std::vector<uint8_t>& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return true;
}

// An SLP on disk as an archive of one, for the archive reader.
bool LoadSlp(const std::string& path, HAL::DrsArchive& out) {
    out = HAL::DrsArchive{};
    if (path.empty() || !ReadAll(path, out.data)) return false;
    out.entries[kSlides] = HAL::DrsEntry{0, static_cast<uint32_t>(out.data.size())};
    return true;
}

// Windows-1252, as the scripts are written, to UTF-8 for the fonts.
std::string Utf8(const std::string& text) {
    static const uint16_t kHigh[32] = {
        0x20AC, 0x81,   0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160,
        0x2039, 0x0152, 0x8D,   0x017D, 0x8F,   0x90,   0x2018, 0x2019, 0x201C, 0x201D, 0x2022,
        0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x9D,   0x017E, 0x0178};
    std::string out;
    for (unsigned char c : text) {
        uint32_t u = c >= 0x80 && c < 0xA0 ? kHigh[c - 0x80] : c;
        if (u < 0x80) {
            out += static_cast<char>(u);
        } else if (u < 0x800) {
            out += static_cast<char>(0xC0 | (u >> 6));
            out += static_cast<char>(0x80 | (u & 0x3F));
        } else {
            out += static_cast<char>(0xE0 | (u >> 12));
            out += static_cast<char>(0x80 | ((u >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (u & 0x3F));
        }
    }
    return out;
}

// The palette entry nearest a colour.
uint8_t Nearest(const std::vector<HAL::PaletteColor>& palette, SDL_Color c) {
    int best = 0, best_d = 1 << 30;
    for (size_t i = 0; i < palette.size(); ++i) {
        const int dr = palette[i].r - c.r, dg = palette[i].g - c.g, db = palette[i].b - c.b;
        const int d = dr * dr + dg * dg + db * db;
        if (d < best_d) { best_d = d; best = static_cast<int>(i); }
    }
    return static_cast<uint8_t>(best);
}

// The .sin's art, palette and text colour, from its own lines: a name with
// id -1 is a file beside it (campaign\media\backgrd1 -> backgrd1.slp, .pal),
// a palette with an id the interface archive's.
struct Look {
    std::string background;
    std::string palette_file;
    uint32_t palette_id = 0;
    SDL_Color text{0, 0, 0, 255};
};

std::string BaseName(const std::string& path) {
    const size_t slash = path.find_last_of("\\/");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

bool ReadSin(const std::string& path, Look& look) {
    std::vector<uint8_t> bytes;
    if (path.empty() || !ReadAll(path, bytes)) return false;
    std::istringstream lines(std::string(bytes.begin(), bytes.end()));
    std::string line;
    while (std::getline(lines, line)) {
        std::istringstream in(line);
        std::string key, name;
        in >> key;
        if (key == "background1_files") {
            std::string none;
            in >> name >> none;
            look.background = BaseName(name) + ".slp";
        } else if (key == "palette_file") {
            long id = -1;
            in >> name >> id;
            if (id > 0) look.palette_id = static_cast<uint32_t>(id);
            else look.palette_file = BaseName(name) + ".pal";
        } else if (key == "text_color1") {
            int r = 0, g = 0, b = 0;
            in >> r >> g >> b;
            look.text = SDL_Color{static_cast<uint8_t>(r), static_cast<uint8_t>(g),
                                  static_cast<uint8_t>(b), 255};
        }
    }
    return !look.background.empty();
}

// A .bln: a float, then the twenty tables, deflated.
bool ReadBlend(const std::string& path, std::vector<uint8_t>& out) {
    std::vector<uint8_t> packed;
    if (path.empty() || !ReadAll(path, packed)) return false;
    int size = 0;
    char* raw = stbi_zlib_decode_noheader_malloc(reinterpret_cast<const char*>(packed.data()),
                                                 static_cast<int>(packed.size()), &size);
    if (raw == nullptr) return false;
    const size_t want = static_cast<size_t>(kLevels) * 256 * 256;
    const bool ok = static_cast<size_t>(size) >= 4 + want;
    if (ok) out.assign(raw + 4, raw + 4 + want);
    std::free(raw);
    return ok;
}

// The script: a line's words by their place, the text in quotes.
std::vector<Item> ReadScript(const std::string& path, const Look& look, int fade) {
    std::vector<Item> items;
    std::vector<uint8_t> bytes;
    if (!ReadAll(path, bytes)) return items;
    std::istringstream lines(std::string(bytes.begin(), bytes.end()));
    std::string line;
    while (std::getline(lines, line)) {
        // The words, a quoted run counting as one.
        std::vector<std::string> words;
        for (size_t i = 0; i < line.size();) {
            const unsigned char c = static_cast<unsigned char>(line[i]);
            if (c == ' ' || c == '\t' || c == '\r') { ++i; continue; }
            if (c == '"') {
                const size_t close = line.find('"', i + 1);
                words.push_back(line.substr(i + 1, close == std::string::npos ? std::string::npos
                                                                              : close - i - 1));
                i = close == std::string::npos ? line.size() : close + 1;
                continue;
            }
            size_t end = i;
            while (end < line.size() && line[end] != ' ' && line[end] != '\t' && line[end] != '\r') ++end;
            words.push_back(line.substr(i, end - i));
            i = end;
        }
        if (words.size() < 9 || !std::isdigit(static_cast<unsigned char>(words[0][0]))) continue;
        Item item;
        const std::string& type = words[1];
        item.type = type == "TEXT" ? kText : type == "PICT" ? kPict : type == "SND" ? kSnd
                  : type == "WND" ? kWnd : type == "MULT" ? kMult : kNone;
        if (item.type == kNone) continue;
        auto number = [&](size_t i) { return i < words.size() ? std::atoi(words[i].c_str()) : 0; };
        item.x = number(2);
        item.y = number(3);
        item.w = number(4);
        item.h = number(5);
        item.start = number(6);
        item.display = number(7);
        item.frame = number(8);
        const std::string str = words.size() > 9 ? words[9] : std::string{};
        if (item.type == kText) item.text = Utf8(str);
        if (item.type == kSnd) item.sound = str;
        item.colour = SDL_Color{static_cast<uint8_t>(number(10)), static_cast<uint8_t>(number(11)),
                                static_cast<uint8_t>(number(12)), 255};
        // Kept on the screen, faded out early, and in the screen's own text
        // colour when the script gives black.
        if (item.x + item.w - 1 > kW) item.w = kW - item.x;
        if (item.y + item.h - 1 > kH) item.h = kH - item.y;
        if (fade > 1 && (item.type == kText || item.type == kPict)) item.display -= 100 + fade;
        if (item.colour.r == 0 && item.colour.g == 0 && item.colour.b == 0) item.colour = look.text;
        items.push_back(std::move(item));
    }
    return items;
}

// An SLP frame's opaque pixels into a buffer of indexes, its hotspot on the point.
void Stamp(const HAL::DrsArchive& slp, int frame, int x, int y, std::vector<uint8_t>& into,
           int* w = nullptr, int* h = nullptr) {
    HAL::SlpFrame f;
    if (!HAL::SlpDecodeFrame(slp, kSlides, static_cast<uint32_t>(frame), f)) return;
    if (w != nullptr) *w = static_cast<int>(f.width);
    if (h != nullptr) *h = static_cast<int>(f.height);
    const int left = x - f.hotspot_x, top = y - f.hotspot_y;
    for (uint32_t row = 0; row < f.height; ++row) {
        const int sy = top + static_cast<int>(row);
        if (sy < 0 || sy >= kH) continue;
        for (uint32_t col = 0; col < f.width; ++col) {
            const int sx = left + static_cast<int>(col);
            if (sx < 0 || sx >= kW) continue;
            const size_t i = row * f.width + col;
            if (f.alpha[i] != 0) into[static_cast<size_t>(sy) * kW + sx] = f.indexes[i];
        }
    }
}

// A text's font, by the script's frame: the game's fonts 15, 16,
// 20, 21 and 23 -- Lucida Blackletter 12 bold, Viner Hand 16, Lucida
// Calligraphy 14, Poor Richard 14, Lucida Bright 12 bold italic.
int FontOf(int frame) {
    static constexpr int kGame[5] = {15, 16, 20, 21, 23};
    return Text::GameFont(frame >= 0 && frame < 5 ? kGame[frame] : 15);
}

// The text drawn into the pictures' buffer: from the rectangle's top left, a
// line the font's height, words wrapped to the rectangle less a pixel,
// clipped to it.
void Write(SDL_Renderer* r, const Item& item, std::vector<uint8_t>& into, uint8_t index) {
    const int font = FontOf(item.frame);
    const int w = std::max(1, item.w - 1), h = std::max(1, item.h - 1);
    std::vector<std::string_view> lines;
    Text::Wrap(font, item.text, w, lines);
    SDL_Texture* target = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, w, h);
    if (target == nullptr) return;
    SDL_Texture* previous = SDL_GetRenderTarget(r);
    SDL_Rect viewport;
    SDL_RenderGetViewport(r, &viewport);
    SDL_SetRenderTarget(r, target);
    SDL_SetRenderDrawColor(r, 0, 0, 0, 0);
    SDL_RenderClear(r);
    const int pitch = Text::GdiHeight(font);
    const int drop = Text::GdiAscent(font) - Text::Ascent(font);
    for (size_t i = 0; i < lines.size(); ++i) {
        Text::Draw(r, font, 0, static_cast<int>(i) * pitch + drop, lines[i],
                   SDL_Color{255, 255, 255, 255});
    }
    std::vector<uint32_t> pixels(static_cast<size_t>(w) * h);
    SDL_RenderReadPixels(r, nullptr, SDL_PIXELFORMAT_ARGB8888, pixels.data(), w * 4);
    SDL_SetRenderTarget(r, previous);
    SDL_RenderSetViewport(r, &viewport);
    SDL_DestroyTexture(target);
    for (int py = 0; py < h; ++py) {
        const int sy = item.y + py;
        if (sy < 0 || sy >= kH) continue;
        for (int px = 0; px < w; ++px) {
            const int sx = item.x + px;
            if (sx < 0 || sx >= kW) continue;
            if ((pixels[static_cast<size_t>(py) * w + px] >> 24) >= 0x80) {
                into[static_cast<size_t>(sy) * kW + sx] = index;
            }
        }
    }
}

SDL_Rect Clip(const SDL_Rect& r) {
    const int x0 = std::max(0, r.x), y0 = std::max(0, r.y);
    const int x1 = std::min(kW, r.x + r.w), y1 = std::min(kH, r.y + r.h);
    return SDL_Rect{x0, y0, std::max(0, x1 - x0), std::max(0, y1 - y0)};
}

void Copy(const std::vector<uint8_t>& from, std::vector<uint8_t>& to, const SDL_Rect& rect) {
    const SDL_Rect r = Clip(rect);
    for (int y = r.y; y < r.y + r.h; ++y) {
        std::copy_n(from.begin() + static_cast<size_t>(y) * kW + r.x, r.w,
                    to.begin() + static_cast<size_t>(y) * kW + r.x);
    }
}

// The pictures over the art at a strength, through the .bln's table of it:
// a fifth of the strength, the twentieth table at most.
void Blend(State& s, const SDL_Rect& rect, int alpha) {
    const int level = std::min(alpha / 5, kLevels - 1);
    const uint8_t* table = s.blend.data() + static_cast<size_t>(level) * 256 * 256;
    const SDL_Rect r = Clip(rect);
    for (int y = r.y; y < r.y + r.h; ++y) {
        for (int x = r.x; x < r.x + r.w; ++x) {
            const size_t i = static_cast<size_t>(y) * kW + x;
            s.screen[i] = table[s.pic[i] * 256 + s.bak[i]];
        }
    }
}

SDL_Rect RectOf(const Item& item) { return SDL_Rect{item.x, item.y, item.w, item.h}; }

void StartSound(State& s, Item& item, uint32_t now) {
    const std::string path = s.sound ? Narration(item.sound) : std::string{};
    if (!path.empty() && HAL::Audio::PlayNarration(path)) {
        item.phase = kShown;
        item.checked = now;
    } else {
        item.phase = kOver;
    }
}

// The items' times.
void Update(State& s, uint32_t now) {
    const int t = static_cast<int>(now);
    for (Item& item : s.items) {
        if (item.start <= t && item.phase == kWaiting) {
            switch (item.type) {
            case kText:
            case kPict: item.phase = kFadingIn; item.alpha = 0; break;
            case kSnd: StartSound(s, item, now); break;
            default: item.phase = kShown; break;
            }
        }
        // A sound is asked once a second whether it still plays.
        if (item.type == kSnd && item.phase == kShown && now - item.checked >= 1000) {
            if (!HAL::Audio::NarrationPlaying()) item.phase = kOver;
            item.checked = now;
        }
        const int end = item.start + item.display;
        if (end <= t && (item.phase == kFadingIn || item.phase == kShown)) {
            if (item.type == kText || item.type == kPict) {
                item.phase = kFadingOut;
                item.alpha = 100;
            } else if (item.type != kSnd) {
                item.phase = kOver;
            }
        }
        if (item.phase == kFadingIn && item.alpha > 0) {
            item.alpha = (t - item.start) * 10 / s.fade * 10 + 1;
        }
        if (item.phase == kFadingOut && item.alpha < 100) {
            const int since = t - end;
            item.alpha = since >= s.fade ? 0 : 99 - since * 10 / s.fade * 10;
        }
    }
    if (s.end >= 0 && t > s.end) s.done = true;
}

// The screen: what fades out, what is new drawn into the
// pictures' buffer, what fades in.
void Compose(State& s, SDL_Renderer* r) {
    const bool fades = s.fade > 1;
    if (s.mode == 1) {
        for (Item& item : s.items) {
            if (item.phase != kFadingOut) continue;
            if (item.alpha == 100) item.alpha = fades ? 99 : 0;
            if (item.alpha > 0) {
                Blend(s, RectOf(item), item.alpha);
            } else {
                Copy(s.bak, s.screen, RectOf(item));
                Copy(s.bak, s.pic, RectOf(item));
                item.phase = kOver;
            }
            s.dirty = true;
        }
    }
    bool showing = false;
    for (Item& item : s.items) {
        if (item.phase != kFadingIn || item.type != kPict) continue;
        showing = true;
        if (item.alpha != 0) continue;
        if (s.mode == 2) s.pic = s.bak;
        Stamp(s.slides, item.frame, item.x, item.y, s.pic, &item.w, &item.h);
        item.alpha = fades ? 1 : 100;
    }
    for (Item& item : s.items) {
        if (item.phase != kFadingIn || item.type != kText) continue;
        showing = true;
        if (item.alpha != 0) continue;
        if (r != nullptr) Write(r, item, s.pic, Nearest(s.palette, item.colour));
        item.alpha = fades ? 1 : 100;
    }
    if (!showing) return;
    if (s.mode == 1) {
        for (Item& item : s.items) {
            if (item.phase != kFadingIn || item.alpha <= 1) continue;
            if (item.alpha < 100) {
                Blend(s, RectOf(item), item.alpha);
            } else {
                Copy(s.pic, s.screen, RectOf(item));
                item.phase = kShown;
            }
            s.dirty = true;
        }
        return;
    }
    // Mode 2: the region of everything fading in, at the last one's strength.
    int x0 = -1, y0 = -1, x1 = -1, y1 = -1, alpha = -1;
    for (Item& item : s.items) {
        if (item.phase != kFadingIn || item.alpha <= 1) continue;
        if (x0 == -1 || item.x < x0) x0 = item.x;
        if (y0 == -1 || item.y < y0) y0 = item.y;
        if (x1 == -1 || item.x + item.w - 1 > x1) x1 = item.x + item.w - 1;
        if (y1 == -1 || item.y + item.h - 1 > y1) y1 = item.y + item.h - 1;
        if (item.alpha >= 100) item.phase = kShown;
        alpha = item.alpha;
    }
    if (alpha < 0) return;
    const SDL_Rect region = {x0, y0, x1 - x0 + 1, y1 - y0 + 1};
    if (alpha >= 100) Copy(s.pic, s.screen, region);
    else Blend(s, region, alpha);
    s.dirty = true;
}

bool LoadShow(State& s, const Show& show) {
    const bool custom = show.campaign == 0;
    Look look;
    const std::string sin = custom ? show.name + ".sin"
                                   : "backgrd" + std::to_string(show.campaign) + ".sin";
    if (!ReadSin(Media(sin), look)) return false;
    // The palette: the .sin's own file, or the interface archive's.
    if (look.palette_id != 0) {
        s.palette = HAL::GraphicCache::Get().InterfacePalette(look.palette_id);
    } else if (!HAL::LoadJascPalette(Media(look.palette_file), s.palette)) {
        return false;
    }
    if (s.palette.size() < 256) s.palette.resize(256);
    HAL::DrsArchive art;
    if (!LoadSlp(Media(look.background), art)) return false;
    s.bak.assign(static_cast<size_t>(kW) * kH, 0);
    Stamp(art, 0, 0, 0, s.bak);
    const std::string bln = custom ? show.name + ".bln" : "cam" + std::to_string(show.campaign) + ".bln";
    if (!ReadBlend(Media(bln), s.blend)) return false;
    if (!LoadSlp(Media(show.name + ".slp"), s.slides)) return false;
    s.fade = custom ? 500 : 400;
    s.mode = custom ? 2 : 1;
    s.items = ReadScript(Media(show.name + ".mm"), look, s.fade);
    if (s.items.empty()) return false;
    s.end = -1;
    for (const Item& item : s.items) {
        if (item.type == kWnd) s.end = item.display;
    }
    s.pic = s.bak;
    s.screen = s.bak;
    return true;
}

SDL_Renderer* g_renderer = nullptr;

} // namespace

Show OfScenario(int campaign, int scenario, bool beginning) {
    char name[32];
    std::snprintf(name, sizeof(name), beginning ? "c%ds%d_beg" : "c%ds%d_end", campaign, scenario);
    return Show{name, campaign};
}

Show Custom(const std::string& name) { return Show{name, 0}; }

bool Start(const Show& show, bool sound, SDL_Renderer* r) {
    Stop();
    g_renderer = r;
    State& s = S();
    SDL_Texture* texture = s.texture;
    s = State{};
    s.texture = texture;
    s.sound = sound;
    s.loaded = LoadShow(s, show);
    s.done = !s.loaded;
    s.dirty = true;
    return s.loaded;
}

void Advance(uint32_t ms) {
    State& s = S();
    if (!s.loaded || s.done) return;
    Update(s, ms);
    Compose(s, g_renderer);
    if (s.sound) HAL::Audio::PumpNarration();
}

void Skip() { S().done = true; }

bool Done() { return S().done; }

void Draw(SDL_Renderer* r) {
    State& s = S();
    if (!s.loaded) return;
    if (s.texture == nullptr) {
        s.texture = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, kW, kH);
        s.dirty = true;
    }
    if (s.texture == nullptr) return;
    if (s.dirty) {
        s.argb.resize(static_cast<size_t>(kW) * kH);
        for (size_t i = 0; i < s.argb.size(); ++i) {
            const HAL::PaletteColor& c = s.palette[s.screen[i]];
            s.argb[i] = 0xFF000000u | (uint32_t(c.r) << 16) | (uint32_t(c.g) << 8) | c.b;
        }
        SDL_UpdateTexture(s.texture, nullptr, s.argb.data(), kW * 4);
        s.dirty = false;
    }
    const SDL_Rect dst = {0, 0, kW, kH};
    SDL_RenderCopy(r, s.texture, nullptr, &dst);
}

namespace {
bool g_intro_armed = true;
}

void ArmIntro() { g_intro_armed = true; }

bool TakeIntro() {
    const bool armed = g_intro_armed;
    g_intro_armed = false;
    return armed;
}

void Stop() {
    State& s = S();
    if (s.sound) HAL::Audio::StopNarration();
    s.sound = false;
    s.loaded = false;
    s.done = true;
}

} // namespace Screens::Slides
