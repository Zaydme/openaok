#include "Multiplayer.h"

#include "Setup.h"
#include "audio/InterfaceSounds.h"
#include "core/GameSetup.h"
#include "core/Profile.h"
#include "core/Strings.h"
#include "hal/Audio.h"
#include "hal/TextInput.h"
#include "net/Lobby.h"
#include "rendering/Text.h"
#include "save/Save.h"
#include "screens/Sin.h"

#include <SDL2/SDL.h>

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <iterator>

namespace Screens::Multiplayer {

namespace {

namespace Text = Rendering::Text;
using Core::Strings::Get;

void Click() { HAL::Audio::Play(Audio::InterfaceSound::kButton2, 1.0f, 0.0f); }

std::string Format(int string_id, const std::string& text) {
    char out[256];
    std::snprintf(out, sizeof(out), Get(string_id).c_str(), text.c_str());
    return out;
}

// The menu's places on the plank, at 800x600.
constexpr SDL_Rect kTitle = {483, 8, 247, 24};
constexpr SDL_Rect kConnectionLabel = {344, 70, 300, 50};
constexpr SDL_Rect kConnections = {433, 95, 360, 70};
constexpr SDL_Rect kShowGames = {501, 179, 200, 40};
constexpr SDL_Rect kGames = {456, 225, 300, 150};
constexpr SDL_Rect kJoin = {526, 379, 150, 40};
constexpr SDL_Rect kCreate = {453, 436, 150, 40};
constexpr SDL_Rect kRestore = {609, 436, 150, 40};
constexpr SDL_Rect kCreateAlone = {526, 436, 150, 40};
// The connection type's heading, white on the town.
constexpr SDL_Color kWhite = {255, 255, 255, 255};

// The Show Games dialog, 405x279 in the middle of the screen, and its parts
// from its top-left.
constexpr int kDialogW = 405;
constexpr int kDialogH = 279;
constexpr SDL_Rect kDialogTitle = {5, 5, 390, 30};
constexpr SDL_Rect kRecentLabel = {5, 40, 200, 25};
constexpr SDL_Rect kRecent = {10, 60, 385, 115};
constexpr SDL_Rect kAddressLabel = {5, 182, 390, 25};
constexpr SDL_Rect kAddress = {10, 200, 385, 25};
constexpr size_t kAddressLength = 254;
constexpr SDL_Rect kDialogOk = {53, 235, 144, 30};
constexpr SDL_Rect kDialogCancel = {207, 235, 144, 30};

// How often the games are asked after; how long a join may take.
constexpr uint32_t kLookMs = 1000;
constexpr uint32_t kJoinMs = 15000;

// LANGUAGE.DLL.
constexpr int kMultiplayer = 9501;
constexpr int kConnectionType = 9212;
constexpr int kLocalNetwork = 9620;
constexpr int kShowGamesLabel = 9233, kJoinLabel = 9234, kCreateLabel = 9235, kRestoreLabel = 9236;
constexpr int kGameOf = 9237;           // "%s's Game"
constexpr int kRestoring = 9218;
constexpr int kLooking = 9219;
constexpr int kDialogTitleText = 9841, kRecentText = 9842, kAddressText = 9843;
constexpr int kCreating = 1218, kJoining = 1220, kJoiningOnline = 1221;
constexpr int kNoLink = 9617;
constexpr int kCannotCreate = 9645;
constexpr int kNoSave = 9719;
constexpr int kOk = 4001, kCancelText = 4002;

// The connection list's entries.
enum Connection { kTcp, kLan };

struct Menu {
    bool open = false;
    Theme town, dialog_theme, wait_theme;
    std::string tcp_name;            // the TCP/IP connection's, from the CD's DirectX
    std::vector<Connection> kinds;   // the connection list's, in its order
    ListBox connections, games;
    Button show, join, create, restore;
    bool has_saves = false;
    Net::Finder finder;
    std::string target;              // over TCP/IP, the host asked
    uint32_t looked = 0;
    std::vector<Net::Found> listed;  // the games list's entries
    // The Show Games dialog.
    bool asking = false;
    SDL_Point corner{0, 0};
    ListBox recent;
    TextField address;
    Button ok, cancel;
    Notice notice;
    // Joining: the message waited on, since when, and the game.
    int waiting = 0;
    uint32_t wait_from = 0;
    Net::Found joining;
    Net::Session session;
};

Menu& M() {
    static Menu menu;
    return menu;
}

Connection Chosen() {
    const Menu& m = M();
    const int i = m.connections.selected;
    return i >= 0 && i < static_cast<int>(m.kinds.size()) ? m.kinds[i] : kLan;
}

std::string NameOf(const Net::Found& game) {
    const std::string name = Format(kGameOf, game.name);
    return game.restoring ? Get(kRestoring) + " " + name : name;
}

// The games list from the games found, keeping the one chosen.
void ListGames(Menu& m) {
    Net::Found chosen;
    const bool had = m.games.selected >= 0 && m.games.selected < static_cast<int>(m.listed.size());
    if (had) chosen = m.listed[m.games.selected];
    m.listed = m.finder.games;
    m.games.items.clear();
    m.games.selected = -1;
    for (size_t i = 0; i < m.listed.size(); ++i) {
        m.games.items.push_back(NameOf(m.listed[i]));
        if (had && m.listed[i].address == chosen.address && m.listed[i].port == chosen.port) {
            m.games.selected = static_cast<int>(i);
        }
    }
}

void Look(Menu& m) {
    if (Chosen() == kLan) Net::Look(m.finder, std::string{});
    else if (!m.target.empty()) Net::Look(m.finder, m.target);
    m.looked = SDL_GetTicks();
}

void OpenDialog(Menu& m) {
    m.asking = true;
    m.corner = SDL_Point{(kMenuWidth - kDialogW) / 2, (kMenuHeight - kDialogH) / 2};
    auto at = [&](SDL_Rect r) { return SDL_Rect{m.corner.x + r.x, m.corner.y + r.y, r.w, r.h}; };
    m.recent = ListBox{at(kRecent), {}, -1, 0, Text::GameFont(10)};
    for (const auto& game : Core::Profile::Current().recent_games) {
        char line[512];
        std::snprintf(line, sizeof(line), "%s - %s", game.name.c_str(), game.address.c_str());
        m.recent.items.push_back(line);
    }
    m.address = TextField{at(kAddress), m.target, kAddressLength, true, Text::GameFont(11)};
    m.ok = Button{at(kDialogOk), Get(kOk), Text::kBlackletter14};
    m.cancel = Button{at(kDialogCancel), Get(kCancelText), Text::kBlackletter14};
    HAL::TextInput::Want(HAL::TextInput::Kind::Fields);
}

void CloseDialog(Menu& m) {
    m.asking = false;
    HAL::TextInput::Want(HAL::TextInput::Kind::None);
}

// A game joined by address is remembered, newest first.
void Remember(const Net::Found& game) {
    auto& recent = Core::Profile::Current().recent_games;
    std::erase_if(recent, [&](const Core::Profile::RecentGame& r) { return r.address == game.address; });
    recent.insert(recent.begin(), {game.name, game.address});
    if (recent.size() > Core::Profile::kRecentGames) recent.resize(Core::Profile::kRecentGames);
    Core::Profile::Save(Core::Profile::Current());
}

void Join(Menu& m) {
    if (m.games.selected < 0 || m.games.selected >= static_cast<int>(m.listed.size())) return;
    m.joining = m.listed[m.games.selected];
    if (Chosen() == kTcp) Remember(m.joining);
    if (!Net::JoinLobby(m.session, m.joining.address, m.joining.port, Core::Profile::Current().name)) {
        OpenNotice(m.notice, m.dialog_theme, Get(kNoLink), Get(kOk));
        return;
    }
    m.waiting = Chosen() == kTcp ? kJoiningOnline : kJoining;
    m.wait_from = SDL_GetTicks();
    Screens::Open(Id::Multiplayer);
}

void Create(Menu& m) {
    if (!Net::OpenLobby(m.session, Net::kPort, Core::DefaultSetup(), Core::Profile::Current().name, false)) {
        OpenNotice(m.notice, m.dialog_theme, Get(kCannotCreate), Get(kOk));
        return;
    }
    CloseMenu();
    Setup::OpenLobby(m.session);
}

void Layout(Menu& m) {
    m.show = Button{kShowGames, Get(kShowGamesLabel), Text::kBlackletter14};
    m.join = Button{kJoin, Get(kJoinLabel), Text::kBlackletter14};
    // With no saved game to restore, Restore is not shown and Create stands
    // alone in the middle.
    m.create = Button{m.has_saves ? kCreate : kCreateAlone, Get(kCreateLabel), Text::kBlackletter14};
    m.restore = Button{kRestore, Get(kRestoreLabel), Text::kBlackletter14};
    m.restore.visible = m.has_saves;
}

} // namespace

void Init() {
    Menu& m = M();
    m.town = MakeTheme(Sin::kMainMenu, kMenuWidth, kMenuHeight);
    m.dialog_theme = MakeTheme(Sin::kMainDialog, kMenuWidth, kMenuHeight, true);
    m.wait_theme = MakeTheme(Sin::kParchment3, kMenuWidth, kMenuHeight);
    std::ifstream in("../assets/extracted_data/connections.json");
    const std::string json((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    const size_t key = json.find("\"tcp\"");
    const size_t open = key == std::string::npos ? key : json.find('"', json.find(':', key) + 1);
    const size_t close = open == std::string::npos ? open : json.find('"', open + 1);
    if (close != std::string::npos) m.tcp_name = json.substr(open + 1, close - open - 1);
}

void OpenMenu() {
    Menu& m = M();
    m.open = true;
    m.kinds.clear();
    m.connections = ListBox{kConnections, {}, 0, 0, Text::GameFont(11)};
    if (!m.tcp_name.empty()) {
        m.kinds.push_back(kTcp);
        m.connections.items.push_back(m.tcp_name);
    }
    m.kinds.push_back(kLan);
    m.connections.items.push_back(Get(kLocalNetwork));
    const int last = Core::Profile::Current().connection;
    for (size_t i = 0; i < m.kinds.size(); ++i) {
        if (static_cast<int>(m.kinds[i]) == last) m.connections.selected = static_cast<int>(i);
    }
    m.games = ListBox{kGames, {}, -1, 0, Text::GameFont(11)};
    m.has_saves = !FilesIn(SavedGamesDir(), {".gam"}).empty();
    Layout(m);
    Net::OpenFinder(m.finder);
    m.listed.clear();
    Look(m);
}

void CloseMenu() {
    Menu& m = M();
    m.open = false;
    m.asking = false;
    Net::CloseFinder(m.finder);
}

bool MenuOpen() { return M().open; }

void DrawMenu(SDL_Renderer* r, const Theme& town, const Pointer& p) {
    Menu& m = M();
    if (!m.open) return;
    DrawTextIn(r, Text::kBlackletter14, kTitle, Get(kMultiplayer), town.text, town.text_shadow);
    {
        const int font = Text::GameFont(11);
        const int w = Text::Width(font, Get(kConnectionType));
        Text::Draw(r, font, kConnectionLabel.x + (kConnectionLabel.w - w) / 2, kConnectionLabel.y,
                   Get(kConnectionType), kWhite);
    }
    DrawListBox(r, town, m.connections, p.x, p.y);
    ListBox games = m.games;
    // Nothing found yet: the list says it is looking.
    if (games.items.empty() && (Chosen() == kLan || !m.target.empty())) games.items.push_back(Get(kLooking));
    DrawListBox(r, town, games, p.x, p.y);
    Button join = m.join;
    join.enabled = m.games.selected >= 0;
    for (const Button* b : {static_cast<const Button*>(&m.show), static_cast<const Button*>(&join),
                            static_cast<const Button*>(&m.create), static_cast<const Button*>(&m.restore)}) {
        if (b->visible) DrawButton(r, town, *b, StateOf(*b, p.x, p.y, p.held));
    }
    if (m.asking) {
        // The dialog's art repeated from its corner, should the art be
        // smaller than the dialog.
        const SDL_Rect box = {m.corner.x, m.corner.y, kDialogW, kDialogH};
        Theme tiled = m.dialog_theme;
        tiled.tiled = true;
        tiled.origin = m.corner;
        DrawBackgroundIn(r, tiled, box);
        DrawBevel(r, m.dialog_theme, box, false);
        auto at = [&](SDL_Rect rect) { return SDL_Rect{m.corner.x + rect.x, m.corner.y + rect.y, rect.w, rect.h}; };
        const Theme& d = m.dialog_theme;
        DrawTextIn(r, Text::GameFont(1), at(kDialogTitle), Get(kDialogTitleText), d.text, d.text_shadow);
        const SDL_Rect recent = at(kRecentLabel);
        DrawText(r, Text::GameFont(11), recent.x, recent.y, Get(kRecentText), d.text, d.text_shadow, Align::Left);
        DrawListBox(r, d, m.recent, p.x, p.y);
        const SDL_Rect label = at(kAddressLabel);
        DrawText(r, Text::GameFont(11), label.x, label.y, Get(kAddressText), d.text, d.text_shadow, Align::Left);
        DrawTextField(r, d, m.address);
        DrawButton(r, d, m.ok, StateOf(m.ok, p.x, p.y, p.held));
        DrawButton(r, d, m.cancel, StateOf(m.cancel, p.x, p.y, p.held));
    }
    DrawNotice(r, m.dialog_theme, m.notice, p);
}

int HelpAt(const Pointer& p) {
    const Menu& m = M();
    if (!m.open || m.asking || m.notice.open) return -1;
    if (Hit(kConnections, p.x, p.y)) return 31050;
    if (Hit(m.show.rect, p.x, p.y)) return 31051;
    if (Hit(kGames, p.x, p.y)) return 31052;
    if (Hit(m.join.rect, p.x, p.y)) return 31053;
    if (Hit(m.create.rect, p.x, p.y)) return 31054;
    if (m.restore.visible && Hit(m.restore.rect, p.x, p.y)) return 31055;
    return -1;
}

bool MenuEvent(const SDL_Event& e, const Pointer& p, Request& request) {
    (void)p;
    (void)request;
    Menu& m = M();
    if (!m.open) return false;
    const bool down = e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT;
    const bool up = e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT;
    if (m.notice.open) {
        if ((up && Hit(m.notice.ok.rect, e.button.x, e.button.y)) ||
            (e.type == SDL_KEYDOWN && (e.key.keysym.sym == SDLK_RETURN || e.key.keysym.sym == SDLK_ESCAPE))) {
            Click();
            m.notice.open = false;
        }
        return true;
    }
    if (m.asking) {
        const bool accept = (up && Hit(m.ok.rect, e.button.x, e.button.y)) ||
                            (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_RETURN);
        if (accept) {
            Click();
            m.target = m.address.text;
            CloseDialog(m);
            m.finder.games.clear();
            ListGames(m);
            Look(m);
            return true;
        }
        if ((up && Hit(m.cancel.rect, e.button.x, e.button.y)) ||
            (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE)) {
            Click();
            CloseDialog(m);
            return true;
        }
        if (TypeIntoField(m.address, e)) return true;
        if (down && ClickListBox(m.recent, e.button.x, e.button.y)) {
            const auto& recent = Core::Profile::Current().recent_games;
            if (m.recent.selected >= 0 && m.recent.selected < static_cast<int>(recent.size())) {
                m.address.text = recent[m.recent.selected].address;
            }
        }
        return true;
    }
    if (down) {
        if (ClickListBox(m.connections, e.button.x, e.button.y)) {
            Core::Profile::Current().connection = static_cast<int>(Chosen());
            Core::Profile::Save(Core::Profile::Current());
            m.finder.games.clear();
            ListGames(m);
            Look(m);
            return true;
        }
        if (ClickListBox(m.games, e.button.x, e.button.y)) {
            if (m.games.selected >= static_cast<int>(m.listed.size())) m.games.selected = -1;
            // A double-click joins the game.
            if (e.button.clicks >= 2 && m.games.selected >= 0) {
                Click();
                Join(m);
            }
            return true;
        }
        return Hit(m.show.rect, e.button.x, e.button.y) || Hit(m.join.rect, e.button.x, e.button.y) ||
               Hit(m.create.rect, e.button.x, e.button.y) ||
               (m.restore.visible && Hit(m.restore.rect, e.button.x, e.button.y));
    }
    if (up) {
        if (Hit(m.show.rect, e.button.x, e.button.y)) {
            Click();
            if (Chosen() == kTcp) OpenDialog(m);
            else Look(m);
            return true;
        }
        if (Hit(m.join.rect, e.button.x, e.button.y) && m.games.selected >= 0) {
            Click();
            Join(m);
            return true;
        }
        if (Hit(m.create.rect, e.button.x, e.button.y)) {
            Click();
            Create(m);
            return true;
        }
        if (m.restore.visible && Hit(m.restore.rect, e.button.x, e.button.y)) {
            Click();
            CloseMenu();
            Screens::OpenRestoreList();
            return true;
        }
    }
    return false;
}

void Restore(const std::string& path) {
    Menu& m = M();
    Core::GameSetup setup;
    std::vector<uint8_t> save;
    Screens::Open(Id::MainMenu);
    OpenMenu();
    if (!Save::ReadBytes(path, save) || !Save::ReadSetup(save, setup)) {
        OpenNotice(m.notice, m.dialog_theme, Get(kNoSave), Get(kOk));
        return;
    }
    m.session.restore = std::move(save);
    if (!Net::OpenLobby(m.session, Net::kPort, setup, Core::Profile::Current().name, true)) {
        OpenNotice(m.notice, m.dialog_theme, Get(kCannotCreate), Get(kOk));
        return;
    }
    CloseMenu();
    Setup::OpenLobby(m.session);
}

void Update(Request& request) {
    Menu& m = M();
    if (m.open) {
        if (SDL_GetTicks() - m.looked >= kLookMs) Look(m);
        Net::Listen(m.finder, SDL_GetTicks());
        if (m.finder.games.size() != m.listed.size() ||
            !std::equal(m.listed.begin(), m.listed.end(), m.finder.games.begin(), [](const Net::Found& a, const Net::Found& b) {
                return a.address == b.address && a.port == b.port && a.name == b.name && a.restoring == b.restoring;
            })) {
            ListGames(m);
        }
    }
    if (Screens::Current() == Id::Multiplayer && m.waiting != 0) {
        Net::Pump(m.session);
        const bool late = SDL_GetTicks() - m.wait_from > kJoinMs;
        if (Net::Seated(m.session)) {
            m.waiting = 0;
            CloseMenu();
            Setup::OpenLobby(m.session);
        } else if (late || !m.session.failure.empty() || m.session.ejected || m.session.canceled) {
            const int why = m.session.ejected && m.session.refused != 0 ? m.session.refused : kNoLink;
            Net::Leave(m.session);
            m.waiting = 0;
            Screens::Open(Id::MainMenu);
            OpenMenu();
            OpenNotice(m.notice, m.dialog_theme, Get(why), Get(kOk));
        }
    }
    (void)request;
}

void DrawWait(SDL_Renderer* r, const Pointer& p) {
    (void)p;
    const Menu& m = M();
    DrawBackground(r, m.wait_theme);
    const int font = Text::GameFont(11);
    const std::string text = Get(m.waiting != 0 ? m.waiting : kCreating);
    std::vector<std::string_view> lines;
    Text::Wrap(font, text, kMenuWidth - 100, lines);
    int y = (kMenuHeight - static_cast<int>(lines.size()) * Text::Height(font)) / 2;
    for (const auto& line : lines) {
        const int w = Text::Width(font, line);
        Text::Draw(r, font, (kMenuWidth - w) / 2, y, line, SDL_Color{0, 0, 0, 255});
        y += Text::Height(font);
    }
}

void WaitEvent(const SDL_Event& e, const Pointer& p) {
    (void)p;
    Menu& m = M();
    if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE) {
        Net::Leave(m.session);
        m.waiting = 0;
        Screens::Open(Id::MainMenu);
        OpenMenu();
    }
}

Net::Session TakeSession() {
    Net::Session taken = std::move(M().session);
    M().session = Net::Session{};
    return taken;
}

} // namespace Screens::Multiplayer
