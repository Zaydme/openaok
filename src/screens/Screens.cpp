#include "Screens.h"

#include "Campaigns.h"
#include "TechTree.h"
#include "Widgets.h"
#include "audio/InterfaceSounds.h"
#include "audio/Music.h"
#include "core/ConfigManager.h"
#include "core/Profile.h"
#include "core/Strings.h"
#include "hal/Audio.h"
#include "hal/GraphicCache.h"
#include "hal/Platform.h"
#include "hal/TextInput.h"
#include "rendering/Text.h"

#include <SDL2/SDL.h>

#include <algorithm>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>

namespace Screens {

namespace Text = Rendering::Text;
using Core::Strings::Get;

namespace {

// ---------------------------------------------------------------------------
// The state every screen shares.

struct Shared {
    Id current = Id::MainMenu;
    Request request;
    int mx = -1;
    int my = -1;
    bool held = false;
    int loading = 0;
};

Shared& S() {
    static Shared shared;
    return shared;
}

void Click() { HAL::Audio::Play(Audio::InterfaceSound::kButton2, 1.0f, 0.0f); }

void Ask(Request::Kind kind) { S().request.kind = kind; }

// ---------------------------------------------------------------------------
// The main menu: the town of interfac.drs 50231, each sign a menu item. The
// item lights when the mouse is over it -- the art's own lit frame -- and its
// help appears on the parchment at the bottom right; Single Player opens its
// own menu over the shop on the right.

struct MainItem {
    int label;          // LANGUAGE.DLL
    int help;
    int frame;          // its first frame in 50231: the sign, lit, pressed, open
    SDL_Point at;       // where the lit frames go, on the art
    SDL_Point label_at; // the label's centre and its cell's top
};

enum MainChoice : int {
    kSinglePlayer, kMultiplayer, kZone, kLearn, kEditor, kHistory, kOptions, kExit,
    kMainChoices,
};

constexpr MainItem kMainItems[kMainChoices] = {
    {9500, 31000, 10, {306, 7}, {371, 22}},
    {9501, 31001, 14, {258, 214}, {311, 224}},
    {9502, 31002, 18, {268, 364}, {305, 373}},
    {9503, 31003, 22, {0, 4}, {63, 17}},
    {9504, 31004, 26, {194, 271}, {232, 279}},
    {9505, 31005, 30, {100, 161}, {158, 172}},
    {9506, 31006, 34, {97, 344}, {150, 353}},
    {9509, 31009, 46, {0, 533}, {80, 563}},
};

constexpr uint32_t kTownArt = 50231;
constexpr uint32_t kTownPalette = 50521;

// The Single Player menu, top to bottom: label, help and y.
struct SubItem {
    int label;
    int help;
    int y;
};
constexpr SubItem kSingleItems[] = {
    {9224, 31030, 88},    // Campaigns
    {9226, 31031, 153},   // Random Map
    {9228, 31032, 203},   // Regicide
    {9227, 31033, 253},   // Death Match
    {9229, 31034, 318},   // Custom Campaign
    {9230, 31035, 368},   // Custom Scenario
    {9223, 31036, 433},   // Saved Game
};
constexpr int kSubX = 476;
constexpr int kSubW = 260;
constexpr int kSubH = 40;

// The Map Editor's menu, on the same plank: Create Scenario, Edit
// Scenario, Campaign Editor at 88, 138 and 203, the title "Map Editor".
constexpr SubItem kEditorItems[] = {
    {9262, 31110, 88},    // Create Scenario
    {9263, 31111, 138},   // Edit Scenario
    {9265, 31112, 203},   // Campaign Editor
};

struct MainMenu {
    Theme theme;
    bool single_open = false;
    bool editor_open = false;
    int pressed = -1;          // a sign held down
    int sub_pressed = -1;
    bool has_campaigns = false;
    bool has_scenarios = false;
    bool has_saves = false;
};

MainMenu& Menu() {
    static MainMenu menu;
    return menu;
}

// Whether the point is on a sign: on its lit frame's own pixels.
int SignAt(int x, int y) {
    const HAL::GraphicAtlas* atlas =
        HAL::GraphicCache::Get().GetInterfaceAtlas(kTownArt, kTownPalette);
    if (atlas == nullptr) return -1;
    for (int i = 0; i < kMainChoices; ++i) {
        const MainItem& item = kMainItems[i];
        const int frame = item.frame + 1;
        if (frame >= static_cast<int>(atlas->frames.size())) continue;
        const HAL::GraphicFrame& f = atlas->frames[frame];
        const int fx = x - (item.at.x - f.hotspot_x);
        const int fy = y - (item.at.y - f.hotspot_y);
        if (atlas->Covers(frame, fx, fy)) return i;
    }
    return -1;
}

SDL_Rect SubRect(int i) {
    if (Menu().editor_open) return SDL_Rect{kSubX, kEditorItems[i].y, kSubW, kSubH};
    return SDL_Rect{kSubX, kSingleItems[i].y, kSubW, kSubH};
}

bool SubEnabled(int i) {
    const MainMenu& menu = Menu();
    // There is no campaign editor: its item stays disabled.
    if (menu.editor_open) return i != 2;
    switch (i) {
    case 4: return menu.has_campaigns;
    case 5: return menu.has_scenarios;
    case 6: return menu.has_saves;
    default: return true;
    }
}

int SubAt(int x, int y) {
    const MainMenu& menu = Menu();
    if (!menu.single_open && !menu.editor_open) return -1;
    const int n = menu.editor_open ? static_cast<int>(std::size(kEditorItems))
                                   : static_cast<int>(std::size(kSingleItems));
    for (int i = 0; i < n; ++i) {
        if (Hit(SubRect(i), x, y)) return i;
    }
    return -1;
}

// The top right corner's close button, on every menu screen.
void DrawCloseBox(SDL_Renderer* r, const Theme& theme) {
    Screens::DrawCloseBox(r, theme, S().held && Hit(kCloseBox, S().mx, S().my));
}

void DrawMainMenu(SDL_Renderer* r) {
    MainMenu& menu = Menu();
    const Theme& theme = menu.theme;
    DrawBackground(r, theme);

    const int over = SignAt(S().mx, S().my);
    for (int i = 0; i < kMainChoices; ++i) {
        const MainItem& item = kMainItems[i];
        int frame = -1;
        if (i == menu.pressed && over == i) frame = item.frame + 2;
        else if (i == over) frame = item.frame + 1;
        else if (i == kSinglePlayer && menu.single_open) frame = item.frame + 3;
        else if (i == kEditor && menu.editor_open) frame = item.frame + 3;
        if (frame >= 0) {
            const HAL::GraphicAtlas* atlas =
                HAL::GraphicCache::Get().GetInterfaceAtlas(kTownArt, kTownPalette);
            if (atlas != nullptr && frame < static_cast<int>(atlas->frames.size())) {
                const HAL::GraphicFrame& f = atlas->frames[frame];
                const SDL_Rect dst = {item.at.x - f.hotspot_x, item.at.y - f.hotspot_y,
                                      f.rect.w, f.rect.h};
                SDL_RenderCopy(r, atlas->base, &f.rect, &dst);
            }
        }
        DrawText(r, Text::kGeorgia9B, item.label_at.x, item.label_at.y, Get(item.label),
                 theme.text, theme.text_shadow, Align::Centre);
    }

    // The help for whatever the mouse is over, on the parchment.
    int help = -1;
    if (over >= 0) help = kMainItems[over].help;
    const int sub = SubAt(S().mx, S().my);
    if (sub >= 0) help = menu.editor_open ? kEditorItems[sub].help : kSingleItems[sub].help;
    if (help >= 0) {
        DrawWrapped(r, Text::kGeorgia9B, 395, 505, 372, Get(help), theme.text,
                    theme.text_shadow);
    }

    if (menu.editor_open) {
        DrawText(r, Text::kBlackletter14, 607, 8, Get(9504), theme.text, theme.text_shadow,
                 Align::Centre);
        for (int i = 0; i < static_cast<int>(std::size(kEditorItems)); ++i) {
            Button button{SubRect(i), Get(kEditorItems[i].label), Text::kBlackletter14, SubEnabled(i)};
            State state = StateOf(button, S().mx, S().my, S().held && menu.sub_pressed == i);
            DrawButton(r, theme, button, state);
        }
    }
    if (menu.single_open) {
        // The title on the plank, and under it the player's name.
        DrawText(r, Text::kBlackletter14, 607, 8, Get(9500), theme.text, theme.text_shadow,
                 Align::Centre);
        DrawText(r, Text::kGeorgia9B, 607, 32, Core::Profile::Current().name, theme.text,
                 theme.text_shadow, Align::Centre);
        for (int i = 0; i < static_cast<int>(std::size(kSingleItems)); ++i) {
            Button button{SubRect(i), Get(kSingleItems[i].label), Text::kBlackletter14,
                          SubEnabled(i)};
            State state = StateOf(button, S().mx, S().my, S().held && menu.sub_pressed == i);
            DrawButton(r, theme, button, state);
        }
    }
    DrawCloseBox(r, theme);
}

void OpenSetup(int kind);   // the game setup screen, below
void OpenFileList(Id which, bool restoring = false);

void MainMenuEvent(const SDL_Event& e) {
    MainMenu& menu = Menu();
    if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
        menu.pressed = SignAt(e.button.x, e.button.y);
        menu.sub_pressed = SubAt(e.button.x, e.button.y);
        if (menu.pressed >= 0) HAL::Audio::Play(Audio::InterfaceSound::kButton1, 1.0f, 0.0f);
    } else if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
        const int sign = SignAt(e.button.x, e.button.y);
        const int sub = SubAt(e.button.x, e.button.y);
        if (Hit(kCloseBox, e.button.x, e.button.y)) {
            Click();
            Ask(Request::Kind::Quit);
        } else if (sign >= 0 && sign == menu.pressed) {
            switch (sign) {
            case kSinglePlayer:
                menu.single_open = !menu.single_open;
                menu.editor_open = false;
                break;
            case kMultiplayer: Open(Id::Multiplayer); break;
            case kZone: break;   // the Gaming Zone is long gone
            // Learn to Play: straight to William Wallace's map, its way
            // back the main menu.
            case kLearn: OpenCampaignMap(8, true); break;
            case kEditor:
                // The Map Editor's own menu on the plank.
                menu.editor_open = !menu.editor_open;
                menu.single_open = false;
                break;
            case kHistory: Open(Id::History); break;
            case kOptions: Open(Id::Options); break;
            case kExit: Ask(Request::Kind::Quit); break;
            default: break;
            }
        } else if (sub >= 0 && sub == menu.sub_pressed && SubEnabled(sub) && menu.editor_open) {
            Click();
            menu.editor_open = false;
            if (sub == 0) Ask(Request::Kind::CreateScenario);
            else if (sub == 1) Ask(Request::Kind::EditScenario);
        } else if (sub >= 0 && sub == menu.sub_pressed && SubEnabled(sub)) {
            Click();
            switch (sub) {
            case 0: Open(Id::Campaigns); break;
            case 1: OpenSetup(0); break;
            case 2: OpenSetup(1); break;
            case 3: OpenSetup(2); break;
            case 4: OpenFileList(Id::CustomCampaigns); break;
            case 5: Open(Id::ScenarioSelect); break;
            case 6: OpenFileList(Id::SavedGames); break;
            default: break;
            }
        }
        menu.pressed = -1;
        menu.sub_pressed = -1;
    } else if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE) {
        if (menu.single_open) menu.single_open = false;
        if (menu.editor_open) menu.editor_open = false;
    }
}

// ---------------------------------------------------------------------------
// The game setup screen: a single-player Random Map, Regicide or Death Match,
// on the parchment with the wooden chest (50103). The players on the left,
// the game's settings in the inset on the right, and the buttons to start or
// go back below.

constexpr int kMaxComputers = 7;

struct PlayerRow {
    DropDown civ;
    DropDown colour;
    DropDown team;
};

struct Setup {
    Theme theme;
    int kind = 0;   // 0 Random Map, 1 Regicide, 2 Death Match
    PlayerRow you;
    PlayerRow computers[kMaxComputers];
    DropDown difficulty, others, map, size, resources, population, reveal, age, victory;
    CheckBox team_together, lock_teams, all_techs, record;
    Button start, cancel, reset, tech_tree;
};

Setup& SetupState() {
    static Setup setup;
    return setup;
}

// The settings rows: label on the left, the drop-down on the right.
constexpr int kSettingLabelRight = 649;
constexpr int kSettingX = 677;
constexpr int kSettingW = 114;
constexpr int kSettingH = 21;
constexpr int kSettingRows[] = {88, 117, 146, 175, 204, 233, 262, 291, 320};

// The player table's columns.
constexpr int kNameX = 58;
constexpr int kCivX = 240;
constexpr int kCivW = 110;
constexpr int kColourX = 367;
constexpr int kTeamX = 456;
constexpr int kBoxW = 30;
constexpr int kRowH = 20;
constexpr int kYouY = 87;
constexpr int kComputerY = 160;
constexpr int kComputerPitch = 24;

std::vector<std::string> CivNames() {
    std::vector<std::string> names;
    names.push_back(Get(10230));            // Random
    for (int civ = 1; civ <= 13; ++civ) names.push_back(Get(10230 + civ));
    return names;
}

std::vector<std::string> Numbers(int from, int to) {
    std::vector<std::string> out;
    for (int n = from; n <= to; ++n) out.push_back(std::to_string(n));
    return out;
}

void ResetSetup(Setup& s) {
    const auto civs = CivNames();
    auto row = [&](PlayerRow& p, int seat) {
        p.civ = DropDown{SDL_Rect{kCivX, 0, kCivW, kRowH}, civs, 0};
        p.civ.font = Text::kGeorgia9B;
        p.civ.plain = true;
        p.colour = DropDown{SDL_Rect{kColourX, 0, kBoxW, kRowH}, Numbers(1, 8), seat - 1};
        p.colour.font = Text::kGeorgia9B;
        p.colour.boxed = true;
        p.colour.coloured = true;
        std::vector<std::string> teams = {Get(9648)};
        for (const auto& n : Numbers(1, 4)) teams.push_back(n);
        p.team = DropDown{SDL_Rect{kTeamX, 0, kBoxW, kRowH}, teams, 0};
        p.team.font = Text::kGeorgia9B;
        p.team.boxed = true;
    };
    row(s.you, 1);
    s.you.civ.rect.y = s.you.colour.rect.y = s.you.team.rect.y = kYouY;
    for (int i = 0; i < kMaxComputers; ++i) {
        row(s.computers[i], i + 2);
        const int y = kComputerY + i * kComputerPitch;
        s.computers[i].civ.rect.y = s.computers[i].colour.rect.y = s.computers[i].team.rect.y = y;
    }
    auto setting = [&](DropDown& d, int row_index, std::vector<std::string> items, int selected) {
        d = DropDown{SDL_Rect{kSettingX, kSettingRows[row_index], kSettingW, kSettingH},
                     std::move(items), selected};
        d.font = Text::kGeorgia9B;
    };
    setting(s.difficulty, 0, {Get(11216), Get(11217), Get(11218), Get(11219), Get(11220)}, 3);
    setting(s.others, 1, Numbers(1, kMaxComputers), 0);
    std::vector<std::string> maps;
    for (int id = 10875; id <= 10890; ++id) maps.push_back(Get(id));
    setting(s.map, 2, maps, 0);
    std::vector<std::string> sizes;
    for (int id = 10611; id <= 10616; ++id) sizes.push_back(Get(id));
    setting(s.size, 3, sizes, 0);
    setting(s.resources, 4, {Get(9702), Get(9703), Get(9704)}, 0);
    std::vector<std::string> population;
    for (int n = 25; n <= 200; n += 25) population.push_back(std::to_string(n));
    setting(s.population, 5, population, 2);
    setting(s.reveal, 6, {Get(9755), Get(9756), Get(9757)}, 0);
    setting(s.age, 7, {Get(4332), Get(4202), Get(4203), Get(4204)}, 0);
    setting(s.victory, 8, {Get(4327), Get(4321)}, 0);
    s.team_together = CheckBox{SDL_Rect{613, 373, 24, 26}, true};
    s.lock_teams = CheckBox{SDL_Rect{740, 373, 24, 26}, true};
    s.all_techs = CheckBox{SDL_Rect{613, 401, 24, 26}, false};
    s.record = CheckBox{SDL_Rect{740, 401, 24, 26}, false};
    s.start = Button{SDL_Rect{37, 551, 160, 35}, Get(9710), Text::kBlackletter14};
    s.cancel = Button{SDL_Rect{217, 551, 163, 35}, Get(4002), Text::kBlackletter14};
    s.reset = Button{SDL_Rect{681, 52, 94, 28}, Get(9305), Text::kBlackletter14};
    s.tech_tree = Button{SDL_Rect{300, 491, 36, 36}, std::string{}, Text::kBlackletter14};
}

void OpenSetup(int kind) {
    Setup& s = SetupState();
    s.kind = kind;
    ResetSetup(s);
    Open(Id::GameSetup);
}

std::vector<DropDown*> SetupDropDowns(Setup& s) {
    std::vector<DropDown*> all = {&s.difficulty, &s.others, &s.map, &s.size};
    if (s.kind != 2) all.push_back(&s.resources);
    for (DropDown* d : {&s.population, &s.reveal, &s.age, &s.victory}) all.push_back(d);
    all.push_back(&s.you.civ);
    all.push_back(&s.you.colour);
    all.push_back(&s.you.team);
    // A computer player's colour follows its seat and has no box.
    const int others = s.others.selected + 1;
    for (int i = 0; i < others; ++i) {
        all.push_back(&s.computers[i].civ);
        all.push_back(&s.computers[i].team);
    }
    return all;
}

void DrawSetup(SDL_Renderer* r) {
    Setup& s = SetupState();
    const Theme& theme = s.theme;
    DrawBackground(r, theme);
    const SDL_Color ink = {0, 0, 0, 255};

    // The title, and the column heads in black letter.
    static const int kTitles[3] = {9653, 9753, 9751};
    DrawText(r, Text::kBlackletter21B, 400, 6, Get(kTitles[s.kind]), ink, ink, Align::Centre,
             false);
    DrawText(r, Text::kBlackletter14, kNameX - 2, 54, Get(9680), ink, ink, Align::Left, false);
    DrawText(r, Text::kBlackletter14, kCivX, 54, Get(9681), ink, ink, Align::Left, false);
    DrawText(r, Text::kBlackletter14, kColourX, 54, Get(9646), ink, ink, Align::Left, false);
    DrawText(r, Text::kBlackletter14, kTeamX - 12, 54, Get(9647), ink, ink, Align::Left, false);
    DrawText(r, Text::kBlackletter14, kNameX - 2, 127, Get(13541), ink, ink, Align::Left, false);

    // The players: the one at this machine, then the computer players.
    DrawText(r, Text::kGeorgia9B, kNameX, kYouY + 2, Core::Profile::Current().name, ink, ink,
             Align::Left, false);
    const int others = s.others.selected + 1;
    for (int i = 0; i < others; ++i) {
        DrawText(r, Text::kGeorgia9B, kNameX, kComputerY + i * kComputerPitch + 2, Get(9683),
                 ink, ink, Align::Left, false);
    }

    // The settings, each label right-aligned against its drop-down.
    DrawText(r, Text::kBlackletter14, 532, 50, Get(9682), ink, ink, Align::Left, false);
    struct Row { int label; const DropDown* d; };
    const Row rows[] = {
        {13513, &s.difficulty}, {13541, &s.others}, {13511, &s.map}, {13512, &s.size},
        {13514, &s.resources}, {13516, &s.population}, {13522, &s.reveal}, {13515, &s.age},
        {13518, &s.victory},
    };
    for (const Row& row : rows) {
        if (row.d == &s.resources && s.kind == 2) continue;   // Death Match fixes them
        DrawText(r, Text::kGeorgia9B, kSettingLabelRight, row.d->rect.y + 3, Get(row.label),
                 ink, ink, Align::Right, false);
    }
    struct Check { int label; const CheckBox* c; };
    const Check checks[] = {
        {13521, &s.team_together}, {13523, &s.lock_teams}, {13524, &s.all_techs},
        {13542, &s.record},
    };
    for (const Check& c : checks) {
        DrawText(r, Text::kGeorgia9B, c.c->box.x - 7, c.c->box.y + 5, Get(c.label), ink, ink,
                 Align::Right, false);
    }
    for (const CheckBox* c : {&s.team_together, &s.lock_teams, &s.all_techs, &s.record}) {
        DrawCheckBox(r, theme, *c, S().mx, S().my, S().held);
    }
    for (const Button* b : {&s.start, &s.cancel, &s.reset}) {
        DrawButton(r, theme, *b, StateOf(*b, S().mx, S().my, S().held));
    }
    // The technology tree: the button art's own picture of it.
    DrawButtonArt(r, theme, 16, s.tech_tree.rect.x, s.tech_tree.rect.y);

    // The player number in its own colour: the dat's pure player colour.
    {
        const int index = Core::ConfigManager::Get().ColourMinimapIndex(s.you.colour.selected);
        const HAL::PaletteColor c = HAL::GraphicCache::Get().GetPaletteColor(static_cast<size_t>(index));
        s.you.colour.colour = SDL_Color{c.r, c.g, c.b, 255};
    }

    // Drop-downs last, an open one over everything.
    DropDown* open = nullptr;
    for (DropDown* d : SetupDropDowns(s)) {
        if (d->open) { open = d; continue; }
        DrawDropDown(r, theme, *d, S().mx, S().my, S().held, kMenuHeight);
    }
    if (open != nullptr) DrawDropDown(r, theme, *open, S().mx, S().my, S().held, kMenuHeight);
    DrawCloseBox(r, theme);
}

// The game the setup screen describes.
Core::GameSetup BuildGame(const Setup& s) {
    Core::GameSetup game = Core::DefaultSetup();
    game.seed = static_cast<uint32_t>(std::time(nullptr)) ^ SDL_GetTicks();
    uint32_t roll = game.seed * 2654435761u;
    auto pick = [&](int n) {
        roll = roll * 1664525u + 1013904223u;
        return static_cast<int>((roll >> 8) % static_cast<uint32_t>(n));
    };
    // The map: the random map script by its name; Random draws one.
    static const char* kScripts[] = {
        "Arabia", "Archipelago", "Baltic", "Black Forest", "Coastal", "Continental",
        "Crater Lake", "Fortress", "Gold Rush", "Highland", "Islands", "Mediterranean",
        "Migration", "Rivers", "Team Islands"};
    const int map = s.map.selected;
    game.map_script = map < 15 ? kScripts[map] : kScripts[pick(15)];
    game.map_size = Core::kMapSizes[std::clamp(s.size.selected, 0, 5)].tiles;
    game.resources = s.kind == 2 ? Core::StartingResources::DeathMatch
                   : s.resources.selected == 1 ? Core::StartingResources::Medium
                   : s.resources.selected == 2 ? Core::StartingResources::High
                   : Core::StartingResources::Standard;
    game.population_limit = 25 * (s.population.selected + 1);
    game.reveal = s.reveal.selected == 1 ? Core::Reveal::Explored
                : s.reveal.selected == 2 ? Core::Reveal::AllVisible : Core::Reveal::Normal;
    game.starting_age = s.age.selected;
    game.victory = s.victory.selected == 1 ? Core::VictoryKind::Conquest
                                           : Core::VictoryKind::Standard;
    game.regicide = s.kind == 1;
    game.difficulty = s.difficulty.selected;
    game.lock_teams = s.lock_teams.checked;
    game.all_techs = s.all_techs.checked;
    game.record = s.record.checked;
    // A game against the computer allows cheats: its lobby has no box for
    // them (only the multiplayer lobby's "Allow Cheats", 13526, does).
    game.cheats = true;
    auto civ_of = [&](const DropDown& d) { return d.selected == 0 ? 1 + pick(13) : d.selected; };
    auto team_of = [&](const DropDown& d) { return static_cast<Core::TeamId>(d.selected); };
    for (int seat = 1; seat < Core::kMaxPlayers; ++seat) game.players[seat] = Core::PlayerSetup{};
    game.players[1] = Core::PlayerSetup{true, civ_of(s.you.civ), team_of(s.you.team), false,
                                        Core::Profile::Current().name};
    game.players[1].colour = s.you.colour.selected;
    const int others = s.others.selected + 1;
    for (int i = 0; i < others; ++i) {
        const PlayerRow& row = s.computers[i];
        game.players[2 + i] = Core::PlayerSetup{true, civ_of(row.civ), team_of(row.team), true};
        game.players[2 + i].colour = row.colour.selected;
    }
    return game;
}

void SetupEvent(const SDL_Event& e) {
    Setup& s = SetupState();
    if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
        // An open list takes the click first, whether it chooses or closes.
        for (DropDown* d : SetupDropDowns(s)) {
            if (d->open) {
                const int before = d->selected;
                ClickDropDown(*d, e.button.x, e.button.y, kMenuHeight);
                Click();
                // Choosing the player's civilisation plays its theme (RANDOM
                // for a random civ).
                if (d == &s.you.civ && d->selected != before) Audio::Music::Civ(d->selected);
                return;
            }
        }
        for (DropDown* d : SetupDropDowns(s)) {
            if (ClickDropDown(*d, e.button.x, e.button.y, kMenuHeight)) {
                Click();
                return;
            }
        }
        for (CheckBox* c : {&s.team_together, &s.lock_teams, &s.all_techs, &s.record}) {
            if (c->enabled && Hit(c->box, e.button.x, e.button.y)) {
                c->checked = !c->checked;
                Click();
                return;
            }
        }
    } else if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
        if (Hit(s.start.rect, e.button.x, e.button.y)) {
            Click();
            S().request.setup = BuildGame(s);
            Ask(Request::Kind::StartGame);
        } else if (Hit(s.cancel.rect, e.button.x, e.button.y)) {
            Click();
            Open(Id::MainMenu);
        } else if (Hit(s.reset.rect, e.button.x, e.button.y)) {
            Click();
            ResetSetup(s);
        } else if (Hit(s.tech_tree.rect, e.button.x, e.button.y)) {
            Click();
            TechTree::OpenPregame(s.you.civ.selected > 0 ? s.you.civ.selected : 1);
        } else if (Hit(kCloseBox, e.button.x, e.button.y)) {
            Click();
            Ask(Request::Kind::Quit);
        }
    } else if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE) {
        Open(Id::MainMenu);
    }
}

// ---------------------------------------------------------------------------
// Options, from the main menu: the player's name, the screen size, the
// volumes and the scroll speed, and the mouse and garrisoning choices, kept
// in the profile.

struct OptionsScreen {
    Theme theme;
    TextField name;
    Button sizes[3];
    Slider music, sound, scroll;
    CheckBox taunts, one_click, two_button, widescreen;
    Slider top_bar, panel, text;   // the interface's sizes, 100% to 200%
    Button ok, cancel;
    int chosen_size = 1;
};

OptionsScreen& OptionsState() {
    static OptionsScreen options;
    return options;
}

void OpenOptions() {
    OptionsScreen& o = OptionsState();
    const auto& profile = Core::Profile::Current();
    o.name = TextField{SDL_Rect{300, 100, 260, 26}, profile.name, 32, true, Text::kGeorgia10B};
    static const int kSizeNames[3] = {9522, 9523, 9524};
    for (int i = 0; i < 3; ++i) {
        o.sizes[i] = Button{SDL_Rect{300 + i * 140, 160, 130, 30}, Get(kSizeNames[i]),
                            Text::kGeorgia10B};
    }
    o.chosen_size = profile.screen_width <= 800 ? 0 : profile.screen_width <= 1024 ? 1 : 2;
    o.music = Slider{SDL_Rect{300, 230, 260, 22}, profile.music_volume, 10};
    o.sound = Slider{SDL_Rect{300, 280, 260, 22}, profile.sound_volume, 10};
    o.scroll = Slider{SDL_Rect{300, 330, 260, 22}, profile.scroll_speed, 10};
    o.taunts = CheckBox{SDL_Rect{300, 380, 30, 27}, profile.audio_taunts};
    o.one_click = CheckBox{SDL_Rect{300, 415, 30, 27}, profile.one_click_garrison};
    o.two_button = CheckBox{SDL_Rect{300, 450, 30, 27}, profile.two_button_mouse};
    o.widescreen = CheckBox{SDL_Rect{300, 485, 30, 27}, profile.widescreen};
    o.top_bar = SizeSlider(SDL_Rect{590, 245, 160, 22}, profile.top_bar_size);
    o.panel = SizeSlider(SDL_Rect{590, 315, 160, 22}, profile.panel_size);
    o.text = SizeSlider(SDL_Rect{590, 385, 160, 22}, profile.text_size);
    o.ok = Button{SDL_Rect{37, 551, 160, 35}, "OK", Text::kBlackletter14};
    o.cancel = Button{SDL_Rect{217, 551, 163, 35}, Get(4002), Text::kBlackletter14};
}

void DrawOptions(SDL_Renderer* r) {
    OptionsScreen& o = OptionsState();
    const Theme& theme = o.theme;
    DrawBackground(r, theme);
    const SDL_Color ink = {0, 0, 0, 255};
    DrawText(r, Text::kBlackletter28B, 400, 20, Get(9506), ink, ink, Align::Centre, false);
    DrawText(r, Text::kGeorgia10B, 280, 104, Get(9612), ink, ink, Align::Right, false);
    DrawTextField(r, theme, o.name);
    DrawText(r, Text::kGeorgia10B, 280, 166, Get(9521), ink, ink, Align::Right, false);
    for (int i = 0; i < 3; ++i) {
        State state = StateOf(o.sizes[i], S().mx, S().my, S().held);
        if (i == o.chosen_size && state == State::Normal) state = State::Selected;
        DrawButton(r, theme, o.sizes[i], state);
    }
    struct Labelled { int label; const Slider* s; int low; int high; };
    const Labelled sliders[] = {
        {9515, &o.music, 9517, 9516}, {9514, &o.sound, 9517, 9516}, {9518, &o.scroll, 9520, 9519},
    };
    for (const auto& l : sliders) {
        std::string label = Get(l.label);
        std::replace(label.begin(), label.end(), '\n', ' ');
        DrawText(r, Text::kGeorgia10B, 280, l.s->track.y + 2, label, ink, ink, Align::Right, false);
        DrawSlider(r, theme, *l.s);
        DrawText(r, Text::kArial8, l.s->track.x, l.s->track.y + 24, Get(l.low), ink, ink,
                 Align::Left, false);
        DrawText(r, Text::kArial8, l.s->track.x + l.s->track.w, l.s->track.y + 24, Get(l.high),
                 ink, ink, Align::Right, false);
    }
    struct Box { int label; const CheckBox* c; };
    const Box boxes[] = {{9526, &o.taunts}, {9527, &o.one_click}, {9528, &o.two_button}};
    for (const auto& b : boxes) {
        DrawCheckBox(r, theme, *b.c, S().mx, S().my, S().held);
        DrawText(r, Text::kGeorgia10B, b.c->box.x + b.c->box.w + 10, b.c->box.y + 5, Get(b.label),
                 ink, ink, Align::Left, false);
    }
    // The frame as wide as the screen (Profile::Options::widescreen); its
    // label is in no string table.
    DrawCheckBox(r, theme, o.widescreen, S().mx, S().my, S().held);
    DrawText(r, Text::kGeorgia10B, o.widescreen.box.x + o.widescreen.box.w + 10, o.widescreen.box.y + 5,
             "Widescreen", ink, ink, Align::Left, false);
    // The interface's sizes (Profile::Options::top_bar_size...), likewise.
    DrawSizeSlider(r, theme, o.top_bar, "Top Bar Size");
    DrawSizeSlider(r, theme, o.panel, "Panel Size");
    DrawSizeSlider(r, theme, o.text, "Text Size");
    DrawButton(r, theme, o.ok, StateOf(o.ok, S().mx, S().my, S().held));
    DrawButton(r, theme, o.cancel, StateOf(o.cancel, S().mx, S().my, S().held));
    DrawCloseBox(r, theme);
}

void OptionsEvent(const SDL_Event& e) {
    OptionsScreen& o = OptionsState();
    if (TypeIntoField(o.name, e)) return;
    if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
        for (Slider* s : {&o.music, &o.sound, &o.scroll, &o.top_bar, &o.panel, &o.text}) {
            if (DragSlider(*s, e.button.x, e.button.y)) return;
        }
        for (CheckBox* c : {&o.taunts, &o.one_click, &o.two_button, &o.widescreen}) {
            if (Hit(c->box, e.button.x, e.button.y)) { c->checked = !c->checked; Click(); return; }
        }
        o.name.focused = Hit(o.name.rect, e.button.x, e.button.y);
    } else if (e.type == SDL_MOUSEMOTION && S().held) {
        for (Slider* s : {&o.music, &o.sound, &o.scroll, &o.top_bar, &o.panel, &o.text}) {
            DragSlider(*s, e.motion.x, e.motion.y);
        }
    } else if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
        for (int i = 0; i < 3; ++i) {
            if (Hit(o.sizes[i].rect, e.button.x, e.button.y)) { o.chosen_size = i; Click(); }
        }
        if (Hit(o.ok.rect, e.button.x, e.button.y)) {
            Click();
            auto& profile = Core::Profile::Current();
            if (!o.name.text.empty()) profile.name = o.name.text;
            static const int kW[3] = {800, 1024, 1280};
            static const int kH[3] = {600, 768, 1024};
            profile.screen_width = kW[o.chosen_size];
            profile.screen_height = kH[o.chosen_size];
            profile.music_volume = o.music.value;
            profile.sound_volume = o.sound.value;
            profile.scroll_speed = o.scroll.value;
            profile.audio_taunts = o.taunts.checked;
            profile.one_click_garrison = o.one_click.checked;
            profile.two_button_mouse = o.two_button.checked;
            profile.widescreen = o.widescreen.checked;
            profile.top_bar_size = SizeOf(o.top_bar);
            profile.panel_size = SizeOf(o.panel);
            profile.text_size = SizeOf(o.text);
            Core::Profile::Save(profile);
            HAL::Audio::SetVolume(static_cast<float>(profile.sound_volume) / 10.0f);
            Audio::Music::SetVolume(profile.music_volume);
            Open(Id::MainMenu);
        } else if (Hit(o.cancel.rect, e.button.x, e.button.y)) {
            Click();
            Open(Id::MainMenu);
        } else if (Hit(kCloseBox, e.button.x, e.button.y)) {
            Ask(Request::Kind::Quit);
        }
    } else if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE) {
        Open(Id::MainMenu);
    }
}

// ---------------------------------------------------------------------------
// The history: the book of 50161, its topics down the left page and the one
// chosen on the right, from the CD's History text files, which list their
// own topics in history.txt.

struct HistoryScreen {
    Theme theme;
    std::vector<std::string> titles;
    std::vector<std::string> files;
    int selected = 0;
    int scroll = 0;            // topic list
    int text_scroll = 0;       // lines of the chosen topic
    std::string text;
    Button back;
};

HistoryScreen& HistoryState() {
    static HistoryScreen history;
    return history;
}

std::string ReadFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    std::stringstream buffer;
    buffer << in.rdbuf();
    std::string text = buffer.str();
    // The files are Windows text in Latin-1; the fonts read UTF-8.
    std::string out;
    out.reserve(text.size());
    for (unsigned char c : text) {
        if (c == '\r') continue;
        if (c < 0x80) { out += static_cast<char>(c); continue; }
        out += static_cast<char>(0xC0 | (c >> 6));
        out += static_cast<char>(0x80 | (c & 0x3F));
    }
    return out;
}

constexpr const char* kHistoryDir = "../assets/original/History/";

void LoadHistoryTopic(HistoryScreen& h) {
    h.text.clear();
    h.text_scroll = 0;
    if (h.selected < 0 || h.selected >= static_cast<int>(h.files.size())) return;
    h.text = ReadFile(std::string(kHistoryDir) + h.files[h.selected]);
}

void OpenHistory() {
    HistoryScreen& h = HistoryState();
    h.titles.clear();
    h.files.clear();
    // history.txt: an RC string table -- 20311 on the titles, 20411 on the
    // files, in step.
    std::istringstream in(ReadFile(std::string(kHistoryDir) + "history.txt"));
    std::string line;
    std::vector<std::pair<int, std::string>> titles, files;
    while (std::getline(in, line)) {
        std::istringstream row(line);
        int id = 0;
        if (!(row >> id)) continue;
        const size_t q1 = line.find('"');
        const size_t q2 = q1 == std::string::npos ? q1 : line.find('"', q1 + 1);
        if (q2 == std::string::npos) continue;
        const std::string value = line.substr(q1 + 1, q2 - q1 - 1);
        if (id >= 20311 && id < 20400) titles.emplace_back(id - 20311, value);
        if (id >= 20411 && id < 20500) files.emplace_back(id - 20411, value);
    }
    for (const auto& t : titles) {
        for (const auto& f : files) {
            if (f.first == t.first) {
                h.titles.push_back(t.second);
                std::string file = f.second;
                if (file.find('.') == std::string::npos) file += ".txt";
                h.files.push_back(file);
            }
        }
    }
    h.selected = 0;
    h.scroll = 0;
    LoadHistoryTopic(h);
    h.back = Button{SDL_Rect{564, 560, 196, 30}, Get(20300), Text::kBlackletter14};
}

constexpr SDL_Rect kHistoryList = {14, 14, 106, 316};
constexpr int kHistoryRowH = 15;

void DrawHistory(SDL_Renderer* r) {
    HistoryScreen& h = HistoryState();
    const Theme& theme = h.theme;
    DrawBackground(r, theme);
    const SDL_Color ink = {0, 0, 0, 255};
    const SDL_Color lit = theme.focus;
    // The topics, one a line in black letter, the chosen one lit.
    const int rows = kHistoryList.h / kHistoryRowH;
    for (int i = 0; i < rows && h.scroll + i < static_cast<int>(h.titles.size()); ++i) {
        const int index = h.scroll + i;
        const SDL_Rect row = {kHistoryList.x, kHistoryList.y + i * kHistoryRowH, kHistoryList.w,
                              kHistoryRowH};
        const bool over = Hit(row, S().mx, S().my);
        Text::Draw(r, Text::kBlackletter12B, row.x, row.y, h.titles[index],
                   index == h.selected ? lit : over ? theme.text : ink);
    }
    // The chosen topic: its text on the right page, its first line in bold.
    std::vector<std::string_view> lines;
    std::string body = h.text;
    // <b>..<b> titles: shown bold, the markup dropped.
    std::string clean;
    bool bold_line = false;
    for (size_t i = 0; i < body.size(); ++i) {
        if (body.compare(i, 3, "<b>") == 0) { i += 2; bold_line = true; continue; }
        clean += body[i];
    }
    (void)bold_line;
    Text::Wrap(Text::kGeorgia9, clean, 400, lines);
    const int line_h = Text::Height(Text::kGeorgia9);
    const int shown = 30;
    for (int i = 0; i < shown && h.text_scroll + i < static_cast<int>(lines.size()); ++i) {
        const int index = h.text_scroll + i;
        Text::Draw(r, index == 0 ? Text::kGeorgia9B : Text::kGeorgia9, 330,
                   80 + i * line_h, lines[index], ink);
    }
    DrawButton(r, theme, h.back, StateOf(h.back, S().mx, S().my, S().held));
}

void HistoryEvent(const SDL_Event& e) {
    HistoryScreen& h = HistoryState();
    if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
        if (Hit(h.back.rect, e.button.x, e.button.y)) { Click(); Open(Id::MainMenu); return; }
        if (Hit(kHistoryList, e.button.x, e.button.y)) {
            const int index = h.scroll + (e.button.y - kHistoryList.y) / kHistoryRowH;
            if (index >= 0 && index < static_cast<int>(h.titles.size())) {
                h.selected = index;
                LoadHistoryTopic(h);
                Click();
            }
        }
    } else if (e.type == SDL_MOUSEWHEEL) {
        if (S().mx < 300) {
            h.scroll = std::clamp(h.scroll - e.wheel.y, 0,
                                  std::max(0, static_cast<int>(h.titles.size()) - 21));
        } else {
            h.text_scroll = std::max(0, h.text_scroll - e.wheel.y * 3);
        }
    } else if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE) {
        Open(Id::MainMenu);
    }
}

// ---------------------------------------------------------------------------
// The multiplayer connection screen, on the plain parchment: the player's
// name, a game to join by its address, or a new game to host.

struct MultiplayerScreen {
    Theme theme;
    TextField address;
    Button join, create, restore, cancel;
};

MultiplayerScreen& MultiplayerState() {
    static MultiplayerScreen mp;
    return mp;
}

void OpenMultiplayer() {
    MultiplayerScreen& m = MultiplayerState();
    m.address = TextField{SDL_Rect{300, 200, 260, 26}, m.address.text.empty() ? "127.0.0.1"
                                                                                : m.address.text,
                          64, true, Text::kGeorgia10B};
    m.join = Button{SDL_Rect{300, 260, 160, 35}, Get(9632), Text::kBlackletter14};
    m.create = Button{SDL_Rect{300, 310, 160, 35}, Get(9634), Text::kBlackletter14};
    // "Create (Restore)": a saved game hosted again (LANGUAGE.DLL 9637,
    // 31055).
    m.restore = Button{SDL_Rect{470, 310, 160, 35}, Get(9637), Text::kBlackletter14};
    m.cancel = Button{SDL_Rect{217, 551, 163, 35}, Get(4002), Text::kBlackletter14};
}

void DrawMultiplayer(SDL_Renderer* r) {
    MultiplayerScreen& m = MultiplayerState();
    const Theme& theme = m.theme;
    DrawBackground(r, theme);
    const SDL_Color ink = {0, 0, 0, 255};
    DrawText(r, Text::kBlackletter28B, 400, 20, Get(9611), ink, ink, Align::Centre, false);
    DrawText(r, Text::kGeorgia10B, 280, 120, Get(9612), ink, ink, Align::Right, false);
    DrawText(r, Text::kGeorgia10B, 300, 120, Core::Profile::Current().name, ink, ink,
             Align::Left, false);
    DrawText(r, Text::kGeorgia10B, 280, 160, Get(9613), ink, ink, Align::Right, false);
    DrawText(r, Text::kGeorgia10B, 300, 160, Get(9620), ink, ink, Align::Left, false);
    DrawText(r, Text::kGeorgia10B, 280, 204, Get(9711), ink, ink, Align::Right, false);
    DrawTextField(r, theme, m.address);
    for (const Button* b : {&m.join, &m.create, &m.restore, &m.cancel}) {
        DrawButton(r, theme, *b, StateOf(*b, S().mx, S().my, S().held));
    }
    DrawWrapped(r, Text::kGeorgia9B, 300, 380, 400, Get(9643), ink, ink, false);
    DrawCloseBox(r, theme);
}

void MultiplayerEvent(const SDL_Event& e) {
    MultiplayerScreen& m = MultiplayerState();
    if (TypeIntoField(m.address, e)) return;
    if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
        if (Hit(m.join.rect, e.button.x, e.button.y)) {
            Click();
            S().request.address = m.address.text;
            Ask(Request::Kind::JoinGame);
        } else if (Hit(m.create.rect, e.button.x, e.button.y)) {
            Click();
            // The host chooses the game on the setup screen, as a network
            // game's host does.
            Setup& s = SetupState();
            s.kind = 0;
            ResetSetup(s);
            S().request.setup = BuildGame(s);
            S().request.port = 27015;
            Ask(Request::Kind::HostGame);
        } else if (Hit(m.restore.rect, e.button.x, e.button.y)) {
            Click();
            OpenFileList(Id::SavedGames, true);
        } else if (Hit(m.cancel.rect, e.button.x, e.button.y)) {
            Click();
            Open(Id::MainMenu);
        } else if (Hit(kCloseBox, e.button.x, e.button.y)) {
            Ask(Request::Kind::Quit);
        }
    } else if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE) {
        Open(Id::MainMenu);
    }
}

// ---------------------------------------------------------------------------
// Lists of files to choose from: custom campaigns, saved games and recorded
// games, on the plain parchment.

struct FileList {
    Theme theme;
    Id which = Id::SavedGames;
    ListBox list;
    std::vector<std::string> paths;
    Button ok, cancel;
    // Chosen to host again as a network game, from the multiplayer screen.
    bool restoring = false;
};

FileList& Files() {
    static FileList files;
    return files;
}

} // namespace

std::vector<std::string> FilesIn(const std::string& dir, const std::vector<std::string>& exts) {
    std::vector<std::string> out;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
        if (!entry.is_regular_file()) continue;
        std::string ext = entry.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        if (std::find(exts.begin(), exts.end(), ext) != exts.end()) {
            out.push_back(entry.path().string());
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

std::string ScenariosDir() {
    const std::string base = HAL::Platform::PrefPath("Scenario");
    return base.empty() ? "../assets/original/Scenario/" : base;
}

std::string SavedGamesDir() {
    const std::string base = HAL::Platform::PrefPath("SaveGame");
    return base.empty() ? "../assets/original/SaveGame/" : base;
}

namespace {

void OpenFileList(Id which, bool restoring) {
    FileList& f = Files();
    f.which = which;
    f.restoring = restoring;
    if (which == Id::CustomCampaigns) {
        f.paths = FilesIn("../assets/original/Campaign", {".cpn"});
    } else {
        f.paths = FilesIn(SavedGamesDir(), {".gam", ".mgl"});
    }
    f.list = ListBox{SDL_Rect{150, 100, 500, 400}, {}, f.paths.empty() ? -1 : 0, 0,
                     Text::kGeorgia10B};
    for (const auto& path : f.paths) {
        f.list.items.push_back(std::filesystem::path(path).stem().string());
    }
    f.ok = Button{SDL_Rect{37, 551, 160, 35}, "OK", Text::kBlackletter14};
    f.cancel = Button{SDL_Rect{217, 551, 163, 35}, Get(4002), Text::kBlackletter14};
    Open(which);
}

void DrawFileList(SDL_Renderer* r) {
    FileList& f = Files();
    const Theme& theme = f.theme;
    DrawBackground(r, theme);
    const SDL_Color ink = {0, 0, 0, 255};
    const int title = f.which == Id::CustomCampaigns ? 11214 : 9223;
    DrawText(r, Text::kBlackletter28B, 400, 20, Get(title), ink, ink, Align::Centre, false);
    DrawListBox(r, theme, f.list, S().mx, S().my);
    Button ok = f.ok;
    ok.enabled = f.list.selected >= 0;
    DrawButton(r, theme, ok, StateOf(ok, S().mx, S().my, S().held));
    DrawButton(r, theme, f.cancel, StateOf(f.cancel, S().mx, S().my, S().held));
    DrawCloseBox(r, theme);
}

void FileListEvent(const SDL_Event& e) {
    FileList& f = Files();
    if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
        ClickListBox(f.list, e.button.x, e.button.y);
    } else if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
        if (Hit(f.ok.rect, e.button.x, e.button.y) && f.list.selected >= 0) {
            Click();
            S().request.path = f.paths[f.list.selected];
            S().request.entry = f.which == Id::CustomCampaigns ? 0 : -1;
            const std::string ext = std::filesystem::path(S().request.path).extension().string();
            if (f.restoring && ext == ".gam") {
                S().request.port = 27015;
                Ask(Request::Kind::HostGame);
                return;
            }
            Ask(f.which == Id::SavedGames
                    ? (ext == ".mgl" ? Request::Kind::PlayRecording : Request::Kind::LoadGame)
                    : Request::Kind::StartScenario);
        } else if (Hit(f.cancel.rect, e.button.x, e.button.y)) {
            Click();
            Open(Id::MainMenu);
        } else if (Hit(kCloseBox, e.button.x, e.button.y)) {
            Ask(Request::Kind::Quit);
        }
    } else if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE) {
        Open(Id::MainMenu);
    }
}

// ---------------------------------------------------------------------------
// The loading screen: the setup screen's parchment while the game is made.

struct Loading {
    Theme theme;
};

Loading& LoadingState() {
    static Loading loading;
    return loading;
}

void DrawLoading(SDL_Renderer* r) {
    const Theme& theme = LoadingState().theme;
    DrawBackground(r, theme);
    const SDL_Color ink = {0, 0, 0, 255};
    // "~ Generating Map ~", and how far along.
    DrawText(r, Text::kBlackletter28B, 400, 250, Get(10661), ink, ink, Align::Centre, false);
    const SDL_Rect bar = {250, 320, 300, 20};
    DrawShaded(r, theme, bar);
    DrawBevel(r, theme, bar, true);
    const int filled = (bar.w - 6) * std::clamp(S().loading, 0, 100) / 100;
    SDL_SetRenderDrawColor(r, theme.focus.r, theme.focus.g, theme.focus.b, 255);
    const SDL_Rect fill = {bar.x + 3, bar.y + 3, filled, bar.h - 6};
    SDL_RenderFillRect(r, &fill);
}

// ---------------------------------------------------------------------------
// The credits: the team's drawings, one after another.

void DrawCredits(SDL_Renderer* r) {
    static Theme theme = MakeTheme(Sin::kCredits, kMenuWidth, kMenuHeight);
    DrawBackground(r, theme);
}

} // namespace

void Init() {
    Menu().theme = MakeTheme(Sin::kMainMenu, kMenuWidth, kMenuHeight);
    Campaigns::Init();
    SetupState().theme = MakeTheme(Sin::kParchment3, kMenuWidth, kMenuHeight);
    OptionsState().theme = MakeTheme(Sin::kParchment, kMenuWidth, kMenuHeight);
    HistoryState().theme = MakeTheme(Sin::kHistory, kMenuWidth, kMenuHeight);
    MultiplayerState().theme = MakeTheme(Sin::kParchment, kMenuWidth, kMenuHeight);
    Files().theme = MakeTheme(Sin::kParchment, kMenuWidth, kMenuHeight);
    LoadingState().theme = MakeTheme(Sin::kParchment, kMenuWidth, kMenuHeight);
    // A custom campaign is any but the game's own five.
    for (const auto& path : FilesIn("../assets/original/Campaign", {".cpn"})) {
        const std::string stem = std::filesystem::path(path).stem().string();
        if (stem != "cam1" && stem != "cam2" && stem != "cam3" && stem != "cam4" && stem != "cam8") {
            Menu().has_campaigns = true;
        }
    }
    Menu().has_scenarios = !FilesIn("../assets/original/Scenario", {".scn", ".scx"}).empty() ||
                           !FilesIn(ScenariosDir(), {".scn", ".scx"}).empty() ||
                           !FilesIn("../original_game_1999/GOODIES", {".scn", ".scx"}).empty();
    Menu().has_saves = !FilesIn(SavedGamesDir(), {".gam", ".mgl"}).empty();
}

void Open(Id screen) {
    S().current = screen;
    switch (screen) {
    case Id::MainMenu: Menu().single_open = Menu().single_open && true; break;
    case Id::Options: OpenOptions(); break;
    case Id::History: OpenHistory(); break;
    case Id::Multiplayer: OpenMultiplayer(); break;
    case Id::Campaigns: Campaigns::PrepareBook(); break;
    case Id::ScenarioSelect: Campaigns::PrepareSelect(); break;
    default: break;
    }
    HAL::TextInput::Want(screen == Id::Options || screen == Id::Multiplayer
                             ? HAL::TextInput::Kind::Fields : HAL::TextInput::Kind::None);
}

Id Current() { return S().current; }

void HandleEvent(const SDL_Event& e) {
    Shared& s = S();
    if (e.type == SDL_MOUSEMOTION) {
        s.mx = e.motion.x;
        s.my = e.motion.y;
    } else if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
        s.held = true;
        s.mx = e.button.x;
        s.my = e.button.y;
    } else if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
        s.held = false;
    }
    if (TechTree::IsOpen()) {
        TechTree::HandleEvent(e, kMenuWidth, kMenuHeight);
        return;
    }
    switch (s.current) {
    case Id::MainMenu: MainMenuEvent(e); break;
    case Id::GameSetup: SetupEvent(e); break;
    case Id::Options: OptionsEvent(e); break;
    case Id::History: HistoryEvent(e); break;
    case Id::Multiplayer: MultiplayerEvent(e); break;
    case Id::Campaigns:
    case Id::CampaignMap:
    case Id::ScenarioSelect:
    case Id::Mission:
    case Id::Aftermath:
        Campaigns::HandleEvent(s.current, e, Campaigns::Pointer{s.mx, s.my, s.held}, s.request);
        break;
    case Id::CustomCampaigns:
    case Id::SavedGames: FileListEvent(e); break;
    case Id::Credits:
        if (e.type == SDL_MOUSEBUTTONUP || e.type == SDL_KEYDOWN) Open(Id::MainMenu);
        break;
    default: break;
    }
}

void Draw(SDL_Renderer* r) {
    if (TechTree::IsOpen()) {
        TechTree::Draw(r, nullptr, kMenuWidth, kMenuHeight);
        return;
    }
    switch (S().current) {
    case Id::MainMenu: DrawMainMenu(r); break;
    case Id::GameSetup: DrawSetup(r); break;
    case Id::Options: DrawOptions(r); break;
    case Id::History: DrawHistory(r); break;
    case Id::Multiplayer: DrawMultiplayer(r); break;
    case Id::Campaigns:
    case Id::CampaignMap:
    case Id::ScenarioSelect:
    case Id::Mission:
    case Id::Aftermath:
        Campaigns::Draw(S().current, r, Campaigns::Pointer{S().mx, S().my, S().held});
        break;
    case Id::CustomCampaigns:
    case Id::SavedGames: DrawFileList(r); break;
    case Id::Loading: DrawLoading(r); break;
    case Id::Credits: DrawCredits(r); break;
    default: break;
    }
}

Request TakeRequest() {
    Request request = S().request;
    S().request = Request{};
    return request;
}

void SetLoadingProgress(int percent) { S().loading = percent; }

void OpenSavedGames() {
    OpenFileList(Id::SavedGames);
}

void OpenCampaignMap(int number, bool from_main_menu) {
    Campaigns::PrepareMap(number, from_main_menu);
    Open(Id::CampaignMap);
}

void OpenMission(const Scenario::File& file) {
    Campaigns::PrepareMission(file);
    Open(Id::Mission);
}

bool OpenAftermath(const Scenario::File& file, bool won) {
    if (!Campaigns::PrepareAftermath(file, won)) return false;
    Open(Id::Aftermath);
    return true;
}

void ReturnFromScenario() {
    Campaigns::Return();
}

} // namespace Screens
