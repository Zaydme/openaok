#include "Campaigns.h"

#include "Slides.h"
#include "Widgets.h"
#include "audio/InterfaceSounds.h"
#include "core/Profile.h"
#include "core/Strings.h"
#include "hal/Audio.h"
#include "hal/GraphicCache.h"
#include "hal/SlpFile.h"
#include "rendering/Text.h"
#include "scenario/Campaign.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

namespace Screens::Campaigns {

namespace Text = Rendering::Text;
using Core::Strings::Get;

namespace {

void Click() { HAL::Audio::Play(Audio::InterfaceSound::kButton2, 1.0f, 0.0f); }

constexpr const char* kCampaignDir = "../assets/original/Campaign/";

// A frame of a screen's own art (the campaign screens keep their emblems
// and markers in the background's SLP, after the background), its hotspot
// on the point.
void DrawFrame(SDL_Renderer* r, const Theme& theme, int frame, int x, int y) {
    const HAL::GraphicAtlas* atlas =
        HAL::GraphicCache::Get().GetInterfaceAtlas(theme.background, theme.style.palette);
    if (atlas == nullptr || atlas->base == nullptr || frame <= 0 ||
        frame >= static_cast<int>(atlas->frames.size())) {
        return;
    }
    const HAL::GraphicFrame& f = atlas->frames[static_cast<size_t>(frame)];
    const SDL_Rect dst = {x - f.hotspot_x, y - f.hotspot_y, f.rect.w, f.rect.h};
    SDL_RenderCopy(r, atlas->base, &f.rect, &dst);
}

// A button with the focus: its label lit as though the mouse were over it.
void DrawFocused(SDL_Renderer* r, const Theme& theme, const Button& b, const Pointer& p) {
    State state = StateOf(b, p.x, p.y, p.held);
    if (state == State::Normal) state = State::Hover;
    DrawButton(r, theme, b, state);
}

// One of a radio group, down while it is the one chosen.
void DrawChoice(SDL_Renderer* r, const Theme& theme, const Button& b, bool chosen,
                const Pointer& p) {
    if (!b.visible) return;
    if (!chosen) {
        DrawButton(r, theme, b, StateOf(b, p.x, p.y, p.held));
        return;
    }
    DrawShaded(r, theme, b.rect);
    DrawBevel(r, theme, b.rect, true);
    const SDL_Rect inside = {b.rect.x + 1, b.rect.y + 1, b.rect.w, b.rect.h};
    DrawTextIn(r, b.font, inside, b.label, theme.state, theme.state_shadow);
}

// ---------------------------------------------------------------------------
// The layouts the campaign screens read from interfac.drs: a line a slot --
// its number, the picture button's x y w h, then its name's x y w h. 50344
// is the Campaigns screen's (slot N for camN.cpn), 50344 + N the map of
// campaign N (a slot a scenario). A button whose x is -1 is not there.

struct Slot {
    SDL_Rect button{-1, -1, 0, 0};
    SDL_Rect label{0, 0, 0, 0};
};

constexpr uint32_t kBookLayout = 50344;

std::vector<Slot> ReadLayout(uint32_t id) {
    std::vector<Slot> slots;
    const HAL::DrsArchive* archive = HAL::GraphicCache::Get().InterfaceArchive();
    std::string text;
    if (archive == nullptr || !HAL::DrsReadText(*archive, id, text)) return slots;
    std::istringstream lines(text);
    std::string line;
    while (std::getline(lines, line)) {
        std::istringstream in(line);
        int number = 0;
        if (!(in >> number) || number < 1) continue;
        // The values a line leaves out.
        int v[8] = {-1, -1, 100, 100, 0, 0, 50, 50};
        for (int& value : v) {
            int read = 0;
            if (!(in >> read)) break;
            value = read;
        }
        if (static_cast<int>(slots.size()) < number) slots.resize(static_cast<size_t>(number));
        slots[static_cast<size_t>(number - 1)] =
            Slot{SDL_Rect{v[0], v[1], v[2], v[3]}, SDL_Rect{v[4], v[5], v[6], v[7]}};
    }
    return slots;
}

std::string CampaignPath(int number) {
    return std::string(kCampaignDir) + "cam" + std::to_string(number) + ".cpn";
}

std::string Player() { return Core::Profile::Current().name; }

// The difficulty the campaign maps and Custom Scenario share: 0 Hardest to
// 4 Easiest, Easy until chosen.
int g_difficulty = 3;

// The drop-down of the difficulties, easiest first, each standing for its
// level (11220 Easiest as 4 ... 11216 Hardest as 0).
DropDown DifficultyList(const SDL_Rect& rect) {
    DropDown d;
    d.rect = rect;
    for (int id = 11220; id >= 11216; --id) d.items.push_back(Get(id));
    d.selected = 4 - std::clamp(g_difficulty, 0, 4);
    d.font = Text::GameFont(11);
    return d;
}
int DifficultyOf(const DropDown& d) { return 4 - d.selected; }

// Where the last scenario game was started from.
struct Origin {
    Id screen = Id::MainMenu;
    int campaign = 0;
    bool from_main_menu = false;
};
Origin g_origin;

// ---------------------------------------------------------------------------
// The Campaigns screen: the book of 50232 in its palette
// (cam.sin, 50267), each campaign's emblem a picture button where 50344 puts
// it -- frame 1 + 4(N - 1) of the art, 2 + 4(N - 1) while the mouse is over
// it -- its line below (LANGUAGE.DLL 34928 + 30(N - 1), font 11), and the
// description of the one the mouse is over (that + 1000) in the box at the
// lower left: 452x190 at (34, 354), the art shaded 47% behind the text, a
// two-line sunken border.

constexpr SDL_Rect kBookTitle = {25, 12, 750, 37};
constexpr SDL_Rect kBookBox = {34, 354, 452, 190};
constexpr SDL_Rect kBackButton = {292, 550, 195, 37};
constexpr int kSlots = 8;

struct Book {
    Theme theme;
    std::vector<Slot> slots;
    bool present[kSlots + 1] = {};
    int pressed = 0;
    Button back;
};

Book& TheBook() {
    static Book book;
    return book;
}

int BookSlotAt(const Book& b, int x, int y) {
    for (int n = 1; n <= kSlots && n <= static_cast<int>(b.slots.size()); ++n) {
        const Slot& s = b.slots[static_cast<size_t>(n - 1)];
        if (b.present[n] && s.button.x >= 0 && Hit(s.button, x, y)) return n;
    }
    return 0;
}

int ListString(int number) { return 34928 + 30 * (number - 1); }

void DrawBook(SDL_Renderer* r, const Pointer& p) {
    Book& b = TheBook();
    const Theme& theme = b.theme;
    DrawBackground(r, theme);
    DrawPanelText(r, Text::GameFont(1), kBookTitle, Get(11242), Align::Centre, Down::Top, false,
                  SinInk(theme));
    const int over = BookSlotAt(b, p.x, p.y);
    for (int n = 1; n <= kSlots && n <= static_cast<int>(b.slots.size()); ++n) {
        const Slot& s = b.slots[static_cast<size_t>(n - 1)];
        if (!b.present[n] || s.button.x < 0) continue;
        DrawFrame(r, theme, (n == over ? 2 : 1) + 4 * (n - 1), s.button.x, s.button.y);
        DrawPanelText(r, Text::GameFont(11), s.label, Get(ListString(n)), Align::Left, Down::Top,
                      false, SinInk(theme));
    }
    DrawShaded(r, theme, kBookBox);
    DrawSunken2(r, theme, kBookBox);
    if (over > 0) {
        DrawPanelText(r, Text::GameFont(11), kBookBox, Get(ListString(over) + 1000), Align::Left,
                      Down::Top, true, SinInk(theme), 0, 2);
    }
    DrawFocused(r, theme, b.back, p);
    DrawCloseBox(r, theme, p.held && Hit(kCloseBox, p.x, p.y));
}

void BookEvent(const SDL_Event& e, const Pointer& p, Request& request) {
    Book& b = TheBook();
    (void)p;
    if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
        b.pressed = BookSlotAt(b, e.button.x, e.button.y);
    } else if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
        const int slot = BookSlotAt(b, e.button.x, e.button.y);
        if (slot > 0 && slot == b.pressed) {
            Click();
            OpenCampaignMap(slot, false);
        } else if (Hit(b.back.rect, e.button.x, e.button.y)) {
            Click();
            Open(Id::MainMenu);
        } else if (Hit(kCloseBox, e.button.x, e.button.y)) {
            request.kind = Request::Kind::Quit;
        }
        b.pressed = 0;
    } else if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE) {
        Open(Id::MainMenu);
    }
}

// ---------------------------------------------------------------------------
// A campaign's map: its art and palette from cam<N>.sin (50250 + N), its
// scenarios' markers and names where 50344 + N puts them. Only the scenarios
// up to the furthest one opened are there; the learning campaign shows all
// seven. A marker is frame
// 1 + 4i of the art, 2 + 4i under the mouse; the learning campaign's won
// scenarios keep frame 2 + 4i, its faded one. A name is the scenario's line
// of LANGUAGE.DLL, font 11, centred and wrapped in its box: black on Saladin's,
// Barbarossa's and Wallace's maps, the parchment's colour on a black shadow
// on the darker maps of Joan of Arc and Genghis Khan.
//
// The title is the campaign's name (font 1, across the top), Wallace's in
// black with "Learning Campaign" under it and the note at the lower left;
// every other has the difficulty at the lower right. Its button back is
// "Campaign Menu" -- "Main Menu" for the learning campaign opened from the
// main menu (Learn to Play).

constexpr int kLearning = 8;
// cam<N>.sin: 50250 + N.
constexpr uint32_t kMapSinBase = 50250;
constexpr SDL_Rect kMapTitle = {25, 12, 750, 37};
constexpr SDL_Rect kWallaceTitle = {25, 4, 750, 200};
constexpr SDL_Rect kWallaceSubtitle = {245, 33, 300, 200};
constexpr SDL_Rect kWallaceNote = {4, 515, 283, 80};
constexpr SDL_Rect kDifficultyLabel = {602, 420, 190, 20};
constexpr SDL_Rect kDifficultyList = {607, 444, 130, 24};
constexpr int kLearningScenarios = 7;

struct Map {
    Theme theme;
    int number = 0;
    bool from_main_menu = false;
    Scenario::Campaign campaign;
    bool loaded = false;
    std::vector<Slot> slots;
    int shown = 0;
    std::vector<bool> won;     // by scenario, from the progress book
    int pressed = -1;
    DropDown difficulty;
    Button back;
};

Map& TheMap() {
    static Map map;
    return map;
}

int MapSlotAt(const Map& m, int x, int y) {
    for (int i = 0; i < m.shown && i < static_cast<int>(m.slots.size()); ++i) {
        const Slot& s = m.slots[static_cast<size_t>(i)];
        if (s.button.x >= 0 && Hit(s.button, x, y)) return i;
    }
    return -1;
}

Ink LabelInk(int number) {
    if (number == 1 || number == 3) return Ink{1, {0, 0, 0, 255}, {227, 212, 182, 255}};
    return kBlackInk;
}

void DrawMap(SDL_Renderer* r, const Pointer& p) {
    Map& m = TheMap();
    const Theme& theme = m.theme;
    DrawBackground(r, theme);
    const Scenario::CampaignStrings strings = Scenario::StringsOf(m.campaign);
    const std::string name = strings.name >= 0 ? Get(strings.name) : m.campaign.name;
    if (m.number == kLearning) {
        DrawPanelText(r, Text::GameFont(1), kWallaceTitle, name, Align::Centre, Down::Top, false,
                      kBlackInk);
        DrawPanelText(r, Text::GameFont(11), kWallaceSubtitle, Get(35147), Align::Centre, Down::Top,
                      false, kBlackInk);
        DrawPanelText(r, Text::GameFont(11), kWallaceNote, Get(36167), Align::Left, Down::Top, true,
                      kBlackInk);
    } else {
        DrawPanelText(r, Text::GameFont(1), kMapTitle, name, Align::Centre, Down::Top, false,
                      SinInk(theme));
    }
    const int over = MapSlotAt(m, p.x, p.y);
    for (int i = 0; i < m.shown && i < static_cast<int>(m.slots.size()); ++i) {
        const Slot& s = m.slots[static_cast<size_t>(i)];
        if (s.button.x < 0) continue;
        const bool won = m.number == kLearning && m.won[static_cast<size_t>(i)];
        const int frame = (won || i == over ? 2 : 1) + 4 * i;
        DrawFrame(r, theme, frame, s.button.x, s.button.y);
        const int label = Scenario::ScenarioString(m.campaign, i);
        DrawPanelText(r, Text::GameFont(11), s.label,
                      label >= 0 ? Get(label) : m.campaign.scenarios[static_cast<size_t>(i)].name,
                      Align::Centre, Down::Top, true, LabelInk(m.number));
    }
    DrawFocused(r, theme, m.back, p);
    DrawCloseBox(r, theme, p.held && Hit(kCloseBox, p.x, p.y));
    if (m.number != kLearning) {
        DrawPanelText(r, Text::GameFont(0), kDifficultyLabel, Get(11215), Align::Left, Down::Top,
                      false, SinInk(theme));
        DrawDropDown(r, theme, m.difficulty, p.x, p.y, p.held, kMenuHeight);
    }
}

void MapEvent(const SDL_Event& e, const Pointer& p, Request& request) {
    Map& m = TheMap();
    (void)p;
    if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
        if (m.number != kLearning && (m.difficulty.open || Hit(m.difficulty.rect, e.button.x, e.button.y))) {
            ClickDropDown(m.difficulty, e.button.x, e.button.y, kMenuHeight);
            g_difficulty = DifficultyOf(m.difficulty);
            m.pressed = -1;
            return;
        }
        m.pressed = MapSlotAt(m, e.button.x, e.button.y);
    } else if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
        const int slot = MapSlotAt(m, e.button.x, e.button.y);
        if (slot >= 0 && slot == m.pressed) {
            Click();
            g_origin = Origin{Id::CampaignMap, m.number, m.from_main_menu};
            request.kind = Request::Kind::StartScenario;
            request.path = m.campaign.path;
            request.entry = slot;
            request.setup.difficulty = g_difficulty;
        } else if (Hit(m.back.rect, e.button.x, e.button.y)) {
            Click();
            Open(m.from_main_menu ? Id::MainMenu : Id::Campaigns);
        } else if (Hit(kCloseBox, e.button.x, e.button.y)) {
            request.kind = Request::Kind::Quit;
        }
        m.pressed = -1;
    } else if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE) {
        Open(m.from_main_menu ? Id::MainMenu : Id::Campaigns);
    }
}

// ---------------------------------------------------------------------------
// Custom Scenario: on the plain parchment (scr2, 50052), laid out at
// 640x480 and scaled by the screen's size over the layout's, so at 800x600
// everything is a quarter larger than the numbers below. Its text is plain
// black.

constexpr SDL_Rect Scaled(int x, int y, int w, int h) {
    return SDL_Rect{x * kMenuWidth / 640, y * kMenuHeight / 480, w * kMenuWidth / 640,
                    h * kMenuHeight / 480};
}
constexpr SDL_Rect kSelectTitle = Scaled(20, 10, 600, 30);
constexpr SDL_Rect kSelectLabel = Scaled(70, 50, 300, 20);
constexpr SDL_Rect kSelectList = Scaled(70, 70, 500, 290);
constexpr SDL_Rect kSelectDifficultyLabel = Scaled(65, 370, 200, 20);
constexpr SDL_Rect kSelectDifficulty = Scaled(70, 390, 130, 24);
constexpr SDL_Rect kSelectOk = Scaled(170, 440, 140, 30);
constexpr SDL_Rect kSelectCancel = Scaled(330, 440, 140, 30);

struct Select {
    Theme theme;
    ListBox list;
    std::vector<std::string> paths;
    DropDown difficulty;
    Button ok, cancel;
};

Select& TheSelect() {
    static Select select;
    return select;
}

void DrawSelect(SDL_Renderer* r, const Pointer& p) {
    Select& s = TheSelect();
    const Theme& theme = s.theme;
    DrawBackground(r, theme);
    DrawPanelText(r, Text::GameFont(1), kSelectTitle, Get(9722), Align::Centre, Down::Top, false,
                  kBlackInk);
    DrawPanelText(r, Text::GameFont(4), kSelectLabel, Get(9726), Align::Left, Down::Top, false,
                  kBlackInk);
    DrawListBox(r, theme, s.list, p.x, p.y);
    DrawPanelText(r, Text::GameFont(4), kSelectDifficultyLabel, Get(11215), Align::Left,
                  Down::Top, false, kBlackInk);
    Button ok = s.ok;
    ok.enabled = s.list.selected >= 0;
    DrawButton(r, theme, ok, StateOf(ok, p.x, p.y, p.held));
    DrawButton(r, theme, s.cancel, StateOf(s.cancel, p.x, p.y, p.held));
    DrawDropDown(r, theme, s.difficulty, p.x, p.y, p.held, kMenuHeight);
    DrawCloseBox(r, theme, p.held && Hit(kCloseBox, p.x, p.y));
}

void SelectEvent(const SDL_Event& e, const Pointer& p, Request& request) {
    Select& s = TheSelect();
    (void)p;
    if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
        if (s.difficulty.open || Hit(s.difficulty.rect, e.button.x, e.button.y)) {
            ClickDropDown(s.difficulty, e.button.x, e.button.y, kMenuHeight);
            g_difficulty = DifficultyOf(s.difficulty);
            return;
        }
        ClickListBox(s.list, e.button.x, e.button.y);
    } else if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
        if (Hit(s.ok.rect, e.button.x, e.button.y) && s.list.selected >= 0) {
            Click();
            g_origin = Origin{Id::ScenarioSelect, 0, false};
            request.kind = Request::Kind::StartScenario;
            request.path = s.paths[static_cast<size_t>(s.list.selected)];
            request.entry = -1;
            request.setup.difficulty = g_difficulty;
        } else if (Hit(s.cancel.rect, e.button.x, e.button.y)) {
            Click();
            Open(Id::MainMenu);
        } else if (Hit(kCloseBox, e.button.x, e.button.y)) {
            request.kind = Request::Kind::Quit;
        }
    } else if (e.type == SDL_MOUSEWHEEL) {
        const int most = std::max(0, static_cast<int>(s.list.items.size()) - ListBoxRows(s.list));
        s.list.scroll = std::clamp(s.list.scroll - e.wheel.y, 0, most);
    } else if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE) {
        Open(Id::MainMenu);
    }
}

// ---------------------------------------------------------------------------
// Custom Campaign: the Campaign Selection Screen, on the plain parchment
// (scr2) as Custom Scenario is, laid out at 640x480 and scaled the same way.
// The custom campaigns -- any but the game's five -- above; the chosen one's
// scenarios below, from its first to the furthest the player has opened, the
// furthest chosen; the difficulty; OK and Cancel.

constexpr SDL_Rect kCustomTitle = Scaled(20, 10, 600, 30);
constexpr SDL_Rect kCustomCampaignLabel = Scaled(72, 50, 200, 20);
constexpr SDL_Rect kCustomCampaigns = Scaled(70, 70, 500, 130);
constexpr SDL_Rect kCustomScenarioLabel = Scaled(72, 210, 200, 20);
constexpr SDL_Rect kCustomScenarios = Scaled(70, 230, 500, 130);

struct Custom {
    ListBox campaigns;
    ListBox scenarios;
    std::vector<std::string> paths;
    Scenario::Campaign chosen;
    int shown = -1;           // the campaign whose scenarios are listed
    DropDown difficulty;
    Button ok, cancel;
};

Custom& TheCustom() {
    static Custom custom;
    return custom;
}

// The chosen campaign's scenarios, from the first to the furthest opened.
void ListScenarios(Custom& c) {
    c.shown = c.campaigns.selected;
    c.scenarios = ListBox{kCustomScenarios, {}, -1, 0, Text::GameFont(11)};
    if (c.shown < 0 || !Scenario::LoadCampaign(c.paths[static_cast<size_t>(c.shown)], c.chosen)) return;
    const Scenario::Progress::Book book = Scenario::Progress::Load();
    const int open = std::min(static_cast<int>(c.chosen.scenarios.size()),
                              Scenario::Progress::Unlocked(book, Player(), c.chosen.name));
    for (int i = 0; i < open; ++i) c.scenarios.items.push_back(c.chosen.scenarios[static_cast<size_t>(i)].name);
    c.scenarios.selected = open - 1;
    c.scenarios.scroll = std::max(0, open - ListBoxRows(c.scenarios));
}

void DrawCustom(SDL_Renderer* r, const Pointer& p) {
    Custom& c = TheCustom();
    const Theme& theme = TheSelect().theme;
    DrawBackground(r, theme);
    DrawPanelText(r, Text::GameFont(1), kCustomTitle, Get(11213), Align::Centre, Down::Top, false, kBlackInk);
    DrawPanelText(r, Text::GameFont(4), kCustomCampaignLabel, Get(11214), Align::Left, Down::Top, false, kBlackInk);
    DrawListBox(r, theme, c.campaigns, p.x, p.y);
    DrawPanelText(r, Text::GameFont(4), kCustomScenarioLabel, Get(9726), Align::Left, Down::Top, false, kBlackInk);
    DrawListBox(r, theme, c.scenarios, p.x, p.y);
    DrawPanelText(r, Text::GameFont(4), kSelectDifficultyLabel, Get(11215), Align::Left, Down::Top, false,
                  kBlackInk);
    Button ok = c.ok;
    ok.enabled = c.scenarios.selected >= 0;
    DrawButton(r, theme, ok, StateOf(ok, p.x, p.y, p.held));
    DrawButton(r, theme, c.cancel, StateOf(c.cancel, p.x, p.y, p.held));
    DrawDropDown(r, theme, c.difficulty, p.x, p.y, p.held, kMenuHeight);
    DrawCloseBox(r, theme, p.held && Hit(kCloseBox, p.x, p.y));
}

void CustomEvent(const SDL_Event& e, Request& request) {
    Custom& c = TheCustom();
    if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
        if (c.difficulty.open || Hit(c.difficulty.rect, e.button.x, e.button.y)) {
            ClickDropDown(c.difficulty, e.button.x, e.button.y, kMenuHeight);
            g_difficulty = DifficultyOf(c.difficulty);
            return;
        }
        if (ClickListBox(c.campaigns, e.button.x, e.button.y)) {
            if (c.campaigns.selected != c.shown) ListScenarios(c);
            return;
        }
        ClickListBox(c.scenarios, e.button.x, e.button.y);
    } else if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
        if (Hit(c.ok.rect, e.button.x, e.button.y) && c.scenarios.selected >= 0) {
            Click();
            g_origin = Origin{Id::CustomCampaigns, 0, false};
            request.kind = Request::Kind::StartScenario;
            request.path = c.paths[static_cast<size_t>(c.shown)];
            request.entry = c.scenarios.selected;
            request.setup.difficulty = g_difficulty;
        } else if (Hit(c.cancel.rect, e.button.x, e.button.y)) {
            Click();
            Open(Id::MainMenu);
        } else if (Hit(kCloseBox, e.button.x, e.button.y)) {
            request.kind = Request::Kind::Quit;
        }
    } else if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE) {
        Open(Id::MainMenu);
    }
}

// ---------------------------------------------------------------------------
// The Campaign Editor, from the Map Editor's menu: on the parchment of scr4,
// at 800x600. The campaign's file name, typed or chosen from the campaigns
// there are, and Save; the scenarios to choose from, Add and Remove; the
// campaign's scenarios in the order they are played, Up and Down; OK and
// Cancel. A campaign is saved under its name in Screens::CampaignsDir().

constexpr SDL_Rect kEditorTitle = {25, 12, 750, 37};
constexpr SDL_Rect kEditorFileLabel = {100, 100, 260, 20};
constexpr SDL_Rect kEditorFile = {100, 120, 380, 24};
constexpr SDL_Rect kEditorName = {100, 120, 360, 24};
constexpr SDL_Rect kEditorSave = {500, 120, 200, 30};
constexpr SDL_Rect kEditorScenariosLabel = {100, 160, 200, 20};
constexpr SDL_Rect kEditorScenarios = {100, 180, 600, 130};
constexpr SDL_Rect kEditorAdd = {360, 320, 160, 30};
constexpr SDL_Rect kEditorRemove = {540, 320, 160, 30};
constexpr SDL_Rect kEditorChosenLabel = {100, 340, 200, 20};
constexpr SDL_Rect kEditorChosen = {100, 360, 500, 130};
constexpr SDL_Rect kEditorUp = {610, 360, 90, 30};
constexpr SDL_Rect kEditorDown = {610, 400, 90, 30};
constexpr SDL_Rect kEditorOk = {212, 550, 175, 37};
constexpr SDL_Rect kEditorCancel = {412, 550, 175, 37};
constexpr size_t kCampaignNameLength = 255;

struct CampaignEditor {
    Theme theme;
    Theme dialog;
    DropDown file;
    std::vector<std::string> file_paths;
    TextField name;
    ListBox scenarios;
    std::vector<std::string> scenario_paths;
    ListBox chosen;
    std::vector<Scenario::CampaignScenario> entries;
    Button save, add, remove, up, down, ok, cancel;
    bool dirty = false;
    // A message; a question asks Yes or No.
    Notice notice;
    Button no;
    bool asking = false;
};

CampaignEditor& TheEditor() {
    static CampaignEditor editor;
    return editor;
}

void ShowEntries(CampaignEditor& c) {
    c.chosen.items.clear();
    for (const auto& e : c.entries) c.chosen.items.push_back(e.name);
    if (c.chosen.selected >= static_cast<int>(c.entries.size())) c.chosen.selected = static_cast<int>(c.entries.size()) - 1;
}

void ListCampaigns(CampaignEditor& c) {
    c.file_paths = CustomCampaigns();
    std::vector<std::string> names;
    for (const auto& path : c.file_paths) names.push_back(std::filesystem::path(path).stem().string());
    c.file = DropDown{kEditorFile, names, -1};
    c.file.font = Text::GameFont(11);
}

void LoadInto(CampaignEditor& c, const std::string& path) {
    Scenario::Campaign campaign;
    if (!Scenario::LoadCampaign(path, campaign)) return;
    c.entries.clear();
    for (size_t i = 0; i < campaign.scenarios.size(); ++i) {
        Scenario::CampaignScenario e{campaign.scenarios[i].name, campaign.scenarios[i].file, {}};
        if (Scenario::ReadCampaignScenario(path, static_cast<int>(i), e.bytes)) c.entries.push_back(std::move(e));
    }
    c.name.text = campaign.name;
    c.chosen.selected = c.entries.empty() ? -1 : 0;
    ShowEntries(c);
    c.dirty = false;
}

// Saves the campaign; false, with the reason shown, when it cannot be.
bool SaveCampaign(CampaignEditor& c) {
    if (c.name.text.empty()) {
        OpenNotice(c.notice, c.dialog, Get(11212), Get(4001));
        return false;
    }
    if (c.entries.empty()) {
        OpenNotice(c.notice, c.dialog, Get(11211), Get(4001));
        return false;
    }
    if (!Scenario::WriteCampaign(CampaignsDir() + c.name.text + ".cpn", c.name.text, c.entries)) return false;
    c.dirty = false;
    ListCampaigns(c);
    return true;
}

void DrawCampaignEditor(SDL_Renderer* r, const Pointer& p) {
    CampaignEditor& c = TheEditor();
    const Theme& theme = c.theme;
    DrawBackground(r, theme);
    DrawPanelText(r, Text::GameFont(1), kEditorTitle, Get(11201), Align::Centre, Down::Top, false, kBlackInk);
    DrawPanelText(r, Text::GameFont(4), kEditorFileLabel, Get(11202), Align::Left, Down::Top, false, kBlackInk);
    DrawPanelText(r, Text::GameFont(4), kEditorScenariosLabel, Get(11207), Align::Left, Down::Top, false, kBlackInk);
    DrawListBox(r, theme, c.scenarios, p.x, p.y);
    DrawPanelText(r, Text::GameFont(4), kEditorChosenLabel, Get(11208), Align::Left, Down::Top, false, kBlackInk);
    DrawListBox(r, theme, c.chosen, p.x, p.y);
    Button add = c.add, remove = c.remove, up = c.up, down = c.down;
    add.enabled = c.scenarios.selected >= 0;
    remove.enabled = c.chosen.selected >= 0;
    up.enabled = c.chosen.selected > 0;
    down.enabled = c.chosen.selected >= 0 && c.chosen.selected + 1 < static_cast<int>(c.entries.size());
    const Button* buttons[] = {&c.save, &add, &remove, &up, &down, &c.ok, &c.cancel};
    for (const Button* b : buttons) {
        DrawButton(r, theme, *b, StateOf(*b, p.x, p.y, p.held));
    }
    // The name typed over the list of campaigns, its arrow beside it.
    DropDown file = c.file;
    file.items.insert(file.items.begin(), std::string{});
    DrawDropDown(r, theme, c.file.open ? c.file : file, p.x, p.y, p.held, kMenuHeight);
    if (!c.file.open) DrawTextField(r, theme, c.name);
    DrawNotice(r, c.dialog, c.notice, p);
    if (c.notice.open && c.asking) DrawButton(r, c.dialog, c.no, StateOf(c.no, p.x, p.y, p.held));
    DrawCloseBox(r, theme, p.held && Hit(kCloseBox, p.x, p.y));
}

void CampaignEditorEvent(const SDL_Event& e, Request& request) {
    CampaignEditor& c = TheEditor();
    const bool down = e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT;
    const bool up = e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT;
    if (c.notice.open) {
        if (up && Hit(c.notice.ok.rect, e.button.x, e.button.y)) {
            Click();
            c.notice.open = false;
            // Saving the changes asked about, then out.
            if (c.asking && SaveCampaign(c)) Open(Id::MainMenu);
        } else if (up && c.asking && Hit(c.no.rect, e.button.x, e.button.y)) {
            Click();
            c.notice.open = false;
            Open(Id::MainMenu);
        }
        return;
    }
    if (TypeIntoField(c.name, e)) {
        c.dirty = true;
        return;
    }
    if (down) {
        if (c.file.open || Hit(SDL_Rect{kEditorName.x + kEditorName.w, kEditorFile.y,
                                        kEditorFile.w - kEditorName.w, kEditorFile.h}, e.button.x, e.button.y)) {
            const int before = c.file.selected;
            ClickDropDown(c.file, e.button.x, e.button.y, kMenuHeight);
            if (!c.file.open && c.file.selected >= 0 && c.file.selected != before) {
                LoadInto(c, c.file_paths[static_cast<size_t>(c.file.selected)]);
            }
            return;
        }
        c.name.focused = Hit(kEditorName, e.button.x, e.button.y);
        if (ClickListBox(c.scenarios, e.button.x, e.button.y)) return;
        ClickListBox(c.chosen, e.button.x, e.button.y);
    } else if (up) {
        const int at = c.chosen.selected;
        if (Hit(c.save.rect, e.button.x, e.button.y)) {
            Click();
            SaveCampaign(c);
        } else if (Hit(c.add.rect, e.button.x, e.button.y) && c.scenarios.selected >= 0) {
            Click();
            const std::filesystem::path path = c.scenario_paths[static_cast<size_t>(c.scenarios.selected)];
            std::ifstream in(path, std::ios::binary);
            Scenario::CampaignScenario entry{path.stem().string(), path.filename().string(),
                                             {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()}};
            // After the one chosen, or at the end.
            const size_t place = at >= 0 ? static_cast<size_t>(at) + 1 : c.entries.size();
            c.entries.insert(c.entries.begin() + static_cast<long>(place), std::move(entry));
            c.chosen.selected = static_cast<int>(place);
            ShowEntries(c);
            c.dirty = true;
        } else if (Hit(c.remove.rect, e.button.x, e.button.y) && at >= 0) {
            Click();
            c.entries.erase(c.entries.begin() + at);
            ShowEntries(c);
            c.dirty = true;
        } else if (Hit(c.up.rect, e.button.x, e.button.y) && at > 0) {
            Click();
            std::swap(c.entries[static_cast<size_t>(at)], c.entries[static_cast<size_t>(at - 1)]);
            c.chosen.selected = at - 1;
            ShowEntries(c);
            c.dirty = true;
        } else if (Hit(c.down.rect, e.button.x, e.button.y) && at >= 0 &&
                   at + 1 < static_cast<int>(c.entries.size())) {
            Click();
            std::swap(c.entries[static_cast<size_t>(at)], c.entries[static_cast<size_t>(at + 1)]);
            c.chosen.selected = at + 1;
            ShowEntries(c);
            c.dirty = true;
        } else if (Hit(c.ok.rect, e.button.x, e.button.y)) {
            Click();
            if (!c.dirty || SaveCampaign(c)) Open(Id::MainMenu);
        } else if (Hit(c.cancel.rect, e.button.x, e.button.y)) {
            Click();
            if (c.dirty) {
                // "Do you want to save your changes to campaign %s?"
                char text[512];
                std::snprintf(text, sizeof(text), Get(11209).c_str(), c.name.text.c_str());
                OpenNotice(c.notice, c.dialog, text, Get(4003));
                c.asking = true;
                c.no = c.notice.ok;
                c.no.label = Get(4004);
                c.notice.ok.rect.x -= c.notice.ok.rect.w / 2 + 8;
                c.no.rect.x += c.no.rect.w / 2 + 8;
            } else {
                Open(Id::MainMenu);
            }
        } else if (Hit(kCloseBox, e.button.x, e.button.y)) {
            request.kind = Request::Kind::Quit;
        }
    } else if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE) {
        Open(Id::MainMenu);
    }
}

// ---------------------------------------------------------------------------
// The scenario's instructions before its game: the "Mission Dialog", on
// the loading screen's art (scr9, 50060). Across the top the
// scenario's name (font 1, centred in 794x40 at (3, 12)); its map picture,
// when it has one, at (144, 97) -- the 548x288 of the parchment the editor's
// scenariobkg.bmp is cut to; below, the text in a 553x122 box at (142, 405),
// font 11, black on a grey shadow, with a scroll
// bar; along the bottom History, Instructions and Hints (10801, 10802,
// 10807) at x 5, 205 and 405 and OK at 605, each 190x37 at y 560.
//
// The instructions show first, else the history, else the hints; the three
// buttons are there only when more than one of the texts is, and the box
// only when one is.

constexpr SDL_Rect kMissionTitle = {3, 12, 794, 40};
constexpr SDL_Point kMissionMap = {144, 97};
constexpr SDL_Rect kMissionText = {142, 405, 553, 122};
constexpr int kScrollW = 20;
constexpr Ink kMissionInk{2, {0, 0, 0, 255}, {200, 200, 200, 255}};

enum Tab : int { kHistory, kInstructions, kHints, kTabs };
constexpr int kTabText[kTabs] = {Scenario::kHistory, Scenario::kInstructions, Scenario::kHints};
constexpr int kTabLabel[kTabs] = {10801, 10802, 10807};

struct Mission {
    Theme theme;
    std::string title;
    std::string texts[kTabs];
    int tab = kInstructions;
    bool tabs_shown = false;
    bool text_shown = false;
    int scroll = 0;
    Button buttons[kTabs];
    Button ok;
    SDL_Texture* picture = nullptr;
    int picture_w = 0, picture_h = 0;
};

Mission& TheMission() {
    static Mission mission;
    return mission;
}

// The scenario's own map picture, 8 bits a pixel in its own palette, rows
// bottom up and padded to four bytes as a BMP keeps them.
SDL_Texture* PictureOf(SDL_Renderer* r, const Scenario::Bitmap& bitmap) {
    if (bitmap.width <= 0 || bitmap.height <= 0 || bitmap.pixels.empty()) return nullptr;
    const size_t stride = bitmap.pixels.size() / static_cast<size_t>(bitmap.height);
    if (stride < static_cast<size_t>(bitmap.width)) return nullptr;
    SDL_Surface* s = SDL_CreateRGBSurfaceWithFormat(0, bitmap.width, bitmap.height, 32,
                                                    SDL_PIXELFORMAT_ARGB8888);
    if (s == nullptr) return nullptr;
    auto* out = static_cast<uint32_t*>(s->pixels);
    for (int y = 0; y < bitmap.height; ++y) {
        const uint8_t* row = bitmap.pixels.data() + static_cast<size_t>(bitmap.height - 1 - y) * stride;
        for (int x = 0; x < bitmap.width; ++x) {
            const uint8_t index = row[x];
            const uint32_t rgb = index < bitmap.palette.size() ? bitmap.palette[index] : 0;
            out[y * (s->pitch / 4) + x] = 0xFF000000u | rgb;
        }
    }
    SDL_Texture* texture = SDL_CreateTextureFromSurface(r, s);
    SDL_FreeSurface(s);
    return texture;
}

const Scenario::File* g_mission_file = nullptr;

int MissionLines(const Mission& m) {
    return static_cast<int>(
        PanelLines(Text::GameFont(11), m.texts[m.tab], PanelWrapWidth(kMissionText.w - kScrollW, 0), true)
            .size());
}
int MissionRows() { return kMissionText.h / std::max(1, PanelPitch(Text::GameFont(11))); }

void DrawMission(SDL_Renderer* r, const Pointer& p) {
    Mission& m = TheMission();
    const Theme& theme = m.theme;
    if (g_mission_file != nullptr && m.picture == nullptr) {
        m.picture = PictureOf(r, g_mission_file->bitmap);
        if (m.picture != nullptr) {
            m.picture_w = g_mission_file->bitmap.width;
            m.picture_h = g_mission_file->bitmap.height;
        }
    }
    DrawBackground(r, theme);
    DrawPanelText(r, Text::GameFont(1), kMissionTitle, m.title, Align::Centre, Down::Centre,
                  false, SinInk(theme));
    if (m.picture != nullptr) {
        const SDL_Rect dst = {kMissionMap.x, kMissionMap.y, m.picture_w, m.picture_h};
        SDL_RenderCopy(r, m.picture, nullptr, &dst);
    }
    if (m.text_shown) {
        const SDL_Rect text = {kMissionText.x, kMissionText.y, kMissionText.w - kScrollW,
                               kMissionText.h};
        const int lines = DrawPanelText(r, Text::GameFont(11), text, m.texts[m.tab], Align::Left,
                                        Down::Top, true, kMissionInk, m.scroll);
        if (lines > MissionRows()) {
            DrawButtonArt(r, theme, 8, kMissionText.x + kMissionText.w - kScrollW, kMissionText.y);
            DrawButtonArt(r, theme, 10, kMissionText.x + kMissionText.w - kScrollW,
                          kMissionText.y + kMissionText.h - kScrollW);
        }
    }
    if (m.tabs_shown) {
        for (int t = 0; t < kTabs; ++t) DrawChoice(r, theme, m.buttons[t], t == m.tab, p);
    }
    DrawFocused(r, theme, m.ok, p);
}

void MissionEvent(const SDL_Event& e, const Pointer& p, Request& request) {
    Mission& m = TheMission();
    (void)p;
    const int most = std::max(0, MissionLines(m) - MissionRows());
    if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
        const int x = e.button.x, y = e.button.y;
        if (Hit(m.ok.rect, x, y)) {
            Click();
            request.kind = Request::Kind::Begin;
            return;
        }
        for (int t = 0; t < kTabs && m.tabs_shown; ++t) {
            if (m.buttons[t].visible && Hit(m.buttons[t].rect, x, y)) {
                Click();
                m.tab = t;
                m.scroll = 0;
                return;
            }
        }
        // The scroll bar's arrows.
        const SDL_Rect up = {kMissionText.x + kMissionText.w - kScrollW, kMissionText.y, kScrollW,
                             kScrollW};
        const SDL_Rect down = {up.x, kMissionText.y + kMissionText.h - kScrollW, kScrollW,
                               kScrollW};
        if (Hit(up, x, y)) m.scroll = std::max(0, m.scroll - 1);
        if (Hit(down, x, y)) m.scroll = std::min(most, m.scroll + 1);
    } else if (e.type == SDL_MOUSEWHEEL) {
        m.scroll = std::clamp(m.scroll - e.wheel.y, 0, most);
    } else if (e.type == SDL_KEYDOWN &&
               (e.key.keysym.sym == SDLK_RETURN || e.key.keysym.sym == SDLK_ESCAPE)) {
        request.kind = Request::Kind::Begin;
    }
}

// ---------------------------------------------------------------------------
// A scenario's end, the "Aftermath Screen": after a scenario that is not a
// campaign's, its victory text when the player won, its loss text when not
// (no screen when that text is empty), then the achievements. On the plain
// parchment (scr2, 50052), laid out at 800x600: "Aftermath" (50001, font 1)
// centred in 794x40 at (3, 12), black; the text in a 626x426 box at
// (87, 87), font 11, in the .sin's colours with its two-line sunken border
// and a scroll bar; "Achievements" (50002) 190x37 at (305, 550), which has
// the focus. Its music is the end's own (won1, won2 or lost), already
// playing from the game's end.

constexpr SDL_Rect kAftermathTitle = {3, 12, 794, 40};
constexpr SDL_Rect kAftermathText = {87, 87, 626, 426};
constexpr SDL_Rect kAftermathButton = {305, 550, 190, 37};

struct Aftermath {
    Theme theme;
    std::string text;
    int scroll = 0;
    Button go_on;
};

Aftermath& TheAftermath() {
    static Aftermath aftermath;
    return aftermath;
}

int AftermathLines(const Aftermath& a) {
    return static_cast<int>(PanelLines(Text::GameFont(11), a.text,
                                       PanelWrapWidth(kAftermathText.w - kScrollW, 2), true)
                                .size());
}
int AftermathRows() { return (kAftermathText.h - 4) / PanelPitch(Text::GameFont(11)); }

void DrawAftermath(SDL_Renderer* r, const Pointer& p) {
    Aftermath& a = TheAftermath();
    const Theme& theme = a.theme;
    DrawBackground(r, theme);
    DrawPanelText(r, Text::GameFont(1), kAftermathTitle, Get(50001), Align::Centre, Down::Centre,
                  false, kBlackInk);
    DrawShaded(r, theme, kAftermathText);
    DrawSunken2(r, theme, kAftermathText);
    const SDL_Rect text = {kAftermathText.x, kAftermathText.y, kAftermathText.w - kScrollW,
                           kAftermathText.h};
    const int lines = DrawPanelText(r, Text::GameFont(11), text, a.text, Align::Left, Down::Top,
                                    true, SinInk(theme), a.scroll, 2);
    if (lines > AftermathRows()) {
        DrawButtonArt(r, theme, 8, kAftermathText.x + kAftermathText.w - kScrollW - 2,
                      kAftermathText.y + 2);
        DrawButtonArt(r, theme, 10, kAftermathText.x + kAftermathText.w - kScrollW - 2,
                      kAftermathText.y + kAftermathText.h - kScrollW - 2);
    }
    DrawFocused(r, theme, a.go_on, p);
}

void AftermathEvent(const SDL_Event& e, const Pointer& p, Request& request) {
    Aftermath& a = TheAftermath();
    (void)p;
    const int most = std::max(0, AftermathLines(a) - AftermathRows());
    if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
        const int x = e.button.x, y = e.button.y;
        if (Hit(a.go_on.rect, x, y)) {
            Click();
            request.kind = Request::Kind::Begin;
            return;
        }
        const int bar = kAftermathText.x + kAftermathText.w - kScrollW - 2;
        if (Hit(SDL_Rect{bar, kAftermathText.y + 2, kScrollW, kScrollW}, x, y)) {
            a.scroll = std::max(0, a.scroll - 1);
        } else if (Hit(SDL_Rect{bar, kAftermathText.y + kAftermathText.h - kScrollW - 2, kScrollW,
                                kScrollW}, x, y)) {
            a.scroll = std::min(most, a.scroll + 1);
        }
    } else if (e.type == SDL_MOUSEWHEEL) {
        a.scroll = std::clamp(a.scroll - e.wheel.y, 0, most);
    } else if (e.type == SDL_KEYDOWN &&
               (e.key.keysym.sym == SDLK_RETURN || e.key.keysym.sym == SDLK_ESCAPE)) {
        request.kind = Request::Kind::Begin;
    }
}

} // namespace

void Init() {
    TheBook().theme = MakeTheme(Sin::kCampaigns, kMenuWidth, kMenuHeight);
    TheBook().slots = ReadLayout(kBookLayout);
    TheSelect().theme = MakeTheme(Sin::kParchment2, kMenuWidth, kMenuHeight);
    TheMission().theme = MakeTheme(Sin::kLoading, kMenuWidth, kMenuHeight);
    TheAftermath().theme = MakeTheme(Sin::kParchment2, kMenuWidth, kMenuHeight);
    TheEditor().theme = MakeTheme(Sin::kParchment4, kMenuWidth, kMenuHeight);
    TheEditor().dialog = MakeTheme(Sin::kParchmentDialog, kMenuWidth, kMenuHeight, true);
}

void PrepareBook() {
    Book& b = TheBook();
    for (int n = 1; n <= kSlots; ++n) {
        std::error_code ec;
        b.present[n] = std::filesystem::exists(CampaignPath(n), ec);
    }
    b.pressed = 0;
    b.back = Button{kBackButton, Get(11241), Text::GameFont(0)};
}

void PrepareMap(int number, bool from_main_menu) {
    Slides::ArmIntro();
    Map& m = TheMap();
    m.number = number;
    m.from_main_menu = from_main_menu;
    m.theme = MakeTheme(kMapSinBase + static_cast<uint32_t>(number), kMenuWidth, kMenuHeight);
    m.slots = ReadLayout(kBookLayout + static_cast<uint32_t>(number));
    m.loaded = Scenario::LoadCampaign(CampaignPath(number), m.campaign);
    const int count = m.loaded ? static_cast<int>(m.campaign.scenarios.size()) : 0;
    const Scenario::Progress::Book book = Scenario::Progress::Load();
    m.shown = number == kLearning
        ? std::min(count, kLearningScenarios)
        : std::min(count, Scenario::Progress::Unlocked(book, Player(), m.campaign.name));
    m.won.assign(static_cast<size_t>(count), false);
    for (int i = 0; i < count; ++i) {
        m.won[static_cast<size_t>(i)] = Scenario::Progress::Won(book, Player(), m.campaign.name, i);
    }
    m.pressed = -1;
    m.difficulty = DifficultyList(kDifficultyList);
    m.back = Button{kBackButton, Get(number == kLearning && from_main_menu ? 11252 : 11251),
                    Text::GameFont(0)};
}

void PrepareSelect() {
    Select& s = TheSelect();
    s.paths = FilesIn("../assets/original/Scenario", {".scn", ".scx"});
    // And those the scenario editor saved, beside the saved games.
    for (const std::string& p : FilesIn(ScenariosDir(), {".scn", ".scx"})) s.paths.push_back(p);
    const auto goodies = FilesIn("../original_game_1999/GOODIES", {".scn", ".scx"});
    s.paths.insert(s.paths.end(), goodies.begin(), goodies.end());
    s.list = ListBox{kSelectList, {}, s.paths.empty() ? -1 : 0, 0, Text::GameFont(11)};
    for (const auto& path : s.paths) {
        s.list.items.push_back(std::filesystem::path(path).stem().string());
    }
    s.difficulty = DifficultyList(kSelectDifficulty);
    s.ok = Button{kSelectOk, Get(4001), Text::GameFont(0)};
    s.cancel = Button{kSelectCancel, Get(4002), Text::GameFont(0)};
}

std::vector<std::string> CustomCampaigns() {
    std::vector<std::string> out;
    std::vector<std::string> found = FilesIn(kCampaignDir, {".cpn"});
    const auto own = FilesIn(CampaignsDir(), {".cpn"});
    found.insert(found.end(), own.begin(), own.end());
    for (const std::string& path : found) {
        Scenario::Campaign campaign;
        if (!Scenario::LoadCampaign(path, campaign) || Scenario::CampaignNumber(campaign) != 0) continue;
        out.push_back(path);
    }
    return out;
}

void PrepareCustom() {
    Custom& c = TheCustom();
    c.paths = CustomCampaigns();
    c.campaigns = ListBox{kCustomCampaigns, {}, c.paths.empty() ? -1 : 0, 0, Text::GameFont(11)};
    for (const std::string& path : c.paths) {
        c.campaigns.items.push_back(std::filesystem::path(path).stem().string());
    }
    // Back from a scenario, the campaign it was in stays chosen.
    for (size_t i = 0; i < c.paths.size(); ++i) {
        if (c.paths[i] == c.chosen.path) c.campaigns.selected = static_cast<int>(i);
    }
    ListScenarios(c);
    c.difficulty = DifficultyList(kSelectDifficulty);
    c.ok = Button{kSelectOk, Get(4001), Text::GameFont(0)};
    c.cancel = Button{kSelectCancel, Get(4002), Text::GameFont(0)};
}

void PrepareCampaignEditor() {
    CampaignEditor& c = TheEditor();
    ListCampaigns(c);
    c.name = TextField{kEditorName, Get(11210), kCampaignNameLength, true, Text::GameFont(11)};
    c.scenario_paths = FilesIn("../assets/original/Scenario", {".scn", ".scx"});
    for (const std::string& p : FilesIn(ScenariosDir(), {".scn", ".scx"})) c.scenario_paths.push_back(p);
    for (const std::string& p : FilesIn("../original_game_1999/GOODIES", {".scn", ".scx"})) c.scenario_paths.push_back(p);
    c.scenarios = ListBox{kEditorScenarios, {}, c.scenario_paths.empty() ? -1 : 0, 0, Text::GameFont(11)};
    for (const auto& path : c.scenario_paths) c.scenarios.items.push_back(std::filesystem::path(path).stem().string());
    c.entries.clear();
    c.chosen = ListBox{kEditorChosen, {}, -1, 0, Text::GameFont(11)};
    c.save = Button{kEditorSave, Get(11204), Text::GameFont(0)};
    c.add = Button{kEditorAdd, Get(11205), Text::GameFont(0)};
    c.remove = Button{kEditorRemove, Get(11206), Text::GameFont(0)};
    c.up = Button{kEditorUp, Get(11221), Text::GameFont(0)};
    c.down = Button{kEditorDown, Get(11222), Text::GameFont(0)};
    c.ok = Button{kEditorOk, Get(4001), Text::GameFont(0)};
    c.cancel = Button{kEditorCancel, Get(4002), Text::GameFont(0)};
    c.dirty = false;
    c.asking = false;
    c.notice.open = false;
}

void PrepareMission(const Scenario::File& file) {
    Mission& m = TheMission();
    if (m.picture != nullptr) SDL_DestroyTexture(m.picture);
    m.picture = nullptr;
    g_mission_file = &file;
    // The scenario's name: the file name it was saved under (as
    // "Joan_scn1.scn"), its last four characters cut off.
    std::string name = file.original_name;
    if (name.size() >= 4) name.resize(name.size() - 4);
    m.title = name;
    for (int t = 0; t < kTabs; ++t) m.texts[t] = Scenario::Show(file.texts[kTabText[t]]);
    // The text shown first: the instructions, else the history, else the
    // hints.
    int present = 0;
    m.tab = -1;
    for (int t : {kInstructions, kHistory, kHints}) {
        if (m.texts[t].empty()) continue;
        if (m.tab < 0) m.tab = t;
        ++present;
    }
    m.text_shown = present > 0;
    m.tabs_shown = present > 1;
    if (m.tab < 0) m.tab = kInstructions;
    m.scroll = 0;
    for (int t = 0; t < kTabs; ++t) {
        m.buttons[t] = Button{SDL_Rect{5 + 200 * t, 560, 190, 37}, Get(kTabLabel[t]),
                              Text::GameFont(0)};
        m.buttons[t].visible = !m.texts[t].empty();
    }
    m.ok = Button{SDL_Rect{605, 560, 190, 37}, Get(4001), Text::GameFont(0)};
}

bool PrepareAftermath(const Scenario::File& file, bool won) {
    Aftermath& a = TheAftermath();
    a.text = Scenario::Show(file.texts[won ? Scenario::kVictory : Scenario::kLoss]);
    a.scroll = 0;
    a.go_on = Button{kAftermathButton, Get(50002), Text::GameFont(0)};
    return !a.text.empty();
}

void Return() {
    switch (g_origin.screen) {
    case Id::CampaignMap: OpenCampaignMap(g_origin.campaign, g_origin.from_main_menu); break;
    case Id::ScenarioSelect: Open(Id::ScenarioSelect); break;
    case Id::CustomCampaigns: Open(Id::CustomCampaigns); break;
    default: Open(Id::MainMenu); break;
    }
}

void Draw(Id screen, SDL_Renderer* r, const Pointer& p) {
    switch (screen) {
    case Id::Campaigns: DrawBook(r, p); break;
    case Id::CampaignMap: DrawMap(r, p); break;
    case Id::ScenarioSelect: DrawSelect(r, p); break;
    case Id::CustomCampaigns: DrawCustom(r, p); break;
    case Id::CampaignEditor: DrawCampaignEditor(r, p); break;
    case Id::Mission: DrawMission(r, p); break;
    case Id::Aftermath: DrawAftermath(r, p); break;
    default: break;
    }
}

void HandleEvent(Id screen, const SDL_Event& e, const Pointer& p, Request& request) {
    switch (screen) {
    case Id::Campaigns: BookEvent(e, p, request); break;
    case Id::CampaignMap: MapEvent(e, p, request); break;
    case Id::ScenarioSelect: SelectEvent(e, p, request); break;
    case Id::CustomCampaigns: CustomEvent(e, request); break;
    case Id::CampaignEditor: CampaignEditorEvent(e, request); break;
    case Id::Mission: MissionEvent(e, p, request); break;
    case Id::Aftermath: AftermathEvent(e, p, request); break;
    default: break;
    }
}

} // namespace Screens::Campaigns
