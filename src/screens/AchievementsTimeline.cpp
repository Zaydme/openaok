#include "AchievementsTimeline.h"

#include "core/Strings.h"
#include "ecs/Players.h"
#include "ecs/Timeline.h"
#include "hal/GraphicCache.h"
#include "rendering/Text.h"
#include "rendering/UIRenderer.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

// The Timeline panel. Its layout is built once per world state: every
// player's readings laid side by side, a column each, across the graph's 689
// pixels (stretched when there are fewer; when there are more, thinned out
// evenly, never dropping a reading something happened at), each column's
// height shared between the players by their part of the world's
// population, and the events on the columns turned into markers. It is drawn
// in order: the tiled background, a bevel, the axes and their captions, the
// bands, the markers and the time axis's figures, the legend, and each
// player's name and civilisation on its band.
//
// The layout is computed in floating point, in screen pixels; the history it
// draws from is the simulation's (ecs/Timeline).
namespace Achievements {

namespace {

namespace Text = Rendering::Text;
namespace T = ECS::Timeline;
using Core::Strings::Get;

constexpr uint32_t kBackgroundArt = 50763;  // tml_bck.slp
constexpr uint32_t kDecalArt = 50766;       // AchDecal.slp
constexpr int kFont = 13;                   // every text in the panel

// The panel on the art.
constexpr int kX = 30, kY = 35, kW = 750, kH = 451;
// The graph in it: its left and top, the frame's width and height, the band
// area's height and the width the readings spread over.
constexpr int kGraphX = kX + 0x28;
constexpr int kGraphY = kY + 0x1a;
constexpr int kGraphW = kW - 0x37;
constexpr int kGraphH = kH - 0x62;
constexpr int kBands = kGraphH - 5;    // 348
constexpr int kSpan = kGraphW - 6;     // 689
// The legend: seven across.
constexpr int kLegendStep = (kW - 0x1b) / 7;
// A time-axis figure left of this is not drawn, clear of "World
// Population".
constexpr int kFiguresFrom = 0xb0;

// Each player colour's two shades: the civilians' light, the soldiers'
// dark; white for any other.
struct Shades {
    SDL_Color civilian;
    SDL_Color military;
};
constexpr Shades kShades[8] = {
    {{103, 148, 203, 255}, {13, 31, 119, 255}},  {{245, 85, 80, 255}, {169, 24, 19, 255}},
    {{124, 170, 133, 255}, {17, 80, 32, 255}},   {{252, 237, 148, 255}, {255, 213, 44, 255}},
    {{140, 168, 170, 255}, {57, 132, 132, 255}}, {{120, 102, 152, 255}, {37, 28, 81, 255}},
    {{208, 208, 207, 255}, {128, 128, 128, 255}}, {{232, 182, 129, 255}, {251, 159, 31, 255}},
};

// The AchDecal frame each event kind is drawn with: the three ages'
// numerals, the Wonder, its ruin, the battle's axe; -1 for the time axis's
// marks. Castles are left undrawn, and a defeat's frame 9 is past the
// shape's last, as is the legend's for it, so neither shows.
int FrameOf(int kind) {
    switch (kind) {
    case 0: return 0;
    case 1: case 10: return 1;
    case 2: return 2;
    case 3: return -1;
    case 4: return 7;
    case 5: return 8;
    case 8: return 13;
    case 11: return 3;
    case 14: return 10;
    case 15: return 11;
    case 19: return 12;
    case 20: return 9;
    default: return 0;
    }
}
constexpr int kTickKind = 3;   // the time axis's marks

struct Column {
    int sample = 0;
    int total = 0;              // the world's population at the reading
    int civilian = 0, military = 0, population = 0;
    int x = 0, y = 0, h = 0;    // the band's piece
    int civilian_h = 0, military_h = 0;
    std::vector<T::Event> events;
};

struct Marker {
    int kind = 0;
    int sample = 0;
    int x = 0, y = 0, h = 0;
    int total = 0;
    bool visible = false;
    int icon_x = 0, icon_y = 0;
};

struct Layout {
    const void* world = nullptr;
    uint64_t tick = ~0ull;
    bool bands = false;                // any readings to draw
    float per_reading = 1.0f;          // pixels per reading, at least one
    int columns = 0;
    std::vector<Core::PlayerId> seats;
    std::vector<Column> cells;         // seat after seat, a column each
    std::vector<Marker> markers;
    // The bands as drawn, a pixel wide each, by player and shade: civilians
    // [0], soldiers [1].
    std::vector<SDL_Rect> runs[Core::kMaxPlayers][2];
    Column& At(int seat, int column) { return cells[static_cast<size_t>(seat * columns + column)]; }
    const Column& At(int seat, int column) const {
        return cells[static_cast<size_t>(seat * columns + column)];
    }
};

const T::Sample* SampleOf(const ECS::Simulation& sim, Core::PlayerId p, int s) {
    const T::Record* r = T::Of(sim.GetRegistry(), p);
    if (r == nullptr || s < 0 || s >= static_cast<int>(r->samples.size())) return nullptr;
    return &r->samples[static_cast<size_t>(s)];
}

// Anything on any list at reading `s`: such a reading is never thinned out.
bool Eventful(const ECS::Simulation& sim, const Layout& L, int s) {
    for (Core::PlayerId p : L.seats) {
        const T::Record* r = T::Of(sim.GetRegistry(), p);
        if (r == nullptr) continue;
        for (const T::Event& e : r->events) {
            if (e.sample == s) return true;
        }
    }
    return false;
}

// One reading into column `c`: the world's population, each player's, and
// its events at the reading (a battle only if kept); and, where the time
// axis is marked, a mark on the cell indexed by the reading (the first
// player's column when nothing is thinned out).
void FillColumn(const ECS::Simulation& sim, Layout& L, int s, int c, bool mark) {
    int total = 0;
    for (Core::PlayerId p : L.seats) {
        if (const T::Sample* x = SampleOf(sim, p, s)) total += x->civilian + x->military;
    }
    for (size_t i = 0; i < L.seats.size(); ++i) {
        Column& o = L.At(static_cast<int>(i), c);
        const T::Sample* x = SampleOf(sim, L.seats[i], s);
        o.sample = s;
        o.total = total;
        o.civilian = x != nullptr ? x->civilian : 0;
        o.military = x != nullptr ? x->military : 0;
        o.population = o.civilian + o.military;
        const T::Record* r = T::Of(sim.GetRegistry(), L.seats[i]);
        if (r == nullptr) continue;
        for (const T::Event& e : r->events) {
            if (e.sample != s) continue;
            if ((e.kind == T::kBattle || e.kind == 12) && e.c != 1) continue;
            o.events.push_back(e);
        }
    }
    if (mark && s >= 0 && s < static_cast<int>(L.cells.size())) {
        T::Event tick;
        tick.kind = kTickKind;
        tick.sample = s;
        L.cells[static_cast<size_t>(s)].events.push_back(tick);
    }
}

// The players' bands in column `c` at `x`: each its part of the
// 348 pixels, two at the least, the fractions carried down, the last player
// taking whatever is left; its civilians' share of it rounded, one at the
// least.
void PlaceColumn(Layout& L, int c, int x) {
    double carried = 0.0;
    int y = 0;
    const int n = static_cast<int>(L.seats.size());
    for (int i = 0; i < n; ++i) {
        Column& o = L.At(i, c);
        int h = 0;
        if (o.population > 0) {
            const double share = static_cast<double>(o.population) / o.total * kBands;
            h = static_cast<int>(share);
            if (h < 2) {
                h = 2;
            } else {
                carried += share - h;
                if (carried >= 1.0) {
                    carried -= 1.0;
                    ++h;
                }
            }
            if (y + h > kBands) h = std::max(0, kBands - y);
        }
        if (i == n - 1) {
            for (int q = n - 1; q >= 0; --q) {
                if (L.At(q, c).population > 0) {
                    h = kBands - y;
                    break;
                }
            }
        }
        o.x = x;
        o.y = kGraphY + y;
        o.h = h;
        o.civilian_h = 0;
        o.military_h = 0;
        if (o.population > 0) {
            if (o.civilian >= 0) {
                o.civilian_h = std::max(1, static_cast<int>(static_cast<double>(o.civilian) /
                                                                 o.population * h + 0.5));
                o.military_h = h - o.civilian_h;
            } else {
                o.military_h = h;
            }
        }
        y += h;
    }
}

// Where a marker's decoration goes: centred on its column, in
// the middle of its band; nowhere if it is taller than the band, or would
// cross the graph's sides.
void Place(Marker& m, const HAL::GraphicAtlas* decal) {
    const int frame = FrameOf(m.kind);
    m.visible = decal != nullptr && frame >= 0 && frame < static_cast<int>(decal->frames.size());
    if (!m.visible) return;
    const int w = decal->frames[static_cast<size_t>(frame)].rect.w;
    const int h = decal->frames[static_cast<size_t>(frame)].rect.h;
    if (w <= 0 || h <= 0) {
        m.visible = false;
        return;
    }
    if (h > m.h + 2) m.visible = false;
    else m.icon_y = m.h / 2 - h / 2 + m.y;
    if (m.x + w / 2 > kGraphX + kSpan || m.x - w / 2 < kGraphX) m.visible = false;
    else m.icon_x = m.x - w / 2;
}

// The interpolation between two columns' heights across the pixels between
// them: nothing from nothing; two at the least at either end; below one, one
// more.
double Between(int a, int b, int n, int i) {
    if (a < 1) return 0.0;
    if (b < 1) b = 1;
    if (a < 2) a = 2;
    if (b < 2) b = 2;
    if (n <= 1) return a;
    if (i >= n) return b;
    double v = a + (static_cast<double>(b) - a) * (static_cast<double>(i) / n);
    if (v < 1.0) v += 1.0;
    return v;
}

// A vertical run: nothing if not a pixel long.
void Run(std::vector<SDL_Rect>& runs, int x, int y, int length) {
    if (length >= 1) runs.push_back(SDL_Rect{x, y, 1, length});
}

// The bands: every pixel across, the players' heights blended
// from one column to the next, the civilians' light shade above the
// soldiers' dark. At a pixel a reading, each column as it was placed.
void Bands(Layout& L) {
    for (auto& seat : L.runs) {
        seat[0].clear();
        seat[1].clear();
    }
    if (!L.bands || L.cells.empty()) return;
    const int n = static_cast<int>(L.seats.size());
    for (int c = 0; c < L.columns; ++c) {
        const bool last = c >= L.columns - 1;
        const int x = L.At(0, c).x;
        int width = (last ? kGraphX + kSpan : L.At(0, c + 1).x) - x;
        if (width <= 0) width = last ? 1 : 0;
        for (int dx = 0; dx < width; ++dx) {
            int y = kGraphY;
            double carried = 0.0;
            for (int i = 0; i < n; ++i) {
                const Column& a = L.At(i, c);
                const Column& b = last ? a : L.At(i, c + 1);
                std::vector<SDL_Rect>* runs = L.runs[i];
                if (L.per_reading <= 1.0f) {
                    Run(runs[0], a.x, a.y, a.civilian_h);
                    Run(runs[1], a.x, a.y + a.civilian_h, a.military_h);
                    continue;
                }
                const double v = Between(a.h, b.h, width, dx);
                int h = static_cast<int>(v);
                carried += v - h;
                if (carried >= 1.0) {
                    carried -= 1.0;
                    ++h;
                }
                if (y + h > kGraphY + kBands) h = kGraphY + kBands - y;
                if (i >= n - 1 && y + h < kGraphY + kBands) h = kGraphY + kBands - y;
                int civ = static_cast<int>(Between(a.civilian_h, b.civilian_h, width, dx));
                if (civ < 1 && b.civilian_h > 0) civ = 1;
                Run(runs[0], x + dx, y, civ);
                Run(runs[1], x + dx, y + civ, h - civ);
                y += h;
            }
        }
    }
}

// The panel's layout, made from the histories.
void Build(const ECS::Simulation& sim, Layout& L, const HAL::GraphicAtlas* decal) {
    L.seats.clear();
    L.cells.clear();
    L.markers.clear();
    for (auto& seat : L.runs) {
        seat[0].clear();
        seat[1].clear();
    }
    const auto& registry = sim.GetRegistry();
    for (int p = 1; p < Core::kMaxPlayers; ++p) {
        if (ECS::Players::Find(registry, static_cast<Core::PlayerId>(p)) != nullptr) {
            L.seats.push_back(static_cast<Core::PlayerId>(p));
        }
    }
    int most = 0;
    for (Core::PlayerId p : L.seats) {
        if (const T::Record* r = T::Of(registry, p)) most = std::max(most, static_cast<int>(r->samples.size()));
    }
    L.bands = most > 0;
    if (most == 0) most = 1;
    // A reading's width; under a pixel, a pixel, and the readings past the
    // 689 thinned out, one in every most/excess.
    L.per_reading = static_cast<float>(kSpan) / static_cast<float>(most);
    int excess = 0;
    float thin = 0.0f;
    if (L.per_reading < 1.0f) {
        L.per_reading = 1.0f;
        L.columns = kSpan;
        excess = most - kSpan;
        thin = static_cast<float>(static_cast<double>(most) / excess);
    } else {
        L.columns = most;
    }
    if (L.seats.empty()) return;
    L.cells.assign(L.seats.size() * static_cast<size_t>(L.columns), Column{});

    // Lay the readings out, thinning where there are too many.
    const int whole = static_cast<int>(thin);
    int gap = std::max(whole - 1, 1);
    const float gap_part = thin - static_cast<float>(whole);
    float gap_carried = 0.0f;
    const int step = static_cast<int>(L.per_reading);
    const float step_part = L.per_reading - static_cast<float>(step);
    float step_carried = 0.0f;
    // The time axis is marked about twelve times.
    const int mark_every =
        L.columns < 12 ? 1 : static_cast<int>((L.columns - 1) * static_cast<double>(0.0833333358168602f));
    int since_mark = 0;
    bool mark = true;
    int dropped = 0;
    int since_kept = 0;
    int x = kGraphX;
    int c = 0;
    for (int s = 0; s < most && c < L.columns; ++s) {
        if (excess > 0 && !Eventful(sim, L, s) && dropped < excess && since_kept >= gap) {
            gap = std::max(whole - 1, 1);
            gap_carried += gap_part;
            if (gap_carried >= 1.0f) {
                gap_carried -= 1.0f;
                ++gap;
            }
            ++dropped;
            since_kept = 0;
            continue;
        }
        FillColumn(sim, L, s, c, mark);
        PlaceColumn(L, c, x);
        step_carried += step_part;
        int advance = step;
        if (step_carried >= 1.0f) {
            step_carried -= 1.0f;
            ++advance;
        }
        x += advance;
        mark = ++since_mark >= mark_every;
        if (mark) since_mark = 0;
        ++since_kept;
        ++c;
    }

    // The markers, column by column, player by player; a Castle's never
    // shows.
    for (int col = 0; col < L.columns; ++col) {
        for (int i = 0; i < static_cast<int>(L.seats.size()); ++i) {
            const Column& o = L.At(i, col);
            for (const T::Event& e : o.events) {
                Marker m;
                m.kind = e.kind;
                m.sample = e.sample;
                m.x = o.x;
                m.y = o.y;
                m.h = o.h;
                m.total = o.total;
                Place(m, decal);
                if (m.kind == T::kCastleBuilt || m.kind == T::kCastleDestroyed || m.kind == 12) {
                    m.visible = false;
                }
                L.markers.push_back(m);
            }
        }
    }
    Bands(L);
}

// --- Drawing. ---

SDL_Color Snap(const Screens::Theme& t, SDL_Color c) {
    const auto& palette = HAL::GraphicCache::Get().InterfacePalette(t.style.palette);
    if (palette.empty()) return c;
    size_t best = 0;
    int best_d = 1 << 30;
    for (size_t i = 0; i < palette.size(); ++i) {
        const int dr = palette[i].r - c.r, dg = palette[i].g - c.g, db = palette[i].b - c.b;
        const int d = dr * dr + dg * dg + db * db;
        if (d < best_d) {
            best_d = d;
            best = i;
        }
    }
    return SDL_Color{palette[best].r, palette[best].g, palette[best].b, 255};
}

void Fill(SDL_Renderer* r, SDL_Point art, int x1, int y1, int x2, int y2, SDL_Color c) {
    if (x2 < x1) std::swap(x1, x2);
    if (y2 < y1) std::swap(y1, y2);
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, 255);
    const SDL_Rect rect{art.x + x1, art.y + y1, x2 - x1 + 1, y2 - y1 + 1};
    SDL_RenderFillRect(r, &rect);
}

// An age's line down its band.
void Line(SDL_Renderer* r, SDL_Point art, int x, int y, int length, SDL_Color c) {
    if (length >= 1) Fill(r, art, x, y, x, y + length - 1, c);
}

// A player's runs of one shade, moved onto the screen and filled at once.
void FillRuns(SDL_Renderer* r, SDL_Point art, const std::vector<SDL_Rect>& runs, SDL_Color c) {
    if (runs.empty()) return;
    static std::vector<SDL_Rect> moved;
    moved.assign(runs.begin(), runs.end());
    for (SDL_Rect& rect : moved) {
        rect.x += art.x;
        rect.y += art.y;
    }
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, 255);
    SDL_RenderFillRects(r, moved.data(), static_cast<int>(moved.size()));
}

void Decal(SDL_Renderer* r, SDL_Point art, const HAL::GraphicAtlas* decal, int frame, int x, int y) {
    if (decal == nullptr || decal->base == nullptr || frame < 0 ||
        frame >= static_cast<int>(decal->frames.size())) {
        return;
    }
    const HAL::GraphicFrame& f = decal->frames[static_cast<size_t>(frame)];
    const SDL_Rect dst{art.x + x - f.hotspot_x, art.y + y - f.hotspot_y, f.rect.w, f.rect.h};
    SDL_RenderCopy(r, decal->base, &f.rect, &dst);
}

// Text by its top left, unshadowed, in the panel's font.
void Say(SDL_Renderer* r, SDL_Point art, int x, int y, std::string_view text, SDL_Color c) {
    Text::Draw(r, Text::GameFont(kFont), art.x + x, art.y + y, text, c);
}

std::string TimeOf(int sample) {
    const long seconds = static_cast<long>(T::kSampleSeconds) * sample;
    long hours = 0, minutes = 0;
    if (seconds > 59) {
        minutes = seconds / 60;
        if (minutes > 59) {
            hours = minutes / 60;
            minutes -= hours * 60;
        }
    }
    char text[32];
    std::snprintf(text, sizeof(text), "%1ld:%02ld", hours, minutes);
    return text;
}

} // namespace

void DrawTimeline(SDL_Renderer* r, const Screens::Theme& theme, const ECS::Simulation& sim,
                  SDL_Point art) {
    auto& cache = HAL::GraphicCache::Get();
    const HAL::GraphicAtlas* background = cache.GetInterfaceAtlas(kBackgroundArt, theme.style.palette);
    const HAL::GraphicAtlas* decal = cache.GetInterfaceAtlas(kDecalArt, theme.style.palette);
    if (decal != nullptr && decal->base == nullptr) decal = nullptr;

    // Made once for the world as it stands; the game stands still behind
    // the screen.
    static Layout layout;
    const void* world = &sim.GetRegistry();
    if (layout.world != world || layout.tick != sim.Tick()) {
        Build(sim, layout, decal);
        layout.world = world;
        layout.tick = sim.Tick();
    }

    const SDL_Rect panel{art.x + kX, art.y + kY, kW, kH};
    SDL_RenderSetClipRect(r, &panel);
    const SDL_Color black = Snap(theme, SDL_Color{0, 0, 0, 255});
    const SDL_Color white = Snap(theme, SDL_Color{255, 255, 255, 255});

    // The background, tiled from the panel's corner.
    if (background != nullptr && background->base != nullptr && !background->frames.empty()) {
        const HAL::GraphicFrame& f = background->frames[0];
        for (int ty = 0; ty < kH; ty += std::max(1, f.rect.h)) {
            for (int tx = 0; tx < kW; tx += std::max(1, f.rect.w)) {
                const SDL_Rect dst{panel.x + tx, panel.y + ty, f.rect.w, f.rect.h};
                SDL_RenderCopy(r, background->base, &f.rect, &dst);
            }
        }
    }

    // The bevel: the screen's first two bevel colours on the top and right,
    // its last two on the bottom and left.
    {
        const int x1 = kX, y1 = kY, x2 = kX + kW - 1, y2 = kY + kH - 1;
        const SDL_Color* bevel = theme.bevel;
        Fill(r, art, x1 + 1, y1, x2, y1, bevel[0]);
        Fill(r, art, x2, y1 + 1, x2, y2 - 1, bevel[0]);
        Fill(r, art, x1 + 2, y1 + 1, x2 - 1, y1 + 1, bevel[1]);
        Fill(r, art, x2 - 1, y1 + 2, x2 - 1, y2 - 2, bevel[1]);
        Fill(r, art, x1, y2, x2, y2, bevel[5]);
        Fill(r, art, x1, y1, x1, y2 - 1, bevel[5]);
        Fill(r, art, x1 + 1, y2 - 1, x2 - 2, y2 - 1, bevel[4]);
        Fill(r, art, x1 + 1, y1 + 1, x1 + 1, y2 - 3, bevel[4]);
    }

    // The frame: black along the top and down the right, a white
    // axis five pixels wide along the bottom and up the left, a tick out to
    // the left at the top; and the captions.
    Fill(r, art, kX + 35, kY + 25, kX + kGraphW + 34, kY + 25, black);
    Fill(r, art, kX + kGraphW + 34, kY + 25, kX + kGraphW + 34, kY + kGraphH + 25, black);
    Fill(r, art, kX + 35, kY + kGraphH + 21, kX + kGraphW + 34, kY + kGraphH + 25, white);
    Fill(r, art, kX + 35, kY + 25, kX + 39, kY + kGraphH + 25, white);
    Fill(r, art, kX + 10, kY + 25, kX + 35, kY + 26, white);
    Say(r, art, kX + 8, kY + kGraphH + 0x1d, Get(11351), white);   // World Population
    Say(r, art, kX + 8, kY + kGraphH + 0x2c, Get(11350), white);   // Time (Hr:Min)
    Say(r, art, kX + 10, kY + 6, Get(11364), white);               // Player's Population Percentage

    // The bands, in each player's shades.
    SDL_Color shades[Core::kMaxPlayers][2];
    for (size_t i = 0; i < layout.seats.size(); ++i) {
        const int slot = cache.ColourSlot(layout.seats[i]);
        const Shades s = slot >= 0 && slot < 8 ? kShades[slot]
                                               : Shades{{255, 255, 255, 255}, {255, 255, 255, 255}};
        shades[i][0] = Snap(theme, s.civilian);
        shades[i][1] = Snap(theme, s.military);
    }
    for (size_t i = 0; i < layout.seats.size(); ++i) {
        FillRuns(r, art, layout.runs[i][0], shades[i][0]);
        FillRuns(r, art, layout.runs[i][1], shades[i][1]);
    }

    // The markers: an age's black line down its band, then each
    // decoration that fits; then the time axis's figures, the world's
    // population and the time, under their marks.
    if (layout.bands) {
        for (const Marker& m : layout.markers) {
            if (m.kind <= 2) Line(r, art, m.x, m.y, m.h, black);
            if (m.visible) Decal(r, art, decal, FrameOf(m.kind), m.icon_x, m.icon_y);
        }
        const int char_w = std::max(1, Text::Width(Text::GameFont(kFont), "x"));
        for (const Marker& m : layout.markers) {
            if (m.kind != kTickKind) continue;
            const std::string people = std::to_string(m.total);
            // A figure sits two pixels left of its centred position.
            const int x = m.x - static_cast<int>(people.size() / 2) * char_w - 2;
            if (x <= kFiguresFrom) continue;
            Say(r, art, x, kY + kGraphH + 0x1d, people, white);
            Say(r, art, x, kY + kGraphH + 0x2c, TimeOf(m.sample), white);
        }
    }

    // The legend: its decorations, and beside each two words, white.
    static constexpr int kLegendFrames[7] = {0, 1, 2, 7, 8, 3, 9};
    static constexpr int kLegendWords[7][2] = {{11360, 11363}, {11361, 11363}, {11362, 11363},
                                               {11352, 11355}, {11352, 11356}, {11353, 11357},
                                               {11354, 11358}};
    for (int k = 0; k < 7; ++k) {
        Decal(r, art, decal, kLegendFrames[k], kX + 0x11 + k * kLegendStep, kY + kH - 0x1a);
        Say(r, art, kX + 0x1f + k * kLegendStep, kY + kH - 0x20, Get(kLegendWords[k][0]), white);
        Say(r, art, kX + 0x1f + k * kLegendStep, kY + kH - 0x14, Get(kLegendWords[k][1]), white);
    }

    // Each player's name and civilisation in black on its band where the
    // graph begins, 170 wide, at twice the panel's left plus six.
    const auto& registry = sim.GetRegistry();
    for (size_t i = 0; i < layout.seats.size() && layout.columns > 0; ++i) {
        const Column& first = layout.At(static_cast<int>(i), 0);
        if (first.h <= 0) continue;
        const Core::PlayerId p = layout.seats[i];
        const ECS::Players::Player* player = ECS::Players::Find(registry, p);
        const int y = first.y + (first.h - 32) / 2;
        const SDL_Rect name_box{art.x + kX + kX + 6, art.y + y, 170, 32};
        SDL_Rect clip;
        SDL_IntersectRect(&name_box, &panel, &clip);
        SDL_RenderSetClipRect(r, &clip);
        Say(r, art, kX + kX + 6, y, Rendering::UIRenderer::PlayerName(registry, p), black);
        if (player != nullptr) Say(r, art, kX + kX + 6, y + 16, Get(10230 + player->civ), black);
        SDL_RenderSetClipRect(r, &panel);
    }
    SDL_RenderSetClipRect(r, nullptr);
}

} // namespace Achievements
