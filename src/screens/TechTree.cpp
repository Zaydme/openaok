#include "TechTree.h"

#include "TechTreeLayout.h"
#include "Widgets.h"
#include "audio/InterfaceSounds.h"
#include "core/ConfigManager.h"
#include "core/Hotkeys.h"
#include "core/Rules.h"
#include "core/Strings.h"
#include "core/Types.h"
#include "ecs/Diplomacy.h"
#include "ecs/Players.h"
#include "hal/Audio.h"
#include "hal/GraphicCache.h"
#include "rendering/Text.h"

#include <algorithm>
#include <string>
#include <vector>

namespace Screens::TechTree {

namespace Text = Rendering::Text;
namespace L = TechTreeLayout;
using Core::Strings::Get;

namespace {

// ---------------------------------------------------------------------------
// The art (interfac.drs), the screen's .sin and its strings.

constexpr uint32_t kTechNode = 50339;     // technode.slp: the boxes
constexpr uint32_t kTechBack = 50341;     // techback.slp: the parchment strip
constexpr uint32_t kTechAges = 50342;     // techages.slp: the left panel, the shields
constexpr uint32_t kArrows = 53004;       // arrows.slp: the scroll buttons' arrows
constexpr uint32_t kSin = 50007;          // scr6: the screen's colours
constexpr uint32_t kPalette = 50500;

// The boxes in technode: red, blue and green, each lit (researched) and
// dark; then the hatching laid over what the civ cannot have.
constexpr int kNodeHatch = 6;

// LANGUAGE.DLL.
constexpr int kClose = 20101;
constexpr int kAgeNames = 20110;          // Dark, Feudal, Castle, Imperial
constexpr int kAge = 20114;
constexpr int kNotAvailable = 20119;
constexpr int kTechnologies = 20120;
constexpr int kUnits = 20121;
constexpr int kBuildings = 20122;
constexpr int kNotResearched = 20124;
constexpr int kGameCivilizations = 20125;
constexpr int kPlayerSuffix = 20126;      // " (Player)"
constexpr int kAllySuffix = 20127;        // " (Ally)"
constexpr int kResearched = 20128;
constexpr int kCivDescriptions = 20150;   // one per civ, from 1
constexpr int kCivNames = 10230;          // the lobby's civ names, from 1

// The fonts, by the game's font ids: 9 Georgia 8 bold,
// 10 Georgia 9, 11 Georgia 9 bold, 12 Arial Narrow 9, 13 Arial 9.
constexpr int kFontButton = Text::kGeorgia8B;
constexpr int kFontPanel = Text::kGeorgia9;
constexpr int kFontAge = Text::kGeorgia9B_2;
constexpr int kFontNode = Text::kArialNarrow9;
constexpr int kFontLegend = Text::kArial9;

constexpr int kCivCount = 13;

// The left panel's controls, per screen size.
struct Panel {
    int frame;                       // techback's and techages' frame for the size
    int legend_x, legend_y, legend_step, hatch_y, hatch_dx;
    int researched_x, researched_y, researched_w, researched_h, researched_dx;
    int text_x, text_y, text_w, text_h;
    int label_x, label_y, civs_x, civs_y;
    int ages_x, ages_right, ages_dy;
};
constexpr int kLabelW = 230, kLabelH = 25;   // "Game Civilizations" and the drop-down

Panel PanelFor(int h) {
    if (h <= 600) return Panel{2, 167, 470, 22, 3, -30, 35, 430, 80, 40, 80, 30, 115, 270, 300,
                               55, 60, 55, 80, 340, 150, 33};
    if (h <= 768) return Panel{0, 188, 593, 22, 15, -40, 45, 550, 80, 40, 90, 40, 120, 280, 430,
                               65, 65, 65, 85, 345, 152, 33};
    return Panel{1, 200, 855, 23, 15, -45, 50, 814, 80, 40, 100, 40, 125, 290, 660,
                 70, 70, 70, 90, 450, 150, 25};
}

// ---------------------------------------------------------------------------

struct State {
    bool open = false;
    bool in_game = false;
    bool fresh = false;              // in a game: read the player on the next frame
    int shown_civ = 1;
    int player_civ = -1;
    int current_age = -1;            // the player's age, 0-3, for the glowing shield
    L::PlayerView player;
    std::vector<std::string> civ_items;
    L::Tree tree;
    L::Layout layout;
    bool dirty = true;
    int layout_w = 0, layout_h = 0;
    int scroll_x = 0, scroll_y = 0;
    // Input.
    int mx = -1, my = -1;
    bool held = false;
    int pressed = -1;                // what is held: 0 Close, 1 left, 2 right, 3 the list's arrow
    bool list_open = false;
    uint32_t key_tick = 0;
    bool key_scrolling = false;
};

State& S() {
    static State state;
    return state;
}

void Click() { HAL::Audio::Play(Audio::InterfaceSound::kButton2, 1.0f, 0.0f); }

const Theme& SinTheme(int w, int h) {
    static Theme theme;
    static int tw = 0, th = 0;
    if (tw != w || th != h) {
        theme = MakeTheme(kSin, w, h);
        tw = w;
        th = h;
    }
    return theme;
}

// The rules a civ's tech tree reads.
L::CivRules RulesFor(int civ) {
    const auto& t = Core::Rules::Get();
    auto commands = [&](int effect) {
        std::vector<L::Command> out;
        const Core::Rules::Effect* e = Core::Rules::EffectById(effect);
        if (e == nullptr) return out;
        for (const auto& c : e->commands) {
            out.push_back(L::Command{c.type, c.a, c.b, c.d.ToFloat()});
        }
        return out;
    };
    L::CivRules r;
    if (const Core::Rules::CivRules* c = Core::Rules::Civ(civ)) r.tech_tree = commands(c->tech_tree);
    r.tech_civ.assign(t.techs.size(), 0);
    r.tech_effect.assign(t.techs.size(), {});
    for (size_t i = 0; i < t.techs.size(); ++i) {
        if (!t.techs[i].valid) continue;
        r.tech_civ[i] = t.techs[i].civ;
        r.tech_effect[i] = commands(t.techs[i].effect);
    }
    return r;
}

void Rebuild() {
    State& s = S();
    if (L::Master().ages.empty()) L::Load("../assets/extracted_data/techtree.json");
    s.tree = L::ForCiv(s.shown_civ, RulesFor(s.shown_civ));
    if (s.in_game && s.shown_civ == s.player_civ) {
        L::PlayerStatuses(s.tree, s.player);
    } else {
        L::PregameStatuses(s.tree);
    }
    s.dirty = true;
}

void Relayout(int w, int h) {
    State& s = S();
    if (!s.dirty && s.layout_w == w && s.layout_h == h) return;
    const bool own = s.in_game && s.shown_civ == s.player_civ;
    s.layout = L::Build(s.tree, L::MetricsFor(w, h),
                        L::Viewer{s.shown_civ, s.in_game ? s.player_civ : -1, own ? &s.player : nullptr});
    s.layout_w = w;
    s.layout_h = h;
    s.dirty = false;
}

// The scroll limits: the strip's right end may come to
// the screen's right edge, and its left end no further than the left edge.
void ScrollTo(int x, int y, int w, int h) {
    State& s = S();
    s.scroll_x = std::min(0, std::max(x, w - s.layout.width));
    s.scroll_y = std::min(0, std::max(y, h - s.layout.height));
}

std::vector<std::string> CivItems(const entt::registry* registry) {
    std::vector<std::string> items;
    const State& s = S();
    for (int civ = 1; civ <= kCivCount; ++civ) {
        std::string name = Get(kCivNames + civ);
        if (registry != nullptr && s.in_game) {
            if (civ == s.player_civ) {
                name += Get(kPlayerSuffix);
            } else {
                // An ally playing it.
                const auto& table = registry->ctx().get<ECS::Players::Table>();
                for (Core::PlayerId p = 1; p < Core::kMaxPlayers; ++p) {
                    const auto& pl = table.players[p];
                    if (!pl.active || pl.civ != civ || p == Core::LocalPlayer()) continue;
                    if (ECS::Diplomacy::Allied(*registry, Core::LocalPlayer(), p)) {
                        name += Get(kAllySuffix);
                        break;
                    }
                }
            }
        }
        items.push_back(name);
    }
    return items;
}

// The local player's view of its technologies and units.
void ReadPlayer(const entt::registry& registry) {
    State& s = S();
    const ECS::Players::Player* p = ECS::Players::Find(registry, Core::LocalPlayer());
    s.player = L::PlayerView{};
    s.player_civ = p != nullptr ? p->civ : -1;
    s.current_age = -1;
    if (p == nullptr) return;
    s.player.civ = p->civ;
    s.player.tech_state.assign(p->techs.size(), 0);
    for (size_t i = 0; i < p->techs.size(); ++i) {
        switch (p->techs[i].state) {
        case ECS::Players::TechState::Researching: s.player.tech_state[i] = 2; break;
        case ECS::Players::TechState::Done: s.player.tech_state[i] = 3; break;
        default: break;
        }
    }
    s.player.enabled.assign(p->units.size(), 0);
    for (size_t i = 0; i < p->units.size(); ++i) s.player.enabled[i] = p->units[i].enabled ? 1 : 0;
    s.current_age = ECS::Players::Resource(*p, Core::Rules::Res::CurrentAge).ToInt();
}

// ---------------------------------------------------------------------------
// Drawing.

void BlitTo(SDL_Renderer* r, uint32_t slp, int frame, int x, int y, int player = -1) {
    auto& graphics = HAL::GraphicCache::Get();
    const HAL::GraphicAtlas* atlas = graphics.GetInterfaceAtlas(slp, kPalette);
    if (atlas == nullptr || atlas->base == nullptr || frame < 0 ||
        frame >= static_cast<int>(atlas->frames.size())) {
        return;
    }
    const HAL::GraphicFrame& f = atlas->frames[static_cast<size_t>(frame)];
    const SDL_Rect dst = {x - f.hotspot_x, y - f.hotspot_y, f.rect.w, f.rect.h};
    SDL_RenderCopy(r, atlas->base, &f.rect, &dst);
    if (player >= 0) {
        if (SDL_Texture* tint = graphics.GetInterfacePlayerLayer(slp, player)) {
            SDL_RenderCopy(r, tint, &f.rect, &dst);
        }
    }
}

void Fill(SDL_Renderer* r, const L::LineRect& line, int dx, int dy, SDL_Color c) {
    const int x1 = std::min(line.x1, line.x2), x2 = std::max(line.x1, line.x2);
    const int y1 = std::min(line.y1, line.y2), y2 = std::max(line.y1, line.y2);
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, 255);
    const SDL_Rect rect = {x1 + dx, y1 + dy, x2 - x1 + 1, y2 - y1 + 1};
    SDL_RenderFillRect(r, &rect);
}

// A box's colour: red for a building, blue for a
// unit, green for a research, by line mode; lit when researched.
int NodeFrame(const L::Node& n) {
    int colour = 0;
    switch (n.line_mode) {
    case 2: case 3: colour = 1; break;
    case 4: colour = 2; break;
    default: colour = 0; break;
    }
    const int dark = (n.status >= 2 && n.status <= 4) ? 1 : 0;
    return dark + colour * 2;
}

// Its icon: the unit, building or technology sheet; the fortified wall's,
// guard tower's and keep's own boxes show their technologies' icons.
void NodeIcon(const L::Node& n, uint32_t& sheet, int& frame) {
    auto& cfg = Core::ConfigManager::Get();
    const L::Tree& t = S().tree;
    sheet = 0;
    frame = -1;
    if (n.kind == L::Kind::Building) {
        if (n.id == 155) { sheet = cfg.ResearchIconSlp(); frame = 46; return; }
        if (n.id == 234) { sheet = cfg.ResearchIconSlp(); frame = 76; return; }
        if (n.id == 235) { sheet = cfg.ResearchIconSlp(); frame = 16; return; }
        sheet = cfg.BuildingIconSlp();
        if (n.id == 236) { frame = 42; return; }
        for (const auto& b : t.buildings) if (b.id == n.id) { frame = b.icon; return; }
    } else if (n.kind == L::Kind::Research) {
        sheet = cfg.ResearchIconSlp();
        for (const auto& q : t.researches) if (q.id == n.id) { frame = q.icon; return; }
    } else {
        sheet = cfg.UnitIconSlp();
        for (const auto& u : t.units) if (u.id == n.id) { frame = u.icon; return; }
    }
}

// Its label: the tech tree's own wording, split at its line break.
void NodeLabel(const L::Node& n, std::string& line1, std::string& line2) {
    const L::Tree& t = S().tree;
    int id = -1;
    std::string fallback;
    if (n.kind == L::Kind::Research) {
        for (const auto& q : t.researches) {
            if (q.id == n.id) { id = q.name_string + 10000; fallback = q.name; break; }
        }
    } else if (n.kind == L::Kind::Building) {
        for (const auto& b : t.buildings) {
            if (b.id == n.id) { id = b.name_string + 9000; fallback = b.name; break; }
        }
    } else {
        for (const auto& u : t.units) {
            if (u.id == n.id) { id = u.name_string + 9000; fallback = u.name; break; }
        }
    }
    std::string text = id >= 0 ? Get(id) : std::string{};
    if (text.empty()) text = fallback;
    const size_t br = text.find('\n');
    line1 = text.substr(0, br);
    line2 = br == std::string::npos ? std::string{} : text.substr(br + 1);
}

bool Visible(const L::Node& n, int dx, int dy, int w, int h) {
    if (n.x < 0 || n.y < 0) return false;
    const int x = n.x + dx, y = n.y + dy;
    return x + 64 >= 0 && x <= w && y + 64 >= 0 && y <= h;
}

void DrawNodePic(SDL_Renderer* r, const L::Node& n, int dx, int dy) {
    const int x = n.x + dx, y = n.y + dy;
    BlitTo(r, kTechNode, NodeFrame(n), x, y);
    uint32_t sheet = 0;
    int frame = -1;
    NodeIcon(n, sheet, frame);
    if (sheet != 0 && frame >= 0) BlitTo(r, sheet, frame, x + 14, y + 3, 1);
    if (n.status == L::kNotAvailable) BlitTo(r, kTechNode, kNodeHatch, x, y);
}

// ---------------------------------------------------------------------------
// Text, placed by its GDI cell.
//
// The layout's text positions are the tops of GDI text cells, whose ascent
// and descent come from the face's VDMX table. Text measures them from the
// OS/2 table's Windows metrics, which are shorter for these faces at these
// sizes (Arial Narrow at 12 pixels: 11 up and 3 down against VDMX's 13 and
// 3), so each line is drawn the difference lower.
struct GdiCell {
    int font, ascent, height;
};
constexpr GdiCell kGdiCells[] = {
    {kFontNode, 13, 16},     // ARIALN.TTF's VDMX at 12 ppem
    {kFontPanel, 12, 15},    // GEORGIA.TTF's at 12 ppem
    {kFontAge, 12, 15},      // GEORGIAB.TTF's at 12 ppem
    {kFontLegend, 12, 15},   // Arial's at 12 ppem
    {kFontButton, 11, 14},   // GEORGIAB.TTF's at 11 ppem
};

GdiCell CellOf(int font) {
    for (const GdiCell& c : kGdiCells) {
        if (c.font == font) return c;
    }
    return GdiCell{font, Text::Ascent(font), Text::Height(font)};
}

// How far below Text's placement GDI's glyphs sit.
int Drop(int font) { return CellOf(font).ascent - Text::Ascent(font); }

// The font table's line height: the cell and its
// external leading, at least one. None of these faces has any.
int FontHeight(int font) { return CellOf(font).height + 1; }

// One line with its GDI cell's top at (x, y).
void DrawCell(SDL_Renderer* r, int font, int x, int y, std::string_view text, SDL_Color c) {
    Text::Draw(r, font, x, y + Drop(font), text, c);
}

void DrawNodeText(SDL_Renderer* r, const L::Node& n, int dx, int dy) {
    std::string a, b;
    NodeLabel(n, a, b);
    const SDL_Color white = {255, 255, 255, 255};
    const int x = n.x + dx + 2, y = n.y + dy + 3 - 2 + 33;
    // The second line sits a font height less two below the first (the two
    // is LANGUAGE.DLL string 98).
    DrawCell(r, kFontNode, x, y, a, white);
    if (!b.empty()) DrawCell(r, kFontNode, x, y + FontHeight(kFontNode) - 2, b, white);
}

// The left panel's text: black; left-aligned text five pixels in
// from the panel's edge and its border; lines the font's height and two
// apart, from the panel's top. A <b> in a description is a marker, not text.
constexpr int kPanelInset = 5;
int PanelPitch(int font) { return FontHeight(font) + 2; }

std::string Plain(std::string text) {
    for (size_t at; (at = text.find("<b>")) != std::string::npos;) text.erase(at, 3);
    return text;
}

void DrawPanel(SDL_Renderer* r, int font, int x, int y, std::string_view text, SDL_Color c) {
    size_t start = 0;
    for (int line = 0; start <= text.size(); ++line) {
        size_t end = text.find('\n', start);
        if (end == std::string_view::npos) end = text.size();
        DrawCell(r, font, x + kPanelInset, y + line * PanelPitch(font), text.substr(start, end - start), c);
        start = end + 1;
    }
}

// ---------------------------------------------------------------------------
// The civ drop-down: a text field
// 230 less the button and one wide, sunk into the parchment, the civ's name
// in black; a gap of one; an 18-wide button showing btn6_2's arrow; and the
// list, which opens below. Two lines of the .sin's bevel round the field and
// the button, not the buttons' three.
constexpr int kListButtonW = 18;
constexpr uint32_t kButtonArt = 50606;    // btn6_2, the .sin's button_file
constexpr int kListArrow = 14;            // its drop-down arrow; 15 pressed
constexpr int kListBorder = 2;            // the field's border, inside which its text is set
constexpr SDL_Color kListFill = {145, 145, 145, 255};   // palette 50500's index 131

SDL_Rect ListRect(const Panel& p, int dx, int dy) {
    return SDL_Rect{p.civs_x + dx, p.civs_y + dy, kLabelW, kLabelH};
}
SDL_Rect FieldOf(const SDL_Rect& d) { return SDL_Rect{d.x, d.y, d.w - kListButtonW - 1, d.h}; }
SDL_Rect ArrowOf(const SDL_Rect& d) { return SDL_Rect{d.x + d.w - kListButtonW, d.y, kListButtonW, d.h}; }
// Choice `i` while open, a text panel's line each.
SDL_Rect ChoiceOf(const SDL_Rect& d, int i) {
    const int pitch = PanelPitch(kFontPanel);
    return SDL_Rect{d.x + kListBorder, d.y + d.h + kListBorder + i * pitch, d.w - 2 * kListBorder, pitch};
}
SDL_Rect ChoicesOf(const SDL_Rect& d, int count) {
    return SDL_Rect{d.x, d.y + d.h, d.w, count * PanelPitch(kFontPanel) + 2 * kListBorder};
}

void Rect(SDL_Renderer* r, int x, int y, int w, int h, SDL_Color c) {
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, 255);
    const SDL_Rect rect = {x, y, w, h};
    SDL_RenderFillRect(r, &rect);
}

// Two lines of bevel: the top and right first, the left and bottom over them
// where they meet.
void Bevel2(SDL_Renderer* r, const Theme& t, const SDL_Rect& rc, bool sunk) {
    const SDL_Color lit[2] = {t.bevel[0], t.bevel[1]};
    const SDL_Color shade[2] = {t.bevel[5], t.bevel[4]};
    const SDL_Color* top_right = sunk ? shade : lit;
    const SDL_Color* bottom_left = sunk ? lit : shade;
    for (int i = 0; i < 2; ++i) {
        const int x = rc.x + i, y = rc.y + i, w = rc.w - 2 * i, h = rc.h - 2 * i;
        Rect(r, x, y, w, 1, top_right[i]);
        Rect(r, x + w - 1, y, 1, h, top_right[i]);
        Rect(r, x, y, 1, h, bottom_left[i]);
        Rect(r, x, y + h - 1, w, 1, bottom_left[i]);
    }
}

// A button picture: centred on the button, clipped to inside its bevel.
void DrawButtonPicture(SDL_Renderer* r, const SDL_Rect& button, int frame) {
    const HAL::GraphicAtlas* atlas = HAL::GraphicCache::Get().GetInterfaceAtlas(kButtonArt, kPalette);
    if (atlas == nullptr || atlas->base == nullptr || frame >= static_cast<int>(atlas->frames.size())) return;
    const SDL_Rect src = atlas->frames[static_cast<size_t>(frame)].rect;
    const SDL_Rect dst = {button.x + (button.w - src.w) / 2, button.y + (button.h - src.h) / 2, src.w, src.h};
    const SDL_Rect inside = {button.x + kListBorder, button.y + kListBorder, button.w - 2 * kListBorder,
                             button.h - 2 * kListBorder};
    SDL_Rect shown;
    if (!SDL_IntersectRect(&dst, &inside, &shown)) return;
    const SDL_Rect from = {src.x + shown.x - dst.x, src.y + shown.y - dst.y, shown.w, shown.h};
    SDL_RenderCopy(r, atlas->base, &from, &shown);
}

void DrawCivList(SDL_Renderer* r, const Theme& theme, const Panel& p, int dx, int dy) {
    const State& s = S();
    const SDL_Color black = {0, 0, 0, 255};
    const SDL_Rect d = ListRect(p, dx, dy);
    const SDL_Rect field = FieldOf(d), arrow = ArrowOf(d);
    Bevel2(r, theme, field, true);
    const int pitch = PanelPitch(kFontPanel);
    if (s.shown_civ >= 1 && s.shown_civ <= static_cast<int>(s.civ_items.size())) {
        // Centred in the field's height, as a text panel centres one line.
        DrawCell(r, kFontPanel, field.x + kListBorder + kPanelInset, field.y + field.h / 2 - pitch / 2,
                 s.civ_items[static_cast<size_t>(s.shown_civ - 1)], black);
    }
    const bool down = s.held && s.pressed == 3 && Hit(arrow, s.mx, s.my);
    Bevel2(r, theme, arrow, down);
    DrawButtonPicture(r, arrow, kListArrow + (down ? 1 : 0));
    if (!s.list_open) return;

    // Open: the choices on the list's fill (palette index 131), the one under
    // the mouse (or else the current one) framed in the bevel's colours.
    const int count = static_cast<int>(s.civ_items.size());
    const SDL_Rect all = ChoicesOf(d, count);
    Rect(r, all.x, all.y, all.w, all.h, kListFill);
    int lit = s.shown_civ - 1;
    for (int i = 0; i < count; ++i) {
        if (Hit(ChoiceOf(d, i), s.mx, s.my)) lit = i;
    }
    for (int i = 0; i < count; ++i) {
        const SDL_Rect c = ChoiceOf(d, i);
        if (i == lit) Bevel2(r, theme, c, false);
        DrawCell(r, kFontPanel, c.x + kPanelInset, c.y, s.civ_items[static_cast<size_t>(i)], black);
    }
    Bevel2(r, theme, all, false);
}

struct Buttons {
    SDL_Rect close, left, right;
};
Buttons ButtonsFor(int w, int h) {
    return Buttons{SDL_Rect{w - 105, h - 22, 100, 20}, SDL_Rect{w - 280, h - 22, 50, 20},
                   SDL_Rect{w - 220, h - 22, 50, 20}};
}

void DrawButtons(SDL_Renderer* r, const Theme& theme, int w, int h) {
    const State& s = S();
    const Buttons b = ButtonsFor(w, h);
    const SDL_Rect rects[3] = {b.close, b.left, b.right};
    for (int i = 0; i < 3; ++i) {
        const bool down = s.pressed == i && Hit(rects[i], s.mx, s.my);
        DrawBevel(r, theme, rects[i], down);
        const int o = down ? 1 : 0;
        if (i == 0) {
            const bool over = Hit(rects[i], s.mx, s.my);
            DrawTextIn(r, kFontButton, SDL_Rect{rects[i].x + o, rects[i].y + o, rects[i].w, rects[i].h},
                       Get(kClose), over ? theme.focus : theme.text,
                       over ? theme.focus_shadow : theme.text_shadow);
        } else {
            // The arrow, centred: frame 1 points left, 0 right.
            const int frame = i == 1 ? 1 : 0;
            BlitTo(r, kArrows, frame, rects[i].x + rects[i].w / 2 - 9 / 2 + o,
                   rects[i].y + rects[i].h / 2 - 17 / 2 + o);
        }
    }
}

void Scroll(int by, int w, int h) {
    State& s = S();
    ScrollTo(s.scroll_x + by, s.scroll_y, w, h);
}

} // namespace

void OpenPregame(int civ) {
    State& s = S();
    s = State{};
    s.open = true;
    s.in_game = false;
    s.shown_civ = std::clamp(civ, 1, kCivCount);
    s.civ_items = CivItems(nullptr);
    Rebuild();
}

void OpenInGame() {
    State& s = S();
    s = State{};
    s.open = true;
    s.in_game = true;
    s.fresh = true;
}

bool IsOpen() { return S().open; }

void Close() {
    S().open = false;
    S().list_open = false;
}

Result HandleEvent(const SDL_Event& e, int w, int h) {
    State& s = S();
    if (!s.open) return Result::Ignored;
    if (e.type == SDL_MOUSEMOTION) {
        s.mx = e.motion.x;
        s.my = e.motion.y;
        return Result::Taken;
    }
    const Buttons b = ButtonsFor(w, h);
    const Panel p = PanelFor(h);
    if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
        s.held = true;
        s.mx = e.button.x;
        s.my = e.button.y;
        // The drop-down takes the click first, whether it chooses or closes.
        const SDL_Rect d = ListRect(p, s.scroll_x, s.scroll_y);
        if (s.list_open) {
            s.list_open = false;
            Click();
            for (int i = 0; i < static_cast<int>(s.civ_items.size()); ++i) {
                if (!Hit(ChoiceOf(d, i), s.mx, s.my) || i + 1 == s.shown_civ) continue;
                // Another civ's tree starts from its left end.
                s.shown_civ = i + 1;
                s.scroll_x = 0;
                s.scroll_y = 0;
                Rebuild();
            }
            return Result::Taken;
        }
        if (Hit(d, s.mx, s.my)) {
            Click();
            s.list_open = true;
            s.pressed = 3;
            return Result::Taken;
        }
        if (Hit(b.close, s.mx, s.my)) s.pressed = 0;
        else if (Hit(b.left, s.mx, s.my)) s.pressed = 1;
        else if (Hit(b.right, s.mx, s.my)) s.pressed = 2;
        else s.pressed = -1;
        return Result::Taken;
    }
    if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
        s.held = false;
        const int pressed = s.pressed;
        s.pressed = -1;
        const int x = e.button.x, y = e.button.y;
        if (pressed == 0 && Hit(b.close, x, y)) {
            Click();
            Close();
            return Result::Closed;
        }
        // The arrows: 32 a step, times 16, at most 512.
        if (pressed == 1 && Hit(b.left, x, y)) { Click(); Scroll(512, w, h); }
        if (pressed == 2 && Hit(b.right, x, y)) { Click(); Scroll(-512, w, h); }
        return Result::Taken;
    }
    if (e.type == SDL_KEYDOWN) {
        if (s.list_open && e.key.keysym.sym == SDLK_ESCAPE) {
            s.list_open = false;
            return Result::Taken;
        }
        if (e.key.keysym.sym == SDLK_ESCAPE ||
            Core::Hotkeys::Is(e.key, Core::Hotkeys::kGameCommands, Core::Hotkeys::kTechTree)) {
            Close();
            return Result::Closed;
        }
        return Result::Taken;
    }
    return Result::Taken;
}

void Draw(SDL_Renderer* r, const entt::registry* registry, int w, int h) {
    State& s = S();
    if (!s.open) return;
    if (s.fresh) {
        s.fresh = false;
        if (registry != nullptr) {
            ReadPlayer(*registry);
            s.shown_civ = std::clamp(s.player_civ, 1, kCivCount);
        }
        s.civ_items = CivItems(registry);
        Rebuild();
    }
    Relayout(w, h);

    // The arrow keys scroll while held, at 32 pixels each 27 ms.
    {
        const Uint8* keys = SDL_GetKeyboardState(nullptr);
        const int dir = keys[SDL_SCANCODE_LEFT] ? -1 : keys[SDL_SCANCODE_RIGHT] ? 1 : 0;
        const uint32_t now = SDL_GetTicks();
        if (dir == 0) {
            s.key_scrolling = false;
        } else if (!s.key_scrolling || now - s.key_tick >= 27) {
            const int step = s.key_scrolling ? std::min(128, static_cast<int>((now - s.key_tick) / 27.0f * 32.0f))
                                             : 0;
            s.key_scrolling = true;
            s.key_tick = now;
            if (step > 0) Scroll(dir < 0 ? step : -step, w, h);
        }
    }
    ScrollTo(s.scroll_x, s.scroll_y, w, h);

    const Panel p = PanelFor(h);
    const L::Layout& l = s.layout;
    const int dx = s.scroll_x, dy = s.scroll_y;
    SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
    SDL_RenderClear(r);

    // --- The parchment strip, the left panel and the shields at both ends.
    for (int i = 0; i <= l.width / 150; ++i) BlitTo(r, kTechBack, p.frame, dx + 150 * i, dy);
    BlitTo(r, kTechAges, p.frame, dx, dy);
    const int age = s.in_game && s.shown_civ == s.player_civ && s.current_age >= 0 ? s.current_age : 4;
    const int shields = p.frame * 5 + 3 + std::clamp(age, 0, 4);
    const HAL::GraphicAtlas* panel = HAL::GraphicCache::Get().GetInterfaceAtlas(kTechAges, kPalette);
    const int panel_w = panel != nullptr && !panel->frames.empty()
                            ? panel->frames[static_cast<size_t>(p.frame)].rect.w : 340;
    BlitTo(r, kTechAges, shields, dx + panel_w, dy);
    BlitTo(r, kTechAges, shields, dx + l.width - 150, dy);

    // --- The boxes: pictures, then their labels.
    for (int pass = 0; pass < 2; ++pass) {
        for (const L::Column& c : l.columns) {
            if (!c.box_column || !Visible(c.box, dx, dy, w, h)) continue;
            if (pass == 0) DrawNodePic(r, c.box, dx, dy);
            else DrawNodeText(r, c.box, dx, dy);
        }
        for (const L::Column& c : l.columns) {
            for (const L::Node& n : c.cells) {
                if (n.kind == L::Kind::None || n.kind == L::Kind::Through) continue;
                if (!Visible(n, dx, dy, w, h)) continue;
                if (pass == 0) DrawNodePic(r, n, dx, dy);
                else DrawNodeText(r, n, dx, dy);
            }
        }
    }
    // --- The lines, in black.
    const SDL_Color black = {0, 0, 0, 255};
    for (const L::LineRect& line : l.lines) Fill(r, line, dx, dy, black);

    // --- The panel's text, which scrolls with it.
    const Theme& theme = SinTheme(w, h);
    for (int a = 0; a < l.age_count && a < 4; ++a) {
        const int mid = (l.bands[a + 1] - l.bands[a]) / 2 + l.bands[a] + p.ages_dy + Drop(kFontAge);
        for (int side = 0; side < 2; ++side) {
            const int x = side == 0 ? p.ages_x : l.width - p.ages_right;
            DrawText(r, kFontAge, x + dx + 75, mid + dy, Get(kAgeNames + a), black, black, Align::Centre,
                     false);
            DrawText(r, kFontAge, x + dx + 75, mid + 17 + dy, Get(kAge), black, black, Align::Centre, false);
        }
    }
    DrawPanel(r, kFontLegend, p.researched_x + dx, p.researched_y + dy, Get(kNotResearched), black);
    DrawPanel(r, kFontLegend, p.researched_x + p.researched_dx + dx, p.researched_y + dy, Get(kResearched), black);
    DrawPanel(r, kFontLegend, p.legend_x + dx, p.legend_y + dy, Get(kUnits), black);
    DrawPanel(r, kFontLegend, p.legend_x + dx, p.legend_y + p.legend_step + dy, Get(kBuildings), black);
    DrawPanel(r, kFontLegend, p.legend_x + dx, p.legend_y + 2 * p.legend_step + dy, Get(kTechnologies), black);
    DrawPanel(r, kFontLegend, p.legend_x + p.hatch_dx + dx, p.legend_y + 3 * p.legend_step + p.hatch_y + dy,
              Get(kNotAvailable), black);
    DrawPanel(r, kFontPanel, p.label_x + dx, p.label_y + dy, Get(kGameCivilizations), black);
    DrawPanel(r, kFontPanel, p.text_x + dx, p.text_y + dy, Plain(Get(kCivDescriptions + s.shown_civ - 1)), black);
    DrawCivList(r, theme, p, dx, dy);

    // --- The buttons, last, over everything; they do not scroll.
    DrawButtons(r, theme, w, h);
}

} // namespace Screens::TechTree
