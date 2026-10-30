#include "Setup.h"

#include "TechTree.h"
#include "audio/InterfaceSounds.h"
#include "audio/Music.h"
#include "core/ConfigManager.h"
#include "core/Profile.h"
#include "core/Strings.h"
#include "hal/Audio.h"
#include "hal/GraphicCache.h"
#include "hal/Net.h"
#include "hal/TextInput.h"
#include "net/Lobby.h"
#include "rendering/Text.h"
#include "screens/Sin.h"

#include <SDL2/SDL.h>

#include <algorithm>
#include <cstdio>
#include <ctime>

namespace Screens::Setup {

namespace {

namespace Text = Rendering::Text;
using Core::Strings::Get;

void Click() { HAL::Audio::Play(Audio::InterfaceSound::kButton2, 1.0f, 0.0f); }

// printf with one string or one number, as LANGUAGE.DLL's lines take them.
std::string Format(int string_id, const std::string& text) {
    char out[256];
    std::snprintf(out, sizeof(out), Get(string_id).c_str(), text.c_str());
    return out;
}
std::string Format(int string_id, int number) {
    char out[256];
    std::snprintf(out, sizeof(out), Get(string_id).c_str(), number);
    return out;
}

constexpr int kMaxComputers = 7;

struct PlayerRow {
    DropDown civ;
    DropDown colour;
    DropDown team;
};

struct Setup {
    Theme theme;
    Theme dialog;   // maindlg2, for the lobby's messages
    int kind = 0;   // 0 Random Map, 1 Regicide, 2 Death Match
    PlayerRow you;
    PlayerRow computers[kMaxComputers];
    DropDown difficulty, others, map, size, resources, population, reveal, age, victory;
    CheckBox team_together, lock_teams, all_techs, record;
    Button start, cancel, reset, tech_tree;

    // The lobby of a network game.
    Net::Session* net = nullptr;
    uint32_t shown = ~0u;            // the table's changes last shown
    DropDown kinds[Net::kRows];      // the host's: what each other row is
    PlayerRow rows[Net::kRows];
    DropDown game_type;              // the host's: Random Map, Regicide, Death Match
    CheckBox cheats;
    ListBox chat;
    TextField say;
    CheckBox ready;
    Button ip;
    Notice notice;
    bool notice_ends = false;        // the message ends the lobby
};

Setup& State() {
    static Setup setup;
    return setup;
}

bool Network() { return State().net != nullptr; }
bool Hosting() { return Network() && State().net->role == Net::Role::Host; }

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

// The lobby's own places: the rows one under another from the top, the
// name's drop-down left of the name, the ping between it and the
// civilisation; the settings as lines of text for those who did not host;
// chat, the I'm Ready! box, the IP button, and Cancel alone for them.
constexpr int kLobbyRowY = 86;
constexpr int kLobbyPitch = 24;
constexpr int kKindX = 32;
constexpr int kKindW = 138;
constexpr int kPingX = 193;
constexpr SDL_Rect kSummary = {525, 83, 220, 20};
constexpr int kSummaryLines = 20;
constexpr SDL_Rect kChatLabel = {35, 283, 100, 20};
constexpr SDL_Rect kChatLines = {35, 303, 452, 136};
constexpr SDL_Rect kSay = {35, 445, 452, 23};
constexpr size_t kSayLength = 247;
constexpr SDL_Rect kReady = {37, 480, 37, 37};
constexpr SDL_Rect kReadyLabel = {79, 480, 150, 37};
constexpr SDL_Rect kIp = {343, 490, 37, 37};
constexpr SDL_Rect kStart = {37, 550, 164, 37};
constexpr SDL_Rect kCancel = {216, 550, 164, 37};
constexpr SDL_Rect kCancelAlone = {126, 550, 164, 37};
constexpr SDL_Rect kHelp = {390, 506, 393, 94};
// A row's name drawn in green once its player is ready.
constexpr SDL_Color kReadyInk = {0, 160, 0, 255};

// LANGUAGE.DLL.
constexpr int kTitleNetwork = 9678;
constexpr int kPingHead = 9793;
constexpr int kChatHead = 9687;
constexpr int kImReady = 9663;
constexpr int kIpLabel = 9711;
constexpr int kNoIp = 9101;
constexpr int kOpen = 9685, kClosed = 9684, kComputer = 9683;
constexpr int kGameTypeLabel = 13510, kCheatsLabel = 13526;
constexpr int kCanceled = 9674;
constexpr int kDropped = 9673;
constexpr int kOk = 4001;
constexpr int kYes = 4003, kNo = 4004;

// The random map scripts, in the Map Type list's order (LANGUAGE.DLL 10875
// on); the last choice, 10890, is Random.
constexpr const char* kScripts[] = {
    "Arabia", "Archipelago", "Baltic", "Black Forest", "Coastal", "Continental",
    "Crater Lake", "Fortress", "Gold Rush", "Highland", "Islands", "Mediterranean",
    "Migration", "Rivers", "Team Islands"};
constexpr int kScriptCount = 15;
constexpr int kMapNames = 10875;

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

void MakeRow(PlayerRow& p, int seat) {
    const auto civs = CivNames();
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
}

void PlaceRow(PlayerRow& p, int y) { p.civ.rect.y = p.colour.rect.y = p.team.rect.y = y; }

void ResetSettings(Setup& s) {
    auto setting = [&](DropDown& d, int row_index, std::vector<std::string> items, int selected) {
        d = DropDown{SDL_Rect{kSettingX, kSettingRows[row_index], kSettingW, kSettingH},
                     std::move(items), selected};
        d.font = Text::kGeorgia9B;
    };
    setting(s.difficulty, 0, {Get(11216), Get(11217), Get(11218), Get(11219), Get(11220)}, 3);
    setting(s.others, 1, Numbers(1, kMaxComputers), 0);
    // In a network game the rows say who plays: the second row is the game.
    setting(s.game_type, 1, {Get(9653), Get(9753), Get(9751)}, s.kind);
    std::vector<std::string> maps;
    for (int id = kMapNames; id <= kMapNames + kScriptCount; ++id) maps.push_back(Get(id));
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
    s.cheats = CheckBox{SDL_Rect{613, 429, 24, 26}, false};
}

void ResetSetup(Setup& s) {
    MakeRow(s.you, 1);
    PlaceRow(s.you, kYouY);
    for (int i = 0; i < kMaxComputers; ++i) {
        MakeRow(s.computers[i], i + 2);
        PlaceRow(s.computers[i], kComputerY + i * kComputerPitch);
    }
    ResetSettings(s);
    s.start = Button{SDL_Rect{37, 551, 160, 35}, Get(9710), Text::kBlackletter14};
    s.cancel = Button{SDL_Rect{217, 551, 163, 35}, Get(4002), Text::kBlackletter14};
    s.reset = Button{SDL_Rect{681, 52, 94, 28}, Get(9305), Text::kBlackletter14};
    s.tech_tree = Button{SDL_Rect{300, 491, 36, 36}, std::string{}, Text::kBlackletter14};
}

// The settings the inset shows, without the players.
Core::GameSetup Settings(const Setup& s) {
    Core::GameSetup game = Core::DefaultSetup();
    const int map = s.map.selected;
    // Random is drawn at the start; until then no script is named.
    game.map_script = map < kScriptCount ? kScripts[map] : std::string{};
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
    game.death_match = s.kind == 2;
    game.difficulty = s.difficulty.selected;
    game.lock_teams = s.lock_teams.checked;
    game.all_techs = s.all_techs.checked;
    game.record = s.record.checked;
    // A game against the computer allows cheats: its lobby has no box for
    // them. A network game's has (13526).
    game.cheats = Network() ? s.cheats.checked : true;
    return game;
}

// The random map, drawn when the game starts.
void DrawMap(Core::GameSetup& game, uint32_t seed) {
    if (!game.map_script.empty()) return;
    game.map_script = kScripts[(seed * 2654435761u >> 8) % kScriptCount];
}

// The game the single-player screen describes.
Core::GameSetup BuildGame(const Setup& s) {
    Core::GameSetup game = Settings(s);
    game.seed = static_cast<uint32_t>(std::time(nullptr)) ^ SDL_GetTicks();
    uint32_t roll = game.seed * 2654435761u;
    auto pick = [&](int n) {
        roll = roll * 1664525u + 1013904223u;
        return static_cast<int>((roll >> 8) % static_cast<uint32_t>(n));
    };
    if (game.map_script.empty()) game.map_script = kScripts[pick(kScriptCount)];
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

// ---------------------------------------------------------------------------
// The lobby.

const Net::Table& Table() { return State().net->table; }

// The rows' controls as the table has them, unless one is open.
void ShowTable(Setup& s) {
    const Net::Table& table = Table();
    if (s.shown == table.changes) return;
    for (int r = 0; r < Net::kRows; ++r) {
        if (s.rows[r].civ.open || s.rows[r].colour.open || s.rows[r].team.open || s.kinds[r].open) return;
    }
    s.shown = table.changes;
    const int mine = s.net->row;
    for (int r = 0; r < Net::kRows; ++r) {
        const Net::Row& row = table.rows[r];
        PlayerRow& p = s.rows[r];
        p.civ.selected = std::min<int>(row.civ, static_cast<int>(p.civ.items.size()) - 1);
        p.colour.selected = row.number;
        p.team.selected = row.team;
        const bool ours = r == mine || (Hosting() && row.kind == Net::RowKind::Computer);
        const bool free = ours && !table.restoring;
        p.civ.enabled = p.colour.enabled = p.team.enabled = free;
        s.kinds[r].selected = row.kind == Net::RowKind::Closed ? 1 : row.kind == Net::RowKind::Computer ? 2 : 0;
    }
    s.ready.checked = table.rows[mine].ready;
    // Chat: the newest line last, in view.
    s.chat.items.clear();
    for (const Net::Said& said : table.chat) s.chat.items.push_back(said.name + ": " + said.text);
    s.chat.scroll = std::max(0, static_cast<int>(s.chat.items.size()) - ListBoxRows(s.chat));
    // What the host has set, on its own inset; the others read it as lines.
    if (Hosting()) return;
    const Core::GameSetup& game = s.net->setup;
    s.kind = game.death_match ? 2 : game.regicide ? 1 : 0;
}

void OpenNoticeOf(Setup& s, std::string text, bool ends) {
    OpenNotice(s.notice, s.dialog, std::move(text), Get(kOk));
    s.notice_ends = ends;
}

// The host's settings, to everyone, after any change to them.
void Publish(Setup& s) {
    if (Hosting()) Net::SetSettings(*s.net, Settings(s));
}

// This machine's row, or a computer row of the host's, after a change.
void PublishRow(Setup& s, int r) {
    const PlayerRow& p = s.rows[r];
    Net::SetRow(*s.net, r, static_cast<uint8_t>(p.civ.selected), static_cast<uint8_t>(p.colour.selected),
                static_cast<uint8_t>(p.team.selected));
}

std::vector<std::string> SummaryLines(const Setup& s) {
    const Core::GameSetup& game = s.net->setup;
    auto yes = [](bool b) { return Get(b ? kYes : kNo); };
    static const int kTypes[3] = {9653, 9753, 9751};
    std::vector<std::string> lines;
    lines.push_back(Get(kTypes[game.death_match ? 2 : game.regicide ? 1 : 0]));
    int size = 0;
    for (int i = 0; i < 6; ++i) {
        if (Core::kMapSizes[i].tiles == game.map_size) size = i;
    }
    lines.push_back(Format(9690, Get(10611 + size)));
    int map = kScriptCount;
    for (int i = 0; i < kScriptCount; ++i) {
        if (game.map_script == kScripts[i]) map = i;
    }
    lines.push_back(Format(9654, Get(kMapNames + map)));
    lines.push_back(Format(9660, Get(game.victory == Core::VictoryKind::Conquest ? 4321 : 4327)));
    static const int kAges[4] = {4332, 4202, 4203, 4204};
    lines.push_back(Format(9700, Get(kAges[std::clamp(game.starting_age, 0, 3)])));
    if (!game.death_match) {
        const int level = game.resources == Core::StartingResources::High ? 9704
                        : game.resources == Core::StartingResources::Medium ? 9703 : 9702;
        lines.push_back(Format(9701, Get(level)));
    }
    lines.push_back(Format(9696, Get(11216 + std::clamp(game.difficulty, 0, 4))));
    const int reveal = game.reveal == Core::Reveal::AllVisible ? 9757
                     : game.reveal == Core::Reveal::Explored ? 9756 : 9755;
    lines.push_back(Format(9656, Get(reveal)));
    lines.push_back(Format(9708, yes(game.all_techs)));
    lines.push_back(Format(9659, yes(game.cheats)));
    lines.push_back(Format(9712, game.population_limit));
    lines.push_back(Format(9797, yes(game.lock_teams)));
    return lines;
}

std::vector<DropDown*> LobbyDropDowns(Setup& s) {
    std::vector<DropDown*> all;
    if (Hosting() && !Table().restoring) {
        for (DropDown* d : {&s.difficulty, &s.game_type, &s.map, &s.size}) all.push_back(d);
        if (s.kind != 2) all.push_back(&s.resources);
        for (DropDown* d : {&s.population, &s.reveal, &s.age, &s.victory}) all.push_back(d);
    }
    for (int r = 0; r < Net::kRows; ++r) {
        const Net::Row& row = Table().rows[r];
        if (Hosting() && r != 0 && row.kind != Net::RowKind::Person && !Table().restoring) all.push_back(&s.kinds[r]);
        if (row.kind != Net::RowKind::Person && row.kind != Net::RowKind::Computer) continue;
        // A civilisation this machine cannot change is only its name.
        if (s.rows[r].civ.enabled) all.push_back(&s.rows[r].civ);
        for (DropDown* d : {&s.rows[r].colour, &s.rows[r].team}) all.push_back(d);
    }
    return all;
}

// The help for what the mouse is over, in the lobby.
int LobbyHelp(const Setup& s, const Pointer& p) {
    for (int r = 0; r < Net::kRows; ++r) {
        const int y = kLobbyRowY + r * kLobbyPitch;
        if (p.y < y || p.y >= y + kRowH) continue;
        if (p.x >= kKindX && p.x < kKindX + kKindW) return 30201;
        if (Hit(s.rows[r].civ.rect, p.x, p.y)) return 30202;
        if (Hit(s.rows[r].colour.rect, p.x, p.y)) return 30212;
        if (Hit(s.rows[r].team.rect, p.x, p.y)) return 30211;
    }
    if (Hit(kChatLines, p.x, p.y)) return 30205;
    if (Hit(kSay, p.x, p.y)) return 30206;
    if (Hit(kReady, p.x, p.y)) return 30210;
    if (Hit(kIp, p.x, p.y)) return 30209;
    if (Hit(s.tech_tree.rect, p.x, p.y)) return 30214;
    if (Hosting() && Hit(kStart, p.x, p.y)) return 30213;
    return -1;
}

void DrawLobby(SDL_Renderer* r, const Pointer& p) {
    Setup& s = State();
    ShowTable(s);
    const Theme& theme = s.theme;
    DrawBackground(r, theme);
    const SDL_Color ink = {0, 0, 0, 255};
    const Net::Table& table = Table();

    DrawText(r, Text::kBlackletter21B, 400, 6, Get(kTitleNetwork), ink, ink, Align::Centre, false);
    DrawText(r, Text::kBlackletter14, 51, 54, Get(9680), ink, ink, Align::Left, false);
    DrawText(r, Text::kBlackletter14, 181, 54, Get(kPingHead), ink, ink, Align::Left, false);
    DrawText(r, Text::kBlackletter14, kCivX, 54, Get(9681), ink, ink, Align::Left, false);
    DrawText(r, Text::kBlackletter14, 426, 54, Get(9646), ink, ink, Align::Right, false);
    DrawText(r, Text::kBlackletter14, 497, 54, Get(9647), ink, ink, Align::Right, false);

    for (int i = 0; i < Net::kRows; ++i) {
        const Net::Row& row = table.rows[i];
        const int y = kLobbyRowY + i * kLobbyPitch;
        PlaceRow(s.rows[i], y);
        s.kinds[i].rect.y = y;
        const bool host_picks = Hosting() && i != 0 && row.kind != Net::RowKind::Person && !table.restoring;
        if (row.kind == Net::RowKind::Person) {
            DrawText(r, Text::kGeorgia9B, kNameX, y + 2, row.name, row.ready ? kReadyInk : ink, ink,
                     Align::Left, false);
            if (i != 0) {
                DrawText(r, Text::kArial8, kPingX, y + 4, std::to_string(row.ping), ink, ink, Align::Left, false);
            }
        } else if (!host_picks) {
            const int label = row.kind == Net::RowKind::Computer ? kComputer
                            : row.kind == Net::RowKind::Closed ? kClosed : kOpen;
            DrawText(r, Text::kGeorgia9B, kNameX, y + 2, Get(label), ink, ink, Align::Left, false);
        }
        if ((row.kind == Net::RowKind::Person || row.kind == Net::RowKind::Computer) && !s.rows[i].civ.enabled) {
            const DropDown& civ = s.rows[i].civ;
            const std::string& name = civ.items[std::clamp(civ.selected, 0, static_cast<int>(civ.items.size()) - 1)];
            DrawText(r, civ.font, civ.rect.x + civ.rect.h - 2 + 7, y + (kRowH - Text::Height(civ.font)) / 2, name,
                     ink, ink, Align::Left, false);
        }
        if (row.kind == Net::RowKind::Person || row.kind == Net::RowKind::Computer) {
            const int index = Core::ConfigManager::Get().ColourMinimapIndex(s.rows[i].colour.selected);
            const HAL::PaletteColor c = HAL::GraphicCache::Get().GetPaletteColor(static_cast<size_t>(index));
            s.rows[i].colour.colour = SDL_Color{c.r, c.g, c.b, 255};
        }
    }

    // The settings: the host's inset, or the lines everyone else reads.
    if (Hosting() && !table.restoring) {
        DrawText(r, Text::kBlackletter14, 532, 50, Get(9682), ink, ink, Align::Left, false);
        struct Row { int label; const DropDown* d; };
        const Row rows[] = {
            {13513, &s.difficulty}, {kGameTypeLabel, &s.game_type}, {13511, &s.map}, {13512, &s.size},
            {13514, &s.resources}, {13516, &s.population}, {13522, &s.reveal}, {13515, &s.age},
            {13518, &s.victory},
        };
        for (const Row& row : rows) {
            if (row.d == &s.resources && s.kind == 2) continue;
            DrawText(r, Text::kGeorgia9B, kSettingLabelRight, row.d->rect.y + 3, Get(row.label),
                     ink, ink, Align::Right, false);
        }
        struct Check { int label; const CheckBox* c; };
        const Check checks[] = {
            {13521, &s.team_together}, {13523, &s.lock_teams}, {13524, &s.all_techs},
            {13542, &s.record}, {kCheatsLabel, &s.cheats},
        };
        for (const Check& c : checks) {
            DrawText(r, Text::kGeorgia9B, c.c->box.x - 7, c.c->box.y + 5, Get(c.label), ink, ink,
                     Align::Right, false);
            DrawCheckBox(r, theme, *c.c, p.x, p.y, p.held);
        }
        DrawButton(r, theme, s.reset, StateOf(s.reset, p.x, p.y, p.held));
    } else {
        DrawText(r, Text::kBlackletter14, 532, 50, Get(9682), ink, ink, Align::Left, false);
        const auto lines = SummaryLines(s);
        for (size_t i = 0; i < lines.size() && i < static_cast<size_t>(kSummaryLines); ++i) {
            DrawText(r, Text::GameFont(11), kSummary.x, kSummary.y + static_cast<int>(i) * kSummary.h, lines[i],
                     ink, ink, Align::Left, false);
        }
    }

    // Chat, the ready check, the technology tree and IP, Start and Cancel.
    DrawText(r, Text::GameFont(4), kChatLabel.x, kChatLabel.y, Get(kChatHead), ink, ink, Align::Left, false);
    DrawListBox(r, theme, s.chat, p.x, p.y);
    DrawTextField(r, theme, s.say);
    DrawCheckBox(r, theme, s.ready, p.x, p.y, p.held);
    DrawText(r, Text::kBlackletter14, kReadyLabel.x,
             kReadyLabel.y + (kReadyLabel.h - Text::Height(Text::kBlackletter14)) / 2, Get(kImReady), ink, ink,
             Align::Left, false);
    DrawButtonArt(r, theme, 16, s.tech_tree.rect.x, s.tech_tree.rect.y);
    DrawButton(r, theme, s.ip, StateOf(s.ip, p.x, p.y, p.held));
    if (Hosting()) DrawButton(r, theme, s.start, StateOf(s.start, p.x, p.y, p.held));
    DrawButton(r, theme, s.cancel, StateOf(s.cancel, p.x, p.y, p.held));

    if (const int help = LobbyHelp(s, p); help >= 0 && !s.notice.open) {
        DrawWrapped(r, Text::GameFont(11), kHelp.x, kHelp.y, kHelp.w, Get(help), theme.text, theme.text_shadow);
    }

    DropDown* open = nullptr;
    for (DropDown* d : LobbyDropDowns(s)) {
        if (d->open) { open = d; continue; }
        DrawDropDown(r, theme, *d, p.x, p.y, p.held, kMenuHeight);
    }
    if (open != nullptr) DrawDropDown(r, theme, *open, p.x, p.y, p.held, kMenuHeight);
    DrawNotice(r, s.dialog, s.notice, p);
    Screens::DrawCloseBox(r, theme, p.held && Hit(kCloseBox, p.x, p.y));
}

void LeaveLobby(Setup& s) {
    if (s.net != nullptr) Net::LeaveLobby(*s.net);
    s.net = nullptr;
    HAL::TextInput::Want(HAL::TextInput::Kind::None);
    Screens::Open(Id::MainMenu);
}

void LobbyEvent(const SDL_Event& e, Request& request) {
    Setup& s = State();
    if (s.notice.open) {
        if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT && Hit(s.notice.ok.rect, e.button.x, e.button.y)) {
            Click();
            s.notice.open = false;
            if (s.notice_ends) LeaveLobby(s);
        } else if (e.type == SDL_KEYDOWN && (e.key.keysym.sym == SDLK_RETURN || e.key.keysym.sym == SDLK_ESCAPE)) {
            s.notice.open = false;
            if (s.notice_ends) LeaveLobby(s);
        }
        return;
    }
    // Typing a line to the others; Enter says it.
    if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_RETURN && s.say.focused) {
        Net::Say(*s.net, s.say.text);
        s.say.text.clear();
        return;
    }
    if (TypeIntoField(s.say, e)) return;
    if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
        const auto drops = LobbyDropDowns(s);
        for (DropDown* d : drops) {
            if (!d->open) continue;
            const int before = d->selected;
            ClickDropDown(*d, e.button.x, e.button.y, kMenuHeight);
            Click();
            if (d->selected == before) return;
            for (int r = 0; r < Net::kRows; ++r) {
                if (d == &s.kinds[r]) {
                    static const Net::RowKind kKinds[3] = {Net::RowKind::Open, Net::RowKind::Closed,
                                                          Net::RowKind::Computer};
                    Net::SetRowKind(*s.net, r, kKinds[d->selected]);
                    s.shown = ~0u;
                    return;
                }
                if (d == &s.rows[r].civ || d == &s.rows[r].colour || d == &s.rows[r].team) {
                    if (d == &s.rows[r].civ && r == s.net->row) Audio::Music::Civ(d->selected);
                    PublishRow(s, r);
                    return;
                }
            }
            if (d == &s.game_type) s.kind = d->selected;
            Publish(s);
            return;
        }
        for (DropDown* d : drops) {
            if (d->enabled && ClickDropDown(*d, e.button.x, e.button.y, kMenuHeight)) {
                Click();
                return;
            }
        }
        if (Hosting() && !Table().restoring) {
            for (CheckBox* c : {&s.team_together, &s.lock_teams, &s.all_techs, &s.record, &s.cheats}) {
                if (Hit(c->box, e.button.x, e.button.y)) {
                    c->checked = !c->checked;
                    Click();
                    Publish(s);
                    return;
                }
            }
        }
        if (Hit(s.ready.box, e.button.x, e.button.y)) {
            Click();
            Net::SetReady(*s.net, !s.ready.checked);
            s.ready.checked = !s.ready.checked;
            return;
        }
        ClickListBox(s.chat, e.button.x, e.button.y);
        s.say.focused = Hit(s.say.rect, e.button.x, e.button.y);
    } else if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
        if (Hosting() && Hit(s.start.rect, e.button.x, e.button.y)) {
            Click();
            if (const int why = Net::StartRefusal(*s.net); why != 0) {
                OpenNoticeOf(s, Get(why), false);
                return;
            }
            const uint32_t seed = static_cast<uint32_t>(std::time(nullptr)) ^ SDL_GetTicks();
            Core::GameSetup settings = s.net->setup;
            DrawMap(settings, seed);
            Net::SetSettings(*s.net, settings);
            Net::StartGame(*s.net, seed);
        } else if (Hit(s.cancel.rect, e.button.x, e.button.y)) {
            Click();
            LeaveLobby(s);
        } else if (Hosting() && !Table().restoring && Hit(s.reset.rect, e.button.x, e.button.y)) {
            Click();
            s.kind = 0;
            ResetSettings(s);
            Publish(s);
        } else if (Hit(s.ip.rect, e.button.x, e.button.y)) {
            Click();
            const std::string address = HAL::Net::LocalAddress();
            OpenNoticeOf(s, address.empty() ? Get(kNoIp) : address, false);
        } else if (Hit(s.tech_tree.rect, e.button.x, e.button.y)) {
            Click();
            const int civ = s.rows[s.net->row].civ.selected;
            TechTree::OpenPregame(civ > 0 ? civ : 1);
        } else if (Hit(kCloseBox, e.button.x, e.button.y)) {
            Click();
            if (s.net != nullptr) Net::LeaveLobby(*s.net);
            s.net = nullptr;
            request.kind = Request::Kind::Quit;
        }
    } else if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE) {
        LeaveLobby(s);
    }
}

// ---------------------------------------------------------------------------
// The single-player screen.

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

void DrawSingle(SDL_Renderer* r, const Pointer& p) {
    Setup& s = State();
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
        DrawCheckBox(r, theme, *c, p.x, p.y, p.held);
    }
    for (const Button* b : {&s.start, &s.cancel, &s.reset}) {
        DrawButton(r, theme, *b, StateOf(*b, p.x, p.y, p.held));
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
        DrawDropDown(r, theme, *d, p.x, p.y, p.held, kMenuHeight);
    }
    if (open != nullptr) DrawDropDown(r, theme, *open, p.x, p.y, p.held, kMenuHeight);
    Screens::DrawCloseBox(r, theme, p.held && Hit(kCloseBox, p.x, p.y));
}

void SingleEvent(const SDL_Event& e, Request& request) {
    Setup& s = State();
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
            request.setup = BuildGame(s);
            request.kind = Request::Kind::StartGame;
        } else if (Hit(s.cancel.rect, e.button.x, e.button.y)) {
            Click();
            Screens::Open(Id::MainMenu);
        } else if (Hit(s.reset.rect, e.button.x, e.button.y)) {
            Click();
            ResetSetup(s);
        } else if (Hit(s.tech_tree.rect, e.button.x, e.button.y)) {
            Click();
            TechTree::OpenPregame(s.you.civ.selected > 0 ? s.you.civ.selected : 1);
        } else if (Hit(kCloseBox, e.button.x, e.button.y)) {
            Click();
            request.kind = Request::Kind::Quit;
        }
    } else if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE) {
        Screens::Open(Id::MainMenu);
    }
}

} // namespace

void Init() {
    State().theme = MakeTheme(Sin::kParchment3, kMenuWidth, kMenuHeight);
    State().dialog = MakeTheme(Sin::kParchmentDialog, kMenuWidth, kMenuHeight, true);
}

void OpenSingle(int kind) {
    Setup& s = State();
    s.net = nullptr;
    s.kind = kind;
    ResetSetup(s);
    Screens::Open(Id::GameSetup);
}

void OpenLobby(Net::Session& session) {
    Setup& s = State();
    s.net = &session;
    s.kind = session.setup.death_match ? 2 : session.setup.regicide ? 1 : 0;
    s.shown = ~0u;
    s.notice.open = false;
    ResetSetup(s);
    for (int r = 0; r < Net::kRows; ++r) {
        MakeRow(s.rows[r], r + 1);
        s.kinds[r] = DropDown{SDL_Rect{kKindX, 0, kKindW, kRowH}, {Get(kOpen), Get(kClosed), Get(kComputer)}, 0};
        s.kinds[r].font = Text::kGeorgia9B;
        s.kinds[r].plain = true;
    }
    s.chat = ListBox{kChatLines, {}, -1, 0, Text::GameFont(10)};
    s.say = TextField{kSay, std::string{}, kSayLength, false, Text::GameFont(10)};
    s.ready = CheckBox{kReady, false};
    s.ip = Button{kIp, Get(kIpLabel), Text::kBlackletter14};
    s.tech_tree = Button{SDL_Rect{300, 490, 37, 37}, std::string{}, Text::kBlackletter14};
    s.start = Button{kStart, Get(9710), Text::kBlackletter14};
    s.cancel = Button{Hosting() ? kCancel : kCancelAlone, Get(4002), Text::kBlackletter14};
    // The host's inset opens on the settings it hosted with.
    if (Hosting()) {
        const Core::GameSetup& game = session.setup;
        s.game_type.selected = s.kind;
        for (int i = 0; i < kScriptCount; ++i) {
            if (game.map_script == kScripts[i]) s.map.selected = i;
        }
        Publish(s);
    }
    Screens::Open(Id::MultiplayerSetup);
    HAL::TextInput::Want(HAL::TextInput::Kind::Fields);
}

void Update(Request& request) {
    Setup& s = State();
    if (s.net == nullptr || Screens::Current() != Id::MultiplayerSetup) return;
    Net::Session& session = *s.net;
    Net::Pump(session);
    if (session.started) {
        s.net = nullptr;
        HAL::TextInput::Want(HAL::TextInput::Kind::None);
        request.kind = Request::Kind::NetworkGame;
        return;
    }
    if (s.notice.open && s.notice_ends) return;
    if (session.canceled) OpenNoticeOf(s, Get(kCanceled), true);
    else if (session.ejected) OpenNoticeOf(s, Get(session.refused != 0 ? session.refused : kDropped), true);
    else if (!session.failure.empty()) OpenNoticeOf(s, Get(kDropped), true);
}

void Draw(SDL_Renderer* r, const Pointer& p) {
    if (Network()) DrawLobby(r, p);
    else DrawSingle(r, p);
}

void HandleEvent(const SDL_Event& e, const Pointer& p, Request& request) {
    (void)p;
    if (Network()) LobbyEvent(e, request);
    else SingleEvent(e, request);
}

} // namespace Screens::Setup
