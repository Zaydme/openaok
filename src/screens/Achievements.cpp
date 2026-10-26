#include "Achievements.h"

#include "AchievementsTimeline.h"
#include "Sin.h"
#include "Widgets.h"
#include "core/GameSetup.h"
#include "core/Rules.h"
#include "core/Strings.h"
#include "ecs/Players.h"
#include "ecs/Score.h"
#include "ecs/Victory.h"
#include "hal/GraphicCache.h"
#include "rendering/Text.h"
#include "rendering/UIRenderer.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

namespace Achievements {

namespace {

namespace Text = Rendering::Text;
using Core::Strings::Get;
using Screens::Theme;

// --- The art; the timeline panel's is in AchievementsTimeline. ---
constexpr uint32_t kTabsArt = 50765;     // sat_tabs.slp: each tab raised, then lowered
constexpr uint32_t kButtonArt = 50768;   // sat_btn.slp: up, then pressed
constexpr uint32_t kBannerArt = 50762;   // PNBnr1.slp: a banner in each player colour
constexpr uint32_t kTeamArt = 50769;     // AchTeam.slp: teams 1 to 4, then "no team"
constexpr uint32_t kDecalArt = 50766;    // AchDecal.slp: the crown, the medal, the star

// AchDecal's frames: the crown marks a winner and the medal the Most
// Valuable Player (help 42390); the star marks a column's best.
constexpr int kCrown = 5;
constexpr int kMedal = 4;
constexpr int kStar = 6;

// --- Layout, on the 800x600 art. ---
constexpr SDL_Rect kTitle{275, 1, 300, 35};          // 9936, game font 1
constexpr SDL_Rect kPageTitle{320, 28, 200, 25};     // 9938.., game font 4
constexpr SDL_Rect kTime{630, 16, 110, 25};          // game font 11, top right
constexpr SDL_Rect kHelp{65, 487, 450, 55};          // the hover help, font 29
constexpr SDL_Rect kNamesHeading{25, 55, 130, 55};   // 9937 "Player Names"
constexpr SDL_Rect kTeamsHeading{725, 56, 80, 25};   // 9945 "Teams"
constexpr SDL_Rect kNamesArea{5, 85, 125, 390};      // help 42390
constexpr SDL_Rect kTimelinePanel{30, 35, 750, 451};      // the timeline panel; help 42391
constexpr SDL_Rect kExitButton{642, 494, 137, 40};   // game font 30
constexpr SDL_Rect kAgainButton{510, 494, 137, 40};  // 9946 "Play Again"

constexpr int kTabs = 6;
SDL_Rect TabLabel(int i) { return SDL_Rect{80 + 108 * i, 535, 100, 60}; }   // game font 11
// Where each tab's art is drawn.
constexpr SDL_Point kTabArt[kTabs] = {{82, 518}, {190, 521}, {298, 521},
                                      {406, 521}, {515, 520}, {622, 519}};

constexpr int kRows = 8;
constexpr int kColumns = 8;
constexpr int kRowPitch = 52;
SDL_Rect NameRect(int row) { return SDL_Rect{25, 78 + kRowPitch * row, 110, 45}; }
SDL_Rect TeamRect(int row) { return SDL_Rect{733, 89 + kRowPitch * row, 30, 20}; }
SDL_Rect HeadingRect(int c) { return SDL_Rect{140 + 95 * c, 58, 100, 50}; }
SDL_Rect CellRect(int row, int c) { return SDL_Rect{140 + 94 * c, 90 + kRowPitch * row, 105, 25}; }
// Each column's hover area, for its help.
SDL_Rect ColumnArea(int c) { return SDL_Rect{140 + 94 * c, 58, 95, 420}; }
SDL_Point BannerAt(int row) { return SDL_Point{0, 74 + kRowPitch * row}; }
SDL_Point TeamArtAt(int row) { return SDL_Point{730, 82 + kRowPitch * row}; }

// Game font numbers (Text::GameFont).
constexpr int kTitleFont = 1;
constexpr int kPageFont = 4;
constexpr int kTabFont = 11;
constexpr int kBodyFont = 29;
constexpr int kButtonFont = 30;

// Colours. Text is white over a black shadow; the tabs and the buttons turn
// light yellow under the mouse. The figures are drawn unshadowed in a dark
// shade of their player's colour, darker than the game screen's.
constexpr SDL_Color kWhite{255, 255, 255, 255};
constexpr SDL_Color kBlack{0, 0, 0, 255};
constexpr SDL_Color kHover{252, 237, 148, 255};
constexpr SDL_Color kFigureColour[8] = {
    {12, 45, 107, 255}, {107, 9, 6, 255},  {6, 52, 9, 255},   {59, 49, 0, 255},
    {11, 51, 53, 255},  {42, 17, 48, 255}, {4, 2, 1, 255},    {40, 24, 7, 255},
};

// Each page's title, first column heading, first help string, and how many
// columns.
struct PageText {
    int title;
    int first_heading;
    int columns;
    int first_help;
};
constexpr PageText kPages[] = {
    {0, 0, 0, 0},
    {9938, 9886, 5, 42300},   // Score
    {9939, 9896, 6, 42310},   // Military
    {9940, 9906, 6, 42320},   // Economy: the sixth is "sent/received", and its
                              // seventh area's help is 42326
    {9941, 9916, 6, 42330},   // Technology
    {9942, 9926, 6, 42340},   // Society
    {9943, 0, 0, 0},          // Timeline: no columns, no page title
};

// The nearest colour in the screen's 256-colour palette.
SDL_Color Snap(const Theme& theme, SDL_Color c) {
    const auto& palette = HAL::GraphicCache::Get().InterfacePalette(theme.style.palette);
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

Theme& ThemeFor(int w, int h) {
    static Theme theme;
    static int cw = 0, ch = 0;
    if (cw != w || ch != h) {
        theme = Screens::MakeTheme(Screens::Sin::kAchievements, w, h, true);
        cw = w;
        ch = h;
    }
    return theme;
}

SDL_Rect At(const Theme& t, SDL_Rect r) { return SDL_Rect{t.origin.x + r.x, t.origin.y + r.y, r.w, r.h}; }

// One frame of a shape, its hotspot at (x, y) on the art.
void Shape(SDL_Renderer* r, const Theme& t, uint32_t slp, int frame, int x, int y) {
    const HAL::GraphicAtlas* atlas = HAL::GraphicCache::Get().GetInterfaceAtlas(slp, t.style.palette);
    if (atlas == nullptr || atlas->base == nullptr || frame < 0 ||
        frame >= static_cast<int>(atlas->frames.size())) {
        return;
    }
    const HAL::GraphicFrame& f = atlas->frames[frame];
    const SDL_Rect dst{t.origin.x + x - f.hotspot_x, t.origin.y + y - f.hotspot_y, f.rect.w, f.rect.h};
    SDL_RenderCopy(r, atlas->base, &f.rect, &dst);
}

// A text panel's layout: lines a font's height plus two apart; vertically
// from the top or centred on the panel; horizontally from the left edge,
// centred, or ending seven short of the right edge.
enum class V { Centre, Top };
enum class H { Centre, Left, Right };
// A panel's ink: shadowed puts the shadow colour one pixel down and left
// under the text; unshadowed draws the text alone.
struct Ink {
    SDL_Color text;
    SDL_Color shadow;
    bool shadowed = true;
};

// A panel that wraps breaks its text at the longest start that fits the
// panel's width less ten, cut after the last space in it (or, with none, a
// character short of it); the next line goes on from there, any further
// spaces kept, so the column headings' runs of spaces
// ("   Buildings             Razed") set their second word under their
// first. A newline always breaks.
enum class Wrap { No, Yes };

void Lines(int font, std::string_view text, int width, Wrap wrap, std::vector<std::string_view>& out) {
    out.clear();
    while (true) {
        const size_t newline = text.find('\n');
        std::string_view para = text.substr(0, newline);
        while (wrap == Wrap::Yes && para.size() > 1 && Text::Width(font, para) > width) {
            size_t fits = 0;
            while (fits + 1 < para.size() && Text::Width(font, para.substr(0, fits + 1)) <= width) ++fits;
            size_t cut = 0;
            for (size_t i = fits; i-- > 0;) {
                if (para[i] == ' ') {
                    cut = i + 1;
                    break;
                }
            }
            if (cut == 0) cut = std::max<size_t>(1, fits > 0 ? fits - 1 : 1);
            out.push_back(para.substr(0, cut));
            para.remove_prefix(cut);
        }
        out.push_back(para);
        if (newline == std::string_view::npos) break;
        text.remove_prefix(newline + 1);
    }
}

void Panel(SDL_Renderer* r, const Theme& t, const SDL_Rect& box, int game_font,
           std::string_view text, V v, H h, const Ink& ink, Wrap wrap) {
    if (text.empty()) return;
    const int font = Text::GameFont(game_font);
    std::vector<std::string_view> lines;
    Lines(font, text, box.w - 10, wrap, lines);
    const int line_h = Text::Height(font) + 2;
    const int n = static_cast<int>(lines.size());
    const SDL_Rect at = At(t, box);
    for (int i = 0; i < n; ++i) {
        const std::string_view line = lines[static_cast<size_t>(i)];
        const int tw = Text::Width(font, line);
        const int x = h == H::Left ? at.x : h == H::Right ? at.x + at.w - tw - 7 : at.x + at.w / 2 - tw / 2;
        const int y = v == V::Top ? at.y + i * line_h : at.y + at.h / 2 - (n * line_h) / 2 + i * line_h;
        if (ink.shadowed) {
            Text::DrawShadowed(r, font, x, y, line, ink.text, ink.shadow);
        } else {
            Text::Draw(r, font, x, y, line, ink.text);
        }
    }
}

// The hover help: the help string of what is under the mouse, its <b>
// markup set bold, wrapped to the strip from its top left.
void HelpStrip(SDL_Renderer* r, const Theme& t, const std::string& text, const Ink& ink) {
    if (text.empty()) return;
    const int base = Text::GameFont(kBodyFont);
    const SDL_Rect at = At(t, kHelp);
    const int line_h = Text::Height(base) + 2;
    int pen = 0, line = 0;
    bool bold = false;
    std::string word;
    auto flush = [&]() {
        if (word.empty()) return;
        const int font = bold ? Text::BoldOf(base) : base;
        const int width = Text::Width(font, word);
        if (pen > 0 && pen + width > at.w - 10) {
            ++line;
            pen = 0;
            while (!word.empty() && word.front() == ' ') word.erase(word.begin());
        }
        Text::DrawShadowed(r, font, at.x + pen, at.y + line * line_h, word, ink.text, ink.shadow);
        pen += Text::Width(font, word);
        word.clear();
    };
    for (size_t i = 0; i < text.size();) {
        if (text.compare(i, 3, "<b>") == 0) {
            flush();
            bold = !bold;
            i += 3;
            continue;
        }
        if (text[i] == '\n') {
            flush();
            ++line;
            pen = 0;
            ++i;
            while (i < text.size() && text[i] == ' ') ++i;
            continue;
        }
        if (text[i] == ' ' && !word.empty() && word.back() != ' ') flush();
        word += text[i++];
    }
    flush();
}

// --- The figures. ---

// "%2ld:%02ld:%02ld", "%1ld:%02ld:%02ld" or "%02ld:%02ld", by how long; a
// time of nothing is a blank.
std::string AgeTime(int32_t ms) {
    if (ms <= 0) return " ";
    long s = ms / 1000, m = 0, hrs = 0;
    if (s > 59) {
        m = s / 60;
        s %= 60;
        if (m > 59) {
            hrs = m / 60;
            m %= 60;
        }
    }
    char text[32];
    if (hrs >= 10) std::snprintf(text, sizeof(text), "%2ld:%02ld:%02ld", hrs, m, s);
    else if (hrs > 0) std::snprintf(text, sizeof(text), "%1ld:%02ld:%02ld", hrs, m, s);
    else std::snprintf(text, sizeof(text), "%02ld:%02ld", m, s);
    return text;
}

// The game's time at the top right: "%02ld:%02ld:%02ld".
std::string ClockText(int32_t ms) {
    long s = ms / 1000, m = 0, hrs = 0;
    if (s > 59) {
        m = s / 60;
        s %= 60;
        if (m > 59) {
            hrs = m / 60;
            m %= 60;
        }
    }
    char text[32];
    std::snprintf(text, sizeof(text), "%02ld:%02ld:%02ld", hrs, m, s);
    return text;
}

// The share of the technologies a player's civilisation can research that
// it has: the count over those researched at a building that its tech tree
// does not disable.
int ResearchPercent(const ECS::Players::Player& player, int researched) {
    const auto& techs = Core::Rules::Get().techs;
    int total = 0;
    for (size_t id = 0; id < techs.size() && id < player.techs.size(); ++id) {
        if (!techs[id].valid || techs[id].location < 0 || techs[id].button <= 0) continue;
        if (player.techs[id].state == ECS::Players::TechState::Disabled) continue;
        ++total;
    }
    return total > 0 ? researched * 100 / total : 0;
}

struct Row {
    bool present = false;
    Core::PlayerId player = 0;
    std::string cells[kColumns];
    // What each column is compared by for its star, and whether it has one.
    uint32_t value[kColumns] = {};
    bool star[kColumns] = {};
};

// A page's figures for every player, and its stars: the most in each column
// (more than nothing; ties all starred), the earliest for the ages.
void Fill(const ECS::Simulation& sim, int page, std::array<Row, kRows>& rows) {
    const auto& registry = sim.GetRegistry();
    bool lowest[kColumns] = {};
    bool counted[kColumns] = {};
    for (int row = 0; row < kRows; ++row) {
        Row& line = rows[static_cast<size_t>(row)];
        line = Row{};
        const auto p = static_cast<Core::PlayerId>(row + 1);
        const ECS::Score::Sheet* s = ECS::Score::Of(registry, p);
        const ECS::Players::Player* player = ECS::Players::Find(registry, p);
        if (s == nullptr || player == nullptr) continue;
        line.present = true;
        line.player = p;
        auto put = [&](int c, uint32_t v) {
            line.cells[c] = std::to_string(v);
            line.value[c] = v;
            counted[c] = true;
        };
        auto u = [](int32_t v) { return static_cast<uint32_t>(v); };
        switch (page) {
        case kScore:
            line.cells[0] = std::to_string(u(s->military));
            line.cells[1] = std::to_string(u(s->economy));
            line.cells[2] = std::to_string(u(s->technology));
            line.cells[3] = std::to_string(u(s->society));
            put(4, u(s->total));
            break;
        case kMilitary:
            put(0, u(s->units_killed));
            put(1, u(s->units_lost));
            put(2, u(s->buildings_razed));
            put(3, u(s->buildings_lost));
            put(4, u(s->units_converted));
            put(5, u(s->largest_army));
            break;
        case kEconomy:
            put(0, u(s->food_collected));
            put(1, u(s->wood_collected));
            put(2, u(s->stone_collected));
            put(3, u(s->gold_collected));
            put(4, u(s->trade_profit));
            // Sent and received in one cell, "sent/received"; each starred on
            // its own, the received as column 6.
            line.cells[5] = std::to_string(u(s->tribute_sent)) + "/" +
                            std::to_string(u(s->tribute_received));
            line.value[5] = u(s->tribute_sent);
            line.value[6] = u(s->tribute_received);
            counted[5] = counted[6] = true;
            break;
        case kTechnology: {
            const int32_t ages[3] = {s->feudal_ms, s->castle_ms, s->imperial_ms};
            for (int c = 0; c < 3; ++c) {
                line.cells[c] = AgeTime(ages[c]);
                line.value[c] = ages[c] > 0 ? u(ages[c]) : 0;
                counted[c] = lowest[c] = true;
            }
            line.cells[3] = std::to_string(u(s->map_explored)) + "%";
            line.value[3] = u(s->map_explored);
            counted[3] = true;
            put(4, u(s->research_count));
            line.cells[5] = std::to_string(ResearchPercent(*player, s->research_count)) + "%";
            break;
        }
        case kSociety:
            put(0, u(s->wonders));
            put(1, u(s->castles));
            put(2, u(s->relics_captured));
            put(3, u(s->relic_gold));
            put(4, u(s->villager_high));
            line.cells[5] = s->survived != 0 ? Get(4003) : std::string();   // "Yes", or nothing
            break;
        default:
            break;
        }
    }
    for (int c = 0; c < kColumns; ++c) {
        if (!counted[c]) continue;
        uint32_t best = 0;
        for (const Row& line : rows) {
            if (!line.present || line.value[c] == 0) continue;
            if (best == 0 || (lowest[c] ? line.value[c] < best : line.value[c] > best)) {
                best = line.value[c];
            }
        }
        if (best == 0) continue;
        for (Row& line : rows) line.star[c] = line.present && line.value[c] == best;
    }
}

// The Most Valuable Player: on a team, worth more than nothing by military
// and razing value (resources 170 and 172) together, and matched by no
// team-mate seated before it nor beaten by any after.
bool MostValuable(const ECS::Simulation& sim, Core::PlayerId p) {
    const auto& registry = sim.GetRegistry();
    const auto* setup = registry.ctx().find<Core::GameSetup>();
    if (setup == nullptr || setup->players[p].team == 0) return false;
    auto worth = [&](Core::PlayerId q) {
        const ECS::Players::Player* player = ECS::Players::Find(registry, q);
        if (player == nullptr) return 0;
        return ECS::Players::Resource(*player, ECS::Score::Res::MilitaryValue).ToInt() +
               ECS::Players::Resource(*player, ECS::Score::Res::RazingValue).ToInt();
    };
    const int own = worth(p);
    if (own <= 0) return false;
    int most = own;
    bool first = true;
    for (int q = 1; q < Core::kMaxPlayers; ++q) {
        const auto other = static_cast<Core::PlayerId>(q);
        if (ECS::Players::Find(registry, other) == nullptr ||
            setup->players[q].team != setup->players[p].team) {
            continue;
        }
        const int v = worth(other);
        if (v >= most) {
            most = v;
            if (q < p) first = false;
        }
    }
    return most == own && first;
}

// The character width of game font 29: a star is centred by how many fit
// in 85 pixels.
int CharWidth() { return std::max(1, Text::Width(Text::GameFont(kBodyFont), "x")); }

// The exit button's label and help by why the screen opened.
int ExitLabel(const State& s) {
    if (!s.opening.game_over) return 9881;              // Return to Game
    return s.opening.campaign ? 9883 : 9882;            // Scenario Menu, Main Menu
}
int ExitHelp(const State& s) {
    if (!s.opening.game_over) return 42227;
    return s.opening.campaign ? 42228 : 42229;
}
bool HasAgain(const State& s) { return s.opening.game_over && s.opening.single_player; }

bool In(const Theme& t, const SDL_Rect& r, int x, int y) { return Screens::Hit(At(t, r), x, y); }

int TabUnder(const Theme& t, int x, int y) {
    for (int i = 0; i < kTabs; ++i) {
        if (In(t, TabLabel(i), x, y)) return i + 1;
    }
    return 0;
}

// What the help strip says for the point (x, y).
std::string HelpAt(const Theme& t, const State& s, int x, int y) {
    if (const int tab = TabUnder(t, x, y)) return Get(42220 + tab - 1);
    if (In(t, kExitButton, x, y)) return Get(ExitHelp(s));
    if (HasAgain(s) && In(t, kAgainButton, x, y)) return Get(42226);
    if (s.page == kTimeline) {
        return In(t, kTimelinePanel, x, y) ? Get(42391) : std::string();
    }
    const PageText& page = kPages[s.page];
    for (int c = 0; c < kColumns; ++c) {
        if (!In(t, ColumnArea(c), x, y)) continue;
        if (c < page.columns) return Get(page.first_help + c);
        if (s.page == kEconomy && c == page.columns) return Get(42326);
        return std::string();
    }
    if (In(t, kNamesArea, x, y)) return Get(42390);
    return std::string();
}

} // namespace

void Draw(SDL_Renderer* r, ECS::Simulation& sim, int w, int h, const State& s, int mx, int my,
          bool held) {
    const Theme& t = ThemeFor(w, h);
    const auto& registry = sim.GetRegistry();
    // The .sin's background colour round its art, centred (its position 2).
    const auto& palette = HAL::GraphicCache::Get().InterfacePalette(t.style.palette);
    const uint8_t fill = t.style.background_colour;
    if (fill < palette.size()) {
        SDL_SetRenderDrawColor(r, palette[fill].r, palette[fill].g, palette[fill].b, 255);
    } else {
        SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
    }
    SDL_RenderClear(r);
    Screens::DrawBackground(r, t);

    const int page = std::clamp(s.page, static_cast<int>(kScore), static_cast<int>(kTimeline));
    const Ink plain{Snap(t, kWhite), Snap(t, kBlack)};
    const Ink hover{Snap(t, kHover), Snap(t, kBlack)};
    const int hovered_tab = TabUnder(t, mx, my);
    const auto* setup = registry.ctx().find<Core::GameSetup>();
    const ECS::Victory::Outcome* outcome = ECS::Victory::Read(registry);

    // The shapes first: the tabs, raised for the page shown; then, except on
    // the timeline, each player's banner in its colour, its team's plate, and
    // the decorations.
    for (int i = 0; i < kTabs; ++i) {
        Shape(r, t, kTabsArt, 2 * i + (page == i + 1 ? 0 : 1), kTabArt[i].x, kTabArt[i].y);
    }
    std::array<Row, kRows> rows;
    Fill(sim, page, rows);
    if (page == kTimeline) {
        // The timeline panel, the players' histories drawn (AchievementsTimeline).
        DrawTimeline(r, t, sim, t.origin);
    } else {
        const int char_w = CharWidth();
        const int fits = 85 / char_w;
        for (int row = 0; row < kRows; ++row) {
            const Row& line = rows[static_cast<size_t>(row)];
            if (!line.present) continue;
            const int slot = HAL::GraphicCache::Get().ColourSlot(line.player);
            Shape(r, t, kBannerArt, slot, BannerAt(row).x, BannerAt(row).y);
            const int team = setup != nullptr ? setup->players[line.player].team : 0;
            Shape(r, t, kTeamArt, team >= 1 && team <= 4 ? team - 1 : 4, TeamArtAt(row).x,
                  TeamArtAt(row).y);
            const bool won = s.opening.game_over && outcome != nullptr && outcome->Won(line.player);
            const bool mvp = MostValuable(sim, line.player);
            const int name_y = NameRect(row).y;
            if (won) Shape(r, t, kDecalArt, kCrown, 8, name_y);
            if (mvp) Shape(r, t, kDecalArt, kMedal, 8, won ? name_y + 17 : name_y);
            for (int c = 0; c < kColumns; ++c) {
                if (!line.star[c]) continue;
                const SDL_Rect cell = CellRect(row, c);
                if (page == kEconomy && c == 6) {
                    // The received half of "sent/received".
                    const int half = static_cast<int>(line.cells[5].size()) / 2;
                    const int ofs = half > 0 ? char_w * half + 52 : 0;
                    Shape(r, t, kDecalArt, kStar, CellRect(row, 5).x + ofs + 15, cell.y);
                    continue;
                }
                const int spare = (fits - static_cast<int>(line.cells[c].size())) / 2;
                const int ofs = spare > 0 ? char_w * spare : 0;
                Shape(r, t, kDecalArt, kStar, cell.x + ofs - 15, cell.y);
            }
        }
    }

    // Then the text panels, over the art.
    Panel(r, t, kTitle, kTitleFont, Get(9936), V::Centre, H::Centre, plain, Wrap::Yes);
    if (page != kTimeline) {
        Panel(r, t, kPageTitle, kPageFont, Get(kPages[page].title), V::Centre, H::Centre, plain,
              Wrap::Yes);
    }
    const auto* table = registry.ctx().find<ECS::Score::Table>();
    Panel(r, t, kTime, kTabFont, ClockText(table != nullptr ? table->clock_ms : 0), V::Top,
          H::Right, plain, Wrap::No);
    if (page != kTimeline) {
        Panel(r, t, kNamesHeading, kBodyFont, Get(9937), V::Top, H::Left, plain, Wrap::No);
        Panel(r, t, kTeamsHeading, kBodyFont, Get(9945), V::Top, H::Left, plain, Wrap::No);
        const PageText& text = kPages[page];
        for (int c = 0; c < text.columns; ++c) {
            Panel(r, t, HeadingRect(c), kBodyFont, Get(text.first_heading + c), V::Top, H::Left, plain,
                  Wrap::Yes);
        }
        for (int row = 0; row < kRows; ++row) {
            const Row& line = rows[static_cast<size_t>(row)];
            if (!line.present) continue;
            Panel(r, t, NameRect(row), kBodyFont,
                  Rendering::UIRenderer::PlayerName(registry, line.player), V::Centre, H::Left, plain,
                  Wrap::Yes);
            const int team = setup != nullptr ? setup->players[line.player].team : 0;
            if (team >= 1 && team <= 4) {
                Panel(r, t, TeamRect(row), kBodyFont, std::to_string(team), V::Centre, H::Centre, plain,
                      Wrap::No);
            }
            const int slot = HAL::GraphicCache::Get().ColourSlot(line.player);
            const Ink figure{Snap(t, slot >= 0 && slot < 8 ? kFigureColour[slot] : kWhite),
                             kBlack, false};
            for (int c = 0; c < text.columns; ++c) {
                Panel(r, t, CellRect(row, c), kBodyFont, line.cells[c], V::Top, H::Centre, figure,
                      Wrap::Yes);
            }
        }
    }
    for (int i = 0; i < kTabs; ++i) {
        Panel(r, t, TabLabel(i), kTabFont, Get(9871 + i), V::Centre, H::Centre,
              hovered_tab == i + 1 ? hover : plain, Wrap::Yes);
    }
    HelpStrip(r, t, HelpAt(t, s, mx, my), plain);

    // The buttons: sat_btn's frames, up and pressed, and the label centred
    // in game font 30.
    auto button = [&](const SDL_Rect& rect, int label, bool pressed) {
        const bool under = In(t, rect, mx, my);
        Shape(r, t, kButtonArt, pressed && under && held ? 1 : 0, rect.x, rect.y);
        Panel(r, t, rect, kButtonFont, Get(label), V::Centre, H::Centre, under ? hover : plain,
              Wrap::No);
    };
    button(kExitButton, ExitLabel(s), s.pressed_exit);
    if (HasAgain(s)) button(kAgainButton, 9946, s.pressed_again);
}

Exit Mouse(State& s, int w, int h, int x, int y, bool down) {
    const Theme& t = ThemeFor(w, h);
    if (down) {
        // A tab turns the page as the button goes down.
        if (const int tab = TabUnder(t, x, y)) {
            s.page = tab;
            return Exit::None;
        }
        s.pressed_exit = In(t, kExitButton, x, y);
        s.pressed_again = HasAgain(s) && In(t, kAgainButton, x, y);
        return Exit::None;
    }
    const bool exit = s.pressed_exit && In(t, kExitButton, x, y);
    const bool again = s.pressed_again && In(t, kAgainButton, x, y);
    s.pressed_exit = s.pressed_again = false;
    if (exit) {
        if (!s.opening.game_over) return Exit::ReturnToGame;
        return s.opening.campaign ? Exit::CampaignMenu : Exit::MainMenu;
    }
    if (again) return Exit::PlayAgain;
    return Exit::None;
}

} // namespace Achievements
