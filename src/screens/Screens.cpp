#include "Screens.h"

#include "Campaigns.h"
#include "Multiplayer.h"
#include "Setup.h"
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
    if (menu.editor_open) return true;
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
        else if (i == kMultiplayer && Multiplayer::MenuOpen()) frame = item.frame + 3;
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
    if (const int mp = Multiplayer::HelpAt(Pointer{S().mx, S().my, S().held}); mp >= 0) help = mp;
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
    Multiplayer::DrawMenu(r, theme, Pointer{S().mx, S().my, S().held});
    DrawCloseBox(r, theme);
}

void OpenFileList(Id which, bool restoring = false);

void MainMenuEvent(const SDL_Event& e) {
    MainMenu& menu = Menu();
    if (Multiplayer::MenuEvent(e, Pointer{S().mx, S().my, S().held}, S().request)) return;
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
                Multiplayer::CloseMenu();
                break;
            case kMultiplayer:
                // The Multiplayer menu on the plank.
                menu.single_open = menu.editor_open = false;
                if (Multiplayer::MenuOpen()) Multiplayer::CloseMenu();
                else Multiplayer::OpenMenu();
                break;
            case kZone: break;   // the Gaming Zone is long gone
            // Learn to Play: straight to William Wallace's map, its way
            // back the main menu.
            case kLearn: OpenCampaignMap(8, true); break;
            case kEditor:
                // The Map Editor's own menu on the plank.
                menu.editor_open = !menu.editor_open;
                menu.single_open = false;
                Multiplayer::CloseMenu();
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
            else Open(Id::CampaignEditor);
        } else if (sub >= 0 && sub == menu.sub_pressed && SubEnabled(sub)) {
            Click();
            switch (sub) {
            case 0: Open(Id::Campaigns); break;
            case 1: Setup::OpenSingle(0); break;
            case 2: Setup::OpenSingle(1); break;
            case 3: Setup::OpenSingle(2); break;
            case 4: Open(Id::CustomCampaigns); break;
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
        Multiplayer::CloseMenu();
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
    CheckBox taunts, one_click, two_button, widescreen, no_selection_limit;
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
    o.no_selection_limit = CheckBox{SDL_Rect{300, 520, 30, 27}, profile.no_selection_limit};
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
    // No selection limit (Profile::Options::no_selection_limit), likewise.
    DrawCheckBox(r, theme, o.no_selection_limit, S().mx, S().my, S().held);
    DrawText(r, Text::kGeorgia10B, o.no_selection_limit.box.x + o.no_selection_limit.box.w + 10,
             o.no_selection_limit.box.y + 5, "Disable selection limit", ink, ink, Align::Left, false);
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
        for (CheckBox* c : {&o.taunts, &o.one_click, &o.two_button, &o.widescreen, &o.no_selection_limit}) {
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
            profile.no_selection_limit = o.no_selection_limit.checked;
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
// Lists of files to choose from: saved games and recorded
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

std::string AiDir() {
    const std::string base = HAL::Platform::PrefPath("AI");
    return base.empty() ? "../assets/original/AI/" : base;
}

std::string CampaignsDir() {
    const std::string base = HAL::Platform::PrefPath("Campaign");
    return base.empty() ? "../assets/original/Campaign/" : base;
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
    f.paths = FilesIn(SavedGamesDir(), {".gam", ".mgl"});
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
    DrawText(r, Text::kBlackletter28B, 400, 20, Get(9223), ink, ink, Align::Centre, false);
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
            S().request.entry = -1;
            const std::string ext = std::filesystem::path(S().request.path).extension().string();
            if (f.restoring && ext == ".gam") {
                Multiplayer::Restore(S().request.path);
                S().request = Request{};
                return;
            }
            Ask(ext == ".mgl" ? Request::Kind::PlayRecording : Request::Kind::LoadGame);
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
    Setup::Init();
    Multiplayer::Init();
    OptionsState().theme = MakeTheme(Sin::kParchment, kMenuWidth, kMenuHeight);
    HistoryState().theme = MakeTheme(Sin::kHistory, kMenuWidth, kMenuHeight);
    Files().theme = MakeTheme(Sin::kParchment, kMenuWidth, kMenuHeight);
    LoadingState().theme = MakeTheme(Sin::kParchment, kMenuWidth, kMenuHeight);
    // A custom campaign is any but the game's own five.
    Menu().has_campaigns = !Campaigns::CustomCampaigns().empty();
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
    case Id::Campaigns: Campaigns::PrepareBook(); break;
    case Id::ScenarioSelect: Campaigns::PrepareSelect(); break;
    case Id::CustomCampaigns: Campaigns::PrepareCustom(); break;
    case Id::CampaignEditor: Campaigns::PrepareCampaignEditor(); break;
    default: break;
    }
    if (screen != Id::MultiplayerSetup) {
        HAL::TextInput::Want(screen == Id::Options || screen == Id::CampaignEditor ? HAL::TextInput::Kind::Fields
                                                                                   : HAL::TextInput::Kind::None);
    }
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
    case Id::GameSetup:
    case Id::MultiplayerSetup: Setup::HandleEvent(e, Pointer{s.mx, s.my, s.held}, s.request); break;
    case Id::Options: OptionsEvent(e); break;
    case Id::History: HistoryEvent(e); break;
    case Id::Multiplayer: Multiplayer::WaitEvent(e, Pointer{s.mx, s.my, s.held}); break;
    case Id::Campaigns:
    case Id::CampaignMap:
    case Id::ScenarioSelect:
    case Id::Mission:
    case Id::Aftermath:
    case Id::CustomCampaigns:
    case Id::CampaignEditor:
        Campaigns::HandleEvent(s.current, e, Campaigns::Pointer{s.mx, s.my, s.held}, s.request);
        break;
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
    case Id::GameSetup:
    case Id::MultiplayerSetup: Setup::Draw(r, Pointer{S().mx, S().my, S().held}); break;
    case Id::Options: DrawOptions(r); break;
    case Id::History: DrawHistory(r); break;
    case Id::Multiplayer: Multiplayer::DrawWait(r, Pointer{S().mx, S().my, S().held}); break;
    case Id::Campaigns:
    case Id::CampaignMap:
    case Id::ScenarioSelect:
    case Id::Mission:
    case Id::Aftermath:
    case Id::CustomCampaigns:
    case Id::CampaignEditor:
        Campaigns::Draw(S().current, r, Campaigns::Pointer{S().mx, S().my, S().held});
        break;
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

void Update() {
    Multiplayer::Update(S().request);
    Setup::Update(S().request);
}

Net::Session TakeNetworkSession() { return Multiplayer::TakeSession(); }

void OpenRestoreList() { OpenFileList(Id::SavedGames, true); }

void OpenMultiplayerMenu() { Multiplayer::OpenMenu(); }

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
