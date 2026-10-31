#include "GameDialogs.h"

#include "Achievements.h"
#include "Screens.h"
#include "Sin.h"
#include "TechTree.h"
#include "Widgets.h"
#include "audio/InterfaceSounds.h"
#include "audio/Music.h"
#include "core/CommandQueue.h"
#include "core/Hotkeys.h"
#include "core/GameSetup.h"
#include "core/Profile.h"
#include "core/Strings.h"
#include "core/Types.h"
#include "ecs/Chat.h"
#include "ecs/Cheats.h"
#include "ecs/Diplomacy.h"
#include "ecs/Players.h"
#include "ecs/Triggers.h"
#include "ecs/Tribute.h"
#include "ecs/Victory.h"
#include "ecs/Vision.h"
#include "hal/Audio.h"
#include "hal/GraphicCache.h"
#include "hal/Window.h"
#include "rendering/Messages.h"
#include "rendering/UIRenderer.h"
#include "rendering/Text.h"

#include <algorithm>
#include <filesystem>
#include <cstdio>
#include <string>
#include <vector>

namespace GameDialogs {

namespace Text = Rendering::Text;
using Core::Strings::Get;
using namespace Screens;

namespace {

struct Shared {
    Dialog dialog = Dialog::None;
    Outcome outcome = Outcome::None;
    bool paused = false;
    // The question a Confirm dialog asks, and what yes does.
    int question = -1;
    Outcome on_yes = Outcome::None;
    // The end of the game: the message shown, when it began.
    int over_message = -1;
    bool ended = false;          // Outcome::Ended handed out
    uint32_t over_at = 0;
    // The Achievements screen: its page, its buttons, why it opened; and
    // whether this is a game against the computer, which offers Play Again.
    Achievements::State achievements;
    bool single_player = true;
    // The Objectives dialog's tab -- 0 the objectives, 1 the hints -- and
    // the first line shown of its text.
    int objectives_tab = 0;
    int objectives_scroll = 0;
    int mx = -1, my = -1;
    bool held = false;
    // The Diplomacy dialog's choices, applied only on OK: the stance toward
    // each player, the tribute heaped on each icon, allied victory.
    uint8_t stance[Core::kMaxPlayers] = {};
    int tribute[Core::kMaxPlayers][4] = {};
    bool allied_victory = false;
    bool diplomacy_fresh = false;
    // The Chat dialog's choice of who hears, and what is being typed there or
    // on the chat line.
    bool hears[Core::kMaxPlayers] = {};
    TextField chat;
    // The Options dialog: the screen size chosen, the three sliders and the
    // three checks, applied only on OK.
    int option_size = 1;
    Slider music, sound, scroll;
    CheckBox taunts, one_click, two_button, no_selection_limit;
    Slider top_bar, panel, text;   // the interface's sizes
    // The Save Game dialog: the games already saved, and the name typed.
    ListBox saves;
    TextField save_name;
    std::string chosen_name;
};

Shared& S() {
    static Shared state;
    return state;
}

void Click() { HAL::Audio::Play(Audio::InterfaceSound::kButton2, 1.0f, 0.0f); }

// The in-game dialogs' art: the size of its parchment (the rest of the art
// is the shadow it casts, right and below) and the middle of its title
// plate. A dialog is placed in a box its parchment less 15 by 8, centred on
// the screen, the shadow falling past it.
struct DialogArt {
    uint32_t sin;
    SDL_Point parchment;
    SDL_Point plate;   // {0, 0}: none
};
constexpr DialogArt kDialogArt[] = {
    {Sin::kDiplomacy, {755, 474}, {386, 34}},
    {Sin::kGameMenu, {293, 299}, {0, 0}},
    {Sin::kObjectives, {462, 495}, {247, 28}},
    {Sin::kChat, {591, 521}, {271, 32}},
    {Sin::kGameDialog, {675, 444}, {346, 34}},
    {Sin::kChat2, {567, 602}, {273, 27}},
};
const DialogArt* ArtOf(uint32_t sin) {
    for (const auto& a : kDialogArt) {
        if (a.sin == sin) return &a;
    }
    return nullptr;
}
SDL_Point ParchmentOf(const Theme& theme, uint32_t sin) {
    const DialogArt* art = ArtOf(sin);
    return art != nullptr ? art->parchment : BackgroundSize(theme);
}
void Place(Theme& theme, uint32_t sin, int w, int h) {
    if (const DialogArt* art = ArtOf(sin)) {
        theme.origin = SDL_Point{(w - (art->parchment.x - 15)) / 2, (h - (art->parchment.y - 8)) / 2};
    }
}
// A dialog's title, in the screen-title face, centred on its plate.
void DrawTitle(SDL_Renderer* r, const Theme& theme, uint32_t sin, int title) {
    const DialogArt* art = ArtOf(sin);
    const SDL_Point plate = art != nullptr && art->plate.x > 0
        ? art->plate : SDL_Point{BackgroundSize(theme).x / 2, 34};
    DrawText(r, Text::kBlackletter21B, theme.origin.x + plate.x, theme.origin.y + plate.y - 11,
             Get(title), theme.text, theme.text_shadow, Align::Centre);
}

// The game menu's buttons, top to bottom, by their LANGUAGE.DLL names and
// what each does.
struct MenuButton {
    int label;
    Outcome outcome;
    Dialog opens;
    int confirm;   // a question to ask first, or -1
};
constexpr MenuButton kMenuButtons[] = {
    {9273, Outcome::QuitToMenu, Dialog::None, 9283},   // Quit Current Game
    {9271, Outcome::None, Dialog::Achievements, -1},   // Achievements
    {9272, Outcome::None, Dialog::SaveGame, -1},       // Save
    {9276, Outcome::Load, Dialog::None, 3100},         // Load
    {9279, Outcome::Restart, Dialog::None, 9279},      // Restart
    {9274, Outcome::None, Dialog::Options, -1},        // Options
    {9280, Outcome::Resign, Dialog::None, 9280},       // Resign
    {9275, Outcome::None, Dialog::None, -1},           // Cancel
};
constexpr int kMenuButtonW = 240;
constexpr int kMenuButtonH = 30;
constexpr int kMenuButtonPitch = 35;

SDL_Rect MenuButtonRect(const Theme& theme, int i) {
    const SDL_Point size = ParchmentOf(theme, Sin::kGameMenu);
    const int top = theme.origin.y + (size.y - static_cast<int>(std::size(kMenuButtons)) *
                                                  kMenuButtonPitch) / 2;
    return SDL_Rect{theme.origin.x + (size.x - kMenuButtonW) / 2, top + i * kMenuButtonPitch,
                    kMenuButtonW, kMenuButtonH};
}

Theme& MenuTheme(int w, int h) {
    static Theme theme;
    static int cached_w = 0, cached_h = 0;
    if (cached_w != w || cached_h != h) {
        theme = MakeTheme(Sin::kGameMenu, w, h, true);
        Place(theme, Sin::kGameMenu, w, h);
        cached_w = w;
        cached_h = h;
    }
    return theme;
}

Theme& GameDialogTheme(uint32_t sin, int w, int h) {
    struct Cached { uint32_t sin; int w, h; Theme theme; };
    static std::vector<Cached> cache;
    for (auto& c : cache) {
        if (c.sin == sin && c.w == w && c.h == h) return c.theme;
    }
    cache.push_back({sin, w, h, MakeTheme(sin, w, h, true)});
    Place(cache.back().theme, sin, w, h);
    return cache.back().theme;
}

// The yes-or-no box: over the game menu's parchment.
SDL_Rect ConfirmButton(const Theme& theme, bool yes) {
    const SDL_Point size = ParchmentOf(theme, Sin::kGameMenu);
    const int y = theme.origin.y + size.y - 70;
    const int x = yes ? theme.origin.x + 30 : theme.origin.x + size.x - 30 - 110;
    return SDL_Rect{x, y, 110, 30};
}

void DrawMenu(SDL_Renderer* r, int w, int h) {
    const Theme& theme = MenuTheme(w, h);
    DrawBackground(r, theme);
    for (int i = 0; i < static_cast<int>(std::size(kMenuButtons)); ++i) {
        Button b{MenuButtonRect(theme, i), Get(kMenuButtons[i].label), Text::kBlackletter14};
        DrawButton(r, theme, b, StateOf(b, S().mx, S().my, S().held));
    }
}

void DrawConfirm(SDL_Renderer* r, int w, int h) {
    const Theme& theme = MenuTheme(w, h);
    DrawBackground(r, theme);
    const SDL_Point size = ParchmentOf(theme, Sin::kGameMenu);
    std::string question = Get(S().question);
    if (S().question == 9283) question = Get(9283) + "?";
    DrawWrapped(r, Text::kGeorgia10B, theme.origin.x + 30, theme.origin.y + 80, size.x - 60,
                question, SDL_Color{0, 0, 0, 255}, SDL_Color{0, 0, 0, 255}, false);
    for (bool yes : {true, false}) {
        Button b{ConfirmButton(theme, yes), Get(yes ? 4003 : 4004), Text::kBlackletter14};
        DrawButton(r, theme, b, StateOf(b, S().mx, S().my, S().held));
    }
}

// A dialog with no contents of its own: its art, its title and a Cancel
// button.
void DrawPlaceholder(SDL_Renderer* r, uint32_t sin, int title, int w, int h) {
    const Theme& theme = GameDialogTheme(sin, w, h);
    DrawBackground(r, theme);
    const SDL_Point size = ParchmentOf(theme, sin);
    DrawTitle(r, theme, sin, title);
    Button close{SDL_Rect{theme.origin.x + (size.x - 120) / 2, theme.origin.y + size.y - 60, 120, 30},
                 Get(9275), Text::kBlackletter14};
    DrawButton(r, theme, close, StateOf(close, S().mx, S().my, S().held));
}

SDL_Rect PlaceholderClose(uint32_t sin, int w, int h) {
    const Theme& theme = GameDialogTheme(sin, w, h);
    const SDL_Point size = ParchmentOf(theme, sin);
    return SDL_Rect{theme.origin.x + (size.x - 120) / 2, theme.origin.y + size.y - 60, 120, 30};
}

// The Save Game dialog, on the in-game dialog art: the saved games in a list
// -- clicking one takes its name -- the name in a field under it, and Save
// and Cancel.
struct SaveLayout {
    SDL_Rect list, field, save, cancel;
};
constexpr int kGameDialogParchmentW = 675;
constexpr int kGameDialogParchmentH = 444;

SaveLayout SaveRects(const Theme& theme) {
    const int x = theme.origin.x + 45;
    const int w = kGameDialogParchmentW - 90;
    const int bottom = theme.origin.y + kGameDialogParchmentH - 62;
    const int centre = theme.origin.x + kGameDialogParchmentW / 2;
    return SaveLayout{SDL_Rect{x, theme.origin.y + 82, w, 220},
                      SDL_Rect{x, theme.origin.y + 318, w, 26},
                      SDL_Rect{centre - 150, bottom, 140, 30},
                      SDL_Rect{centre + 10, bottom, 140, 30}};
}

// ---- Options ---------------------------------------------------------------
//
// The game's options from inside a game, on the same art: the screen size,
// the music, sound and scrolling, and the mouse and garrison choices -- the
// main menu's Options less the name, with the same LANGUAGE.DLL labels.
struct OptionsLayout {
    SDL_Rect sizes[3];
    SDL_Rect ok, cancel;
    int label_right;
};

OptionsLayout OptionsRects(const Theme& theme) {
    const int ox = theme.origin.x, oy = theme.origin.y;
    OptionsLayout l{};
    for (int i = 0; i < 3; ++i) l.sizes[i] = SDL_Rect{ox + 250 + i * 130, oy + 82, 120, 28};
    const int bottom = oy + kGameDialogParchmentH - 62;
    const int centre = ox + kGameDialogParchmentW / 2;
    l.ok = SDL_Rect{centre - 150, bottom, 140, 30};
    l.cancel = SDL_Rect{centre + 10, bottom, 140, 30};
    l.label_right = ox + 230;
    return l;
}

void OpenOptions(int w, int h) {
    Shared& s = S();
    const Theme& theme = GameDialogTheme(Sin::kGameDialog, w, h);
    const int ox = theme.origin.x, oy = theme.origin.y;
    const auto& profile = Core::Profile::Current();
    s.option_size = profile.screen_width <= 800 ? 0 : profile.screen_width <= 1024 ? 1 : 2;
    s.music = Slider{SDL_Rect{ox + 250, oy + 135, 260, 22}, profile.music_volume, 10};
    s.sound = Slider{SDL_Rect{ox + 250, oy + 180, 260, 22}, profile.sound_volume, 10};
    s.scroll = Slider{SDL_Rect{ox + 250, oy + 225, 260, 22}, profile.scroll_speed, 10};
    s.taunts = CheckBox{SDL_Rect{ox + 250, oy + 263, 30, 27}, profile.audio_taunts};
    s.one_click = CheckBox{SDL_Rect{ox + 250, oy + 291, 30, 27}, profile.one_click_garrison};
    s.two_button = CheckBox{SDL_Rect{ox + 250, oy + 319, 30, 27}, profile.two_button_mouse};
    s.no_selection_limit = CheckBox{SDL_Rect{ox + 250, oy + 347, 30, 27}, profile.no_selection_limit};
    // The interface's sizes, in a column of their own on the right.
    s.top_bar = SizeSlider(SDL_Rect{ox + 530, oy + 155, 120, 22}, profile.top_bar_size);
    s.panel = SizeSlider(SDL_Rect{ox + 530, oy + 225, 120, 22}, profile.panel_size);
    s.text = SizeSlider(SDL_Rect{ox + 530, oy + 295, 120, 22}, profile.text_size);
}

void DrawOptionsDialog(SDL_Renderer* r, int w, int h) {
    const Theme& theme = GameDialogTheme(Sin::kGameDialog, w, h);
    DrawBackground(r, theme);
    DrawTitle(r, theme, Sin::kGameDialog, 9274);
    Shared& s = S();
    const OptionsLayout l = OptionsRects(theme);
    const SDL_Color ink = {0, 0, 0, 255};
    DrawText(r, Text::kGeorgia10B, l.label_right, l.sizes[0].y + 6, Get(9521), ink, ink,
             Align::Right, false);
    static const int kSizeNames[3] = {9522, 9523, 9524};
    for (int i = 0; i < 3; ++i) {
        Button b{l.sizes[i], Get(kSizeNames[i]), Text::kGeorgia10B};
        State state = StateOf(b, s.mx, s.my, s.held);
        if (i == s.option_size && state == State::Normal) state = State::Selected;
        DrawButton(r, theme, b, state);
    }
    struct Labelled { int label; const Slider* slider; int low; int high; };
    const Labelled sliders[] = {
        {9515, &s.music, 9517, 9516}, {9514, &s.sound, 9517, 9516}, {9518, &s.scroll, 9520, 9519},
    };
    for (const auto& lab : sliders) {
        std::string label = Get(lab.label);
        std::replace(label.begin(), label.end(), '\n', ' ');
        DrawText(r, Text::kGeorgia10B, l.label_right, lab.slider->track.y + 2, label, ink, ink,
                 Align::Right, false);
        DrawSlider(r, theme, *lab.slider);
        DrawText(r, Text::kArial8, lab.slider->track.x, lab.slider->track.y + 24, Get(lab.low), ink,
                 ink, Align::Left, false);
        DrawText(r, Text::kArial8, lab.slider->track.x + lab.slider->track.w,
                 lab.slider->track.y + 24, Get(lab.high), ink, ink, Align::Right, false);
    }
    struct Box { int label; const CheckBox* c; };
    const Box boxes[] = {{9526, &s.taunts}, {9527, &s.one_click}, {9528, &s.two_button}};
    for (const auto& b : boxes) {
        DrawCheckBox(r, theme, *b.c, s.mx, s.my, s.held);
        DrawText(r, Text::kGeorgia10B, b.c->box.x + b.c->box.w + 10, b.c->box.y + 5, Get(b.label),
                 ink, ink, Align::Left, false);
    }
    // No selection limit (Profile::Options::no_selection_limit); its label
    // is in no string table.
    DrawCheckBox(r, theme, s.no_selection_limit, s.mx, s.my, s.held);
    DrawText(r, Text::kGeorgia10B, s.no_selection_limit.box.x + s.no_selection_limit.box.w + 10,
             s.no_selection_limit.box.y + 5, "Disable selection limit", ink, ink, Align::Left, false);
    DrawSizeSlider(r, theme, s.top_bar, "Top Bar Size");
    DrawSizeSlider(r, theme, s.panel, "Panel Size");
    DrawSizeSlider(r, theme, s.text, "Text Size");
    Button ok{l.ok, Get(4001), Text::kBlackletter14};
    State state = StateOf(ok, s.mx, s.my, s.held);
    if (state == State::Normal) state = State::Hover;
    DrawButton(r, theme, ok, state);
    Button cancel{l.cancel, Get(4002), Text::kBlackletter14};
    DrawButton(r, theme, cancel, StateOf(cancel, s.mx, s.my, s.held));
}

// An event in the Options dialog; true when it closed it.
bool OptionsEventIn(const SDL_Event& e, int w, int h) {
    Shared& s = S();
    const Theme& theme = GameDialogTheme(Sin::kGameDialog, w, h);
    const OptionsLayout l = OptionsRects(theme);
    if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
        for (Slider* slider : {&s.music, &s.sound, &s.scroll, &s.top_bar, &s.panel, &s.text}) {
            if (DragSlider(*slider, e.button.x, e.button.y)) return false;
        }
        for (CheckBox* c : {&s.taunts, &s.one_click, &s.two_button, &s.no_selection_limit}) {
            if (Hit(c->box, e.button.x, e.button.y)) {
                c->checked = !c->checked;
                Click();
                return false;
            }
        }
    } else if (e.type == SDL_MOUSEMOTION && s.held) {
        for (Slider* slider : {&s.music, &s.sound, &s.scroll, &s.top_bar, &s.panel, &s.text}) {
            DragSlider(*slider, e.motion.x, e.motion.y);
        }
    } else if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
        for (int i = 0; i < 3; ++i) {
            if (Hit(l.sizes[i], e.button.x, e.button.y)) {
                s.option_size = i;
                Click();
            }
        }
        if (Hit(l.ok, e.button.x, e.button.y)) {
            Click();
            auto& profile = Core::Profile::Current();
            static const int kW[3] = {800, 1024, 1280};
            static const int kH[3] = {600, 768, 1024};
            const bool resized = profile.screen_width != kW[s.option_size];
            profile.screen_width = kW[s.option_size];
            profile.screen_height = kH[s.option_size];
            profile.music_volume = s.music.value;
            profile.sound_volume = s.sound.value;
            profile.scroll_speed = s.scroll.value;
            profile.audio_taunts = s.taunts.checked;
            profile.one_click_garrison = s.one_click.checked;
            profile.two_button_mouse = s.two_button.checked;
            profile.no_selection_limit = s.no_selection_limit.checked;
            profile.top_bar_size = SizeOf(s.top_bar);
            profile.panel_size = SizeOf(s.panel);
            profile.text_size = SizeOf(s.text);
            Core::Profile::Save(profile);
            HAL::Audio::SetVolume(static_cast<float>(profile.sound_volume) / 10.0f);
            Audio::Music::SetVolume(profile.music_volume);
            // The game is laid out again at the new size at once.
            if (resized) {
                if (HAL::Window* window = HAL::MainWindow()) {
                    window->SetGameSize(profile.widescreen ? window->WideWidth(profile.screen_height)
                                                           : profile.screen_width,
                                        profile.screen_height);
                }
            }
            return true;
        }
        if (Hit(l.cancel, e.button.x, e.button.y)) {
            Click();
            return true;
        }
    }
    return false;
}

void OpenSave() {
    Shared& s = S();
    s.saves = ListBox{};
    s.saves.font = Text::kGeorgia10B;
    for (const auto& path : Screens::FilesIn(Screens::SavedGamesDir(), {".gam"})) {
        s.saves.items.push_back(std::filesystem::path(path).stem().string());
    }
    s.save_name.font = Text::kGeorgia10B;
    s.save_name.max_length = 40;
    s.save_name.focused = true;
}

void DrawSave(SDL_Renderer* r, int w, int h) {
    const Theme& theme = GameDialogTheme(Sin::kGameDialog, w, h);
    DrawBackground(r, theme);
    DrawTitle(r, theme, Sin::kGameDialog, 9411);
    Shared& s = S();
    const SaveLayout l = SaveRects(theme);
    s.saves.rect = l.list;
    s.save_name.rect = l.field;
    DrawListBox(r, theme, s.saves, s.mx, s.my);
    DrawTextField(r, theme, s.save_name);
    Button save{l.save, Get(9272), Text::kBlackletter14};
    save.enabled = !s.save_name.text.empty();
    DrawButton(r, theme, save, StateOf(save, s.mx, s.my, s.held));
    Button cancel{l.cancel, Get(9275), Text::kBlackletter14};
    DrawButton(r, theme, cancel, StateOf(cancel, s.mx, s.my, s.held));
}

// One frame of an interface sheet at its own size, its player colour drawn
// over it.
void DrawArt(SDL_Renderer* r, uint32_t slp, int frame, int x, int y, int player = 1) {
    auto& graphics = HAL::GraphicCache::Get();
    const HAL::GraphicAtlas* atlas = graphics.GetInterfaceAtlas(slp);
    if (atlas == nullptr || atlas->base == nullptr || frame < 0 ||
        frame >= static_cast<int>(atlas->frames.size())) {
        return;
    }
    const HAL::GraphicFrame& f = atlas->frames[static_cast<size_t>(frame)];
    const SDL_Rect dst = {x - f.hotspot_x, y - f.hotspot_y, f.rect.w, f.rect.h};
    SDL_RenderCopy(r, atlas->base, &f.rect, &dst);
    if (SDL_Texture* tint = graphics.GetInterfacePlayerLayer(slp, player)) {
        SDL_RenderCopy(r, tint, &f.rect, &dst);
    }
}

// ---- Diplomacy ----------------------------------------------------------
//
// Placed as every in-game dialog is (Place). A row per player, the viewer's
// first: the name on its banner (interfac.drs 50746, in the player's
// colour), the civilisation, how they regard the viewer; then the viewer's
// stance in three round buttons of the dialog's button art and a tribute
// button per stockpile (50732), the viewer's own row carrying the stockpiles
// over them.
constexpr uint32_t kBanner = 50746;
constexpr uint32_t kTributeIcons = 50732;     // wood, stone, food, gold
constexpr int kDipRowTop = 138;               // the viewer's row, art-relative
constexpr int kDipRowPitch = 30;
constexpr int kDipNameX = 35, kDipCivX = 186, kDipTheirX = 265;
constexpr int kDipStanceX[3] = {335, 413, 481};       // ally, neutral, enemy
constexpr int kDipStanceHeadX[3] = {359, 422, 506};
constexpr int kDipTributeX[4] = {535, 584, 632, 682}; // wood, food, gold, stone
constexpr int kDipTributeFrame[4] = {0, 2, 3, 1};
constexpr uint8_t kDipTributeResource[4] = {1, 0, 3, 2};  // dat resources
constexpr ECS::Diplomacy::Stance kDipStances[3] = {
    ECS::Diplomacy::Stance::Ally, ECS::Diplomacy::Stance::Neutral, ECS::Diplomacy::Stance::Enemy};

Theme DiplomacyTheme(int w, int h) {
    return GameDialogTheme(Sin::kDiplomacy, w, h);
}

// The players in the rows, the viewer first.
std::vector<Core::PlayerId> DiplomacyRows(const entt::registry& registry) {
    std::vector<Core::PlayerId> rows;
    const Core::PlayerId me = Core::LocalPlayer();
    if (ECS::Players::Find(registry, me) != nullptr) rows.push_back(me);
    for (int p = 1; p < Core::kMaxPlayers; ++p) {
        const auto* player = ECS::Players::Find(registry, static_cast<Core::PlayerId>(p));
        if (player != nullptr && player->active && p != me) rows.push_back(static_cast<Core::PlayerId>(p));
    }
    return rows;
}

int StanceIndex(ECS::Diplomacy::Stance stance) {
    return stance == ECS::Diplomacy::Stance::Ally ? 0 : stance == ECS::Diplomacy::Stance::Neutral ? 1 : 2;
}

void OpenDiplomacy(const entt::registry& registry) {
    Shared& s = S();
    const Core::PlayerId me = Core::LocalPlayer();
    for (int p = 0; p < Core::kMaxPlayers; ++p) {
        s.stance[p] = static_cast<uint8_t>(StanceIndex(
            ECS::Diplomacy::StanceOf(registry, me, static_cast<Core::PlayerId>(p))));
        for (int& t : s.tribute[p]) t = 0;
    }
    s.allied_victory = ECS::Diplomacy::AlliedVictory(registry, me);
}

SDL_Rect DipButton(const Theme& t, int x, int row) {
    return SDL_Rect{t.origin.x + x, t.origin.y + kDipRowTop + row * kDipRowPitch - 6, 39, 39};
}
SDL_Rect DipTribute(const Theme& t, int column, int row) {
    return SDL_Rect{t.origin.x + kDipTributeX[column],
                    t.origin.y + kDipRowTop + row * kDipRowPitch - 6, 34, 30};
}
SDL_Rect DipBottom(const Theme& t, int which) {
    static const int kX[3] = {35, 267, 498};
    return SDL_Rect{t.origin.x + kX[which], t.origin.y + 419, 214, 28};
}
SDL_Rect DipAlliedVictory(const Theme& t) { return SDL_Rect{t.origin.x + 340, t.origin.y + 375, 39, 39}; }

void DrawDiplomacy(SDL_Renderer* r, ECS::Simulation& sim, int w, int h) {
    const Theme theme = DiplomacyTheme(w, h);
    DrawBackground(r, theme);
    const auto& registry = sim.GetRegistry();
    Shared& s = S();
    const int ox = theme.origin.x, oy = theme.origin.y;
    const SDL_Color ink{0, 0, 0, 255};
    const SDL_Color white{255, 255, 255, 255};
    DrawTitle(r, theme, Sin::kDiplomacy, 9851);
    // The column heads.
    DrawText(r, Text::kGeorgia9B, ox + kDipNameX, oy + 109, Get(9862), ink, ink, Align::Left, false);
    DrawText(r, Text::kGeorgia9B, ox + kDipCivX, oy + 109, Get(9852), ink, ink, Align::Centre, false);
    DrawText(r, Text::kGeorgia9B, ox + kDipTheirX, oy + 93, Get(9865), ink, ink, Align::Centre, false);
    DrawText(r, Text::kGeorgia9B, ox + kDipTheirX, oy + 109, Get(9866), ink, ink, Align::Centre, false);
    DrawText(r, Text::kGeorgia9B, ox + 427, oy + 93, Get(9864), ink, ink, Align::Centre, false);
    for (int i = 0; i < 3; ++i) {
        DrawText(r, Text::kGeorgia9B, ox + kDipStanceHeadX[i], oy + 109, Get(9853 + (i == 0 ? 0 : i == 1 ? 1 : 2)),
                 ink, ink, Align::Centre, false);
    }
    const Core::PlayerId me = Core::LocalPlayer();
    const ECS::Players::Player* mine = ECS::Players::Find(registry, me);
    const bool locked = ECS::Diplomacy::Locked(registry);
    const auto rows = DiplomacyRows(registry);
    for (int row = 0; row < static_cast<int>(rows.size()); ++row) {
        const Core::PlayerId p = rows[static_cast<size_t>(row)];
        const ECS::Players::Player* player = ECS::Players::Find(registry, p);
        const int y = oy + kDipRowTop + row * kDipRowPitch;
        DrawArt(r, kBanner, 0, ox + kDipNameX - 1, y - 6, p);
        DrawText(r, Text::kGeorgia9B, ox + kDipNameX, y,
                 Rendering::UIRenderer::PlayerName(registry, p), white, ink);
        if (player != nullptr) {
            DrawText(r, Text::kGeorgia9B, ox + kDipCivX, y, Get(10230 + player->civ), white, ink,
                     Align::Centre);
        }
        if (p == me) {
            // The viewer's own stockpiles, over the tribute buttons.
            static const Core::ResourceType kKinds[4] = {Core::ResourceType::Wood, Core::ResourceType::Food,
                                                         Core::ResourceType::Gold, Core::ResourceType::Stone};
            for (int c = 0; c < 4 && mine != nullptr; ++c) {
                DrawText(r, Text::kGeorgia9B, ox + kDipTributeX[c] + 17, y,
                         std::to_string(ECS::Players::Stock(*mine, kKinds[c])), white, ink, Align::Centre);
            }
            continue;
        }
        const ECS::Diplomacy::Stance theirs = ECS::Diplomacy::StanceOf(registry, p, me);
        DrawText(r, Text::kGeorgia9B, ox + kDipTheirX, y, Get(9853 + StanceIndex(theirs)), white, ink,
                 Align::Centre);
        for (int i = 0; i < 3; ++i) {
            const SDL_Rect b = DipButton(theme, kDipStanceX[i], row);
            const bool chosen = s.stance[p] == i;
            const bool down = !locked && s.held && Hit(b, s.mx, s.my);
            DrawButtonArt(r, theme, (chosen ? 6 : 4) + (down ? 1 : 0), b.x, b.y);
        }
        for (int c = 0; c < 4; ++c) {
            const SDL_Rect b = DipTribute(theme, c, row);
            DrawArt(r, kTributeIcons, kDipTributeFrame[c], b.x, b.y);
            if (s.tribute[p][c] > 0) {
                DrawTextIn(r, Text::kGeorgia9B, b, std::to_string(s.tribute[p][c]), white, ink);
            }
        }
    }
    // Allied victory, the help button, and the three at the foot.
    const SDL_Rect av = DipAlliedVictory(theme);
    DrawButtonArt(r, theme, s.allied_victory ? 2 : 0, av.x, av.y);
    DrawText(r, Text::kGeorgia9B, av.x + 46, av.y + 11, Get(9857), white, ink);
    Button help{SDL_Rect{ox + 684, oy + 381, 28, 28}, "?", Text::kBlackletter14};
    DrawButton(r, theme, help, StateOf(help, s.mx, s.my, s.held));
    const int labels[3] = {9858, 9859, 9275};
    for (int i = 0; i < 3; ++i) {
        Button b{DipBottom(theme, i), Get(labels[i]), Text::kBlackletter14};
        State state = StateOf(b, s.mx, s.my, s.held);
        // OK is the dialog's default, its label in the focus colour.
        if (i == 0 && state == State::Normal) state = State::Hover;
        DrawButton(r, theme, b, state);
    }
}

// A click in the Diplomacy dialog; true when it closed the dialog.
bool DiplomacyClick(ECS::Simulation& sim, int x, int y, int w, int h) {
    const Theme theme = DiplomacyTheme(w, h);
    auto& registry = sim.GetRegistry();
    Shared& s = S();
    const Core::PlayerId me = Core::LocalPlayer();
    const bool locked = ECS::Diplomacy::Locked(registry);
    const auto rows = DiplomacyRows(registry);
    for (int row = 0; row < static_cast<int>(rows.size()); ++row) {
        const Core::PlayerId p = rows[static_cast<size_t>(row)];
        if (p == me) continue;
        for (int i = 0; i < 3 && !locked; ++i) {
            if (Hit(DipButton(theme, kDipStanceX[i], row), x, y)) {
                Click();
                s.stance[p] = static_cast<uint8_t>(i);
                return false;
            }
        }
        for (int c = 0; c < 4; ++c) {
            if (!Hit(DipTribute(theme, c, row), x, y)) continue;
            const int amount = s.tribute[p][c] + 100;
            const auto refusal = ECS::Tribute::Check(registry, me, p, kDipTributeResource[c], amount);
            if (refusal == ECS::Tribute::Refusal::None) {
                Click();
                s.tribute[p][c] = amount;
            } else if (refusal == ECS::Tribute::Refusal::NoMarket) {
                Rendering::Messages::Post(Get(9863));
            } else if (refusal == ECS::Tribute::Refusal::CannotPay) {
                // "Not enough food." and its fellows, by dat resource.
                Rendering::Messages::Post(Get(3001 + kDipTributeResource[c]));
            }
            return false;
        }
    }
    if (!locked && Hit(DipAlliedVictory(theme), x, y)) {
        Click();
        s.allied_victory = !s.allied_victory;
        return false;
    }
    if (Hit(DipBottom(theme, 1), x, y)) {   // Clear Tributes
        Click();
        for (auto& row : s.tribute) {
            for (int& t : row) t = 0;
        }
        return false;
    }
    if (Hit(DipBottom(theme, 0), x, y)) {   // OK: everything chosen, as orders
        Click();
        auto& queue = Core::CommandQueue::Get();
        for (Core::PlayerId p : rows) {
            if (p == me) continue;
            const uint8_t chosen = static_cast<uint8_t>(kDipStances[s.stance[p]]);
            if (!locked && kDipStances[s.stance[p]] != ECS::Diplomacy::StanceOf(registry, me, p)) {
                queue.PushCommand(Core::DiplomacyCommand{p, chosen, me});
            }
            for (int c = 0; c < 4; ++c) {
                if (s.tribute[p][c] > 0) {
                    queue.PushCommand(Core::TributeCommand{p, kDipTributeResource[c], s.tribute[p][c], me});
                }
            }
        }
        if (!locked && s.allied_victory != ECS::Diplomacy::AlliedVictory(registry, me)) {
            queue.PushCommand(Core::AlliedVictoryCommand{s.allied_victory, me});
        }
        return true;
    }
    if (Hit(DipBottom(theme, 2), x, y)) {   // Cancel
        Click();
        return true;
    }
    return false;
}

// ---- Objectives ----------------------------------------------------------
//
// The Objectives dialog: a 485x523 panel on dlg_obj.sin's art, its title
// (10910, font 1) across the top, the text in a 385x359 box at (40, 66) in
// font 22 (black, wrapped, with a scroll bar), the Objectives and Hints tabs
// below it on objtabs.slp (interfac.drs 53005, frames 0 and 2) and OK.
//
// The objectives page lists the scenario's objective triggers in their
// order: those on, and those done (struck out, in font 28), each followed by
// an empty line. With none it shows an empty line and the scenario's
// instructions. The hints page is an empty line, string 10911 (which the DLL
// lacks) and the hints; without hints the tabs are not there. A random map's
// page is the victory condition's text for the kind of game.
int VictoryText(const entt::registry& registry) {
    const auto* setup = registry.ctx().find<Core::GameSetup>();
    const bool regicide = setup != nullptr && setup->regicide;
    const bool conquest = setup != nullptr && setup->victory == Core::VictoryKind::Conquest;
    if (regicide) return conquest ? 9849 : 9846;
    return conquest ? 9823 : 9820;
}

constexpr SDL_Rect kObjTitle = {40, 18, 396, 40};
constexpr SDL_Rect kObjText = {40, 66, 385, 359};
constexpr SDL_Rect kObjTabs[2] = {{31, 425, 116, 63}, {148, 425, 116, 63}};
constexpr int kObjTabFrame[2] = {0, 2};
constexpr int kObjTabLabel[2] = {10910, 10912};
constexpr SDL_Rect kObjOk = {270, 445, 145, 30};
constexpr uint32_t kObjTabArt = 53005;   // objtabs.slp
constexpr int kObjScrollW = 20;

SDL_Rect At(const Theme& theme, const SDL_Rect& r) {
    return SDL_Rect{theme.origin.x + r.x, theme.origin.y + r.y, r.w, r.h};
}

SDL_Rect ObjectivesClose(const Theme& theme) { return At(theme, kObjOk); }

struct ObjectiveLine {
    std::string text;
    bool done = false;
};

std::string HintsOf(const entt::registry& registry) {
    const Scenario::File* file = ECS::Triggers::ScenarioOf(registry);
    return file != nullptr ? Scenario::Show(file->texts[Scenario::kHints]) : std::string{};
}

std::vector<ObjectiveLine> ObjectivePage(const entt::registry& registry, int tab) {
    std::vector<ObjectiveLine> page;
    const Scenario::File* file = ECS::Triggers::ScenarioOf(registry);
    if (file == nullptr) {
        page.push_back({" "});
        page.push_back({Get(VictoryText(registry))});
        return page;
    }
    if (tab == 1) {
        page.push_back({" "});
        page.push_back({Get(10911)});
        page.push_back({HintsOf(registry)});
        return page;
    }
    for (const ECS::Triggers::Objective& o : ECS::Triggers::Objectives(registry)) {
        if (!o.on && !o.fired) continue;
        page.push_back({o.text, o.fired && !o.on});
        page.push_back({" "});
    }
    if (page.empty()) {
        page.push_back({" "});
        page.push_back({Scenario::Show(file->texts[Scenario::kInstructions])});
    }
    return page;
}

// The page's lines as the text box wraps them, each with whether it is
// struck out.
std::vector<ObjectiveLine> ObjectiveRows(const entt::registry& registry, int tab) {
    const int body = Text::GameFont(22);
    const int width = PanelWrapWidth(kObjText.w, 0);
    std::vector<ObjectiveLine> rows;
    for (const ObjectiveLine& paragraph : ObjectivePage(registry, tab)) {
        std::vector<std::string_view> lines = PanelLines(body, paragraph.text, width, true);
        if (lines.empty()) lines.push_back(std::string_view{});
        for (std::string_view line : lines) rows.push_back({std::string(line), paragraph.done});
    }
    return rows;
}

int ObjectiveRowsShown() { return kObjText.h / PanelPitch(Text::GameFont(22)); }

void DrawObjectives(SDL_Renderer* r, ECS::Simulation& sim, int w, int h) {
    const Theme& theme = GameDialogTheme(Sin::kObjectives, w, h);
    Shared& s = S();
    const auto& registry = sim.GetRegistry();
    const bool tabs = !HintsOf(registry).empty();
    if (!tabs) s.objectives_tab = 0;
    DrawBackground(r, theme);
    DrawPanelText(r, Text::GameFont(1), At(theme, kObjTitle), Get(kObjTabLabel[s.objectives_tab]),
                  Align::Centre, Down::Centre, false, SinInk(theme));

    const std::vector<ObjectiveLine> rows = ObjectiveRows(registry, s.objectives_tab);
    const int body = Text::GameFont(22);
    const int struck = Text::GameFont(28);
    const SDL_Rect box = At(theme, kObjText);
    const int shown = ObjectiveRowsShown();
    const SDL_Color black{0, 0, 0, 255};
    for (int i = 0; i < shown && s.objectives_scroll + i < static_cast<int>(rows.size()); ++i) {
        const ObjectiveLine& row = rows[static_cast<size_t>(s.objectives_scroll + i)];
        const int x = box.x + 5;
        const int y = box.y + i * PanelPitch(body);
        DrawPanelLine(r, row.done ? struck : body, x, y, row.text, kBlackInk);
        if (row.done && !row.text.empty()) {
            Text::StrikeOut(r, struck, x, y + Text::GdiAscent(struck) - Text::Ascent(struck),
                            Text::Width(struck, row.text), black);
        }
    }
    if (static_cast<int>(rows.size()) > shown) {
        DrawButtonArt(r, theme, 8, box.x + box.w, box.y);
        DrawButtonArt(r, theme, 10, box.x + box.w, box.y + box.h - kObjScrollW);
    }

    if (tabs) {
        const HAL::GraphicAtlas* art = HAL::GraphicCache::Get().GetInterfaceAtlas(kObjTabArt);
        for (int t = 0; t < 2; ++t) {
            const SDL_Rect tab = At(theme, kObjTabs[t]);
            if (art != nullptr && art->base != nullptr &&
                kObjTabFrame[t] < static_cast<int>(art->frames.size())) {
                const HAL::GraphicFrame& f = art->frames[static_cast<size_t>(kObjTabFrame[t])];
                const SDL_Rect dst = {tab.x - f.hotspot_x, tab.y - f.hotspot_y, f.rect.w, f.rect.h};
                SDL_RenderCopy(r, art->base, &f.rect, &dst);
            }
            const bool chosen = t == s.objectives_tab;
            DrawTextIn(r, Text::GameFont(11), tab, Get(kObjTabLabel[t]),
                       chosen ? theme.state : Hit(tab, s.mx, s.my) ? theme.focus : theme.text,
                       theme.text_shadow);
        }
    }
    Button close{ObjectivesClose(theme), Get(4001), Text::GameFont(0)};
    State state = StateOf(close, s.mx, s.my, s.held);
    if (state == State::Normal) state = State::Hover;   // the dialog's focus
    DrawButton(r, theme, close, state);
}

// A click in the Objectives dialog; true when it closes.
bool ObjectivesClick(ECS::Simulation& sim, int x, int y, int w, int h) {
    const Theme& theme = GameDialogTheme(Sin::kObjectives, w, h);
    Shared& s = S();
    if (Hit(ObjectivesClose(theme), x, y)) {
        Click();
        return true;
    }
    if (!HintsOf(sim.GetRegistry()).empty()) {
        for (int t = 0; t < 2; ++t) {
            if (Hit(At(theme, kObjTabs[t]), x, y) && t != s.objectives_tab) {
                Click();
                s.objectives_tab = t;
                s.objectives_scroll = 0;
            }
        }
    }
    const SDL_Rect box = At(theme, kObjText);
    const int most = std::max(0, static_cast<int>(ObjectiveRows(sim.GetRegistry(), s.objectives_tab).size()) -
                                 ObjectiveRowsShown());
    if (Hit(SDL_Rect{box.x + box.w, box.y, kObjScrollW, kObjScrollW}, x, y)) {
        s.objectives_scroll = std::max(0, s.objectives_scroll - 1);
    } else if (Hit(SDL_Rect{box.x + box.w, box.y + box.h - kObjScrollW, kObjScrollW, kObjScrollW}, x, y)) {
        s.objectives_scroll = std::min(most, s.objectives_scroll + 1);
    }
    return false;
}

// ---- Chat ----------------------------------------------------------------
//
// Who hears: a check per player, and buttons that check the allies, the
// enemies or everyone; what to say in a field below; OK sends it as chat
// orders. The art is the Chat dialog's own (Sin 50017).
struct ChatLayout {
    SDL_Rect allies, enemies, everyone, field, ok, cancel;
};
constexpr int kChatRowTop = 110;
constexpr int kChatRowPitch = 30;

ChatLayout ChatRects(const Theme& theme) {
    const int ox = theme.origin.x, oy = theme.origin.y;
    const SDL_Point size = ParchmentOf(theme, Sin::kChat);
    const int bottom = oy + size.y - 62;
    const int third = (size.x - 80 - 20) / 3;
    return ChatLayout{SDL_Rect{ox + 40, oy + 360, third, 30},
                      SDL_Rect{ox + 50 + third, oy + 360, third, 30},
                      SDL_Rect{ox + 60 + 2 * third, oy + 360, third, 30},
                      SDL_Rect{ox + 40, oy + 405, size.x - 80, 26},
                      SDL_Rect{ox + size.x / 2 - 170, bottom, 160, 30},
                      SDL_Rect{ox + size.x / 2 + 10, bottom, 160, 30}};
}

std::vector<Core::PlayerId> Others(const entt::registry& registry) {
    std::vector<Core::PlayerId> out;
    for (int p = 1; p < Core::kMaxPlayers; ++p) {
        const auto* player = ECS::Players::Find(registry, static_cast<Core::PlayerId>(p));
        if (player != nullptr && player->active && p != Core::LocalPlayer()) {
            out.push_back(static_cast<Core::PlayerId>(p));
        }
    }
    return out;
}

SDL_Rect ChatCheck(const Theme& theme, int row) {
    return SDL_Rect{theme.origin.x + 40, theme.origin.y + kChatRowTop + row * kChatRowPitch - 8, 39, 39};
}

void OpenChat() {
    Shared& s = S();
    for (bool& h : s.hears) h = true;
    s.chat.text.clear();
    s.chat.font = Text::kGeorgia10B;
    s.chat.max_length = ECS::Chat::kMostCharacters;
    s.chat.focused = true;
}

void DrawChat(SDL_Renderer* r, ECS::Simulation& sim, int w, int h) {
    const Theme& theme = GameDialogTheme(Sin::kChat, w, h);
    DrawBackground(r, theme);
    DrawTitle(r, theme, Sin::kChat, 9832);
    Shared& s = S();
    const SDL_Color ink{0, 0, 0, 255};
    DrawText(r, Text::kGeorgia10B, theme.origin.x + 40, theme.origin.y + 80, Get(9831), ink, ink,
             Align::Left, false);
    const auto& registry = sim.GetRegistry();
    const auto others = Others(registry);
    for (int row = 0; row < static_cast<int>(others.size()); ++row) {
        const Core::PlayerId p = others[static_cast<size_t>(row)];
        const SDL_Rect box = ChatCheck(theme, row);
        DrawButtonArt(r, theme, s.hears[p] ? 2 : 0, box.x, box.y);
        const HAL::PaletteColor c = HAL::GraphicCache::Get().GetPlayerColor(p);
        DrawText(r, Text::kGeorgia10B, box.x + 50, theme.origin.y + kChatRowTop + row * kChatRowPitch,
                 Rendering::UIRenderer::PlayerName(registry, p), SDL_Color{c.r, c.g, c.b, 255}, ink);
    }
    const ChatLayout l = ChatRects(theme);
    for (const auto& [rect, label] : {std::pair{l.allies, 9833}, std::pair{l.enemies, 9834},
                                      std::pair{l.everyone, 9835}}) {
        Button b{rect, Get(label), Text::kBlackletter14};
        DrawButton(r, theme, b, StateOf(b, s.mx, s.my, s.held));
    }
    s.chat.rect = l.field;
    s.chat.font = Text::kGeorgia10B;
    DrawTextField(r, theme, s.chat);
    Button ok{l.ok, Get(4001), Text::kBlackletter14};
    State state = StateOf(ok, s.mx, s.my, s.held);
    if (state == State::Normal) state = State::Hover;
    DrawButton(r, theme, ok, state);
    Button cancel{l.cancel, Get(4002), Text::kBlackletter14};
    DrawButton(r, theme, cancel, StateOf(cancel, s.mx, s.my, s.held));
}

// Sends what was typed to whoever hears: everyone, the allies, or each
// chosen player in turn.
void SendChat(const entt::registry& registry, const std::string& text, const bool* hears) {
    if (text.empty()) return;
    auto& queue = Core::CommandQueue::Get();
    const Core::PlayerId me = Core::LocalPlayer();
    // In a game that allows cheats, a cheat is done rather than said.
    if (const auto* setup = registry.ctx().find<Core::GameSetup>(); setup != nullptr && setup->cheats) {
        if (const int code = ECS::Cheats::Recognise(text); code != ECS::Cheats::kNone) {
            queue.PushCommand(Core::CheatCommand{code, me});
            return;
        }
    }
    const auto others = Others(registry);
    bool all = true, allies_only = true, any = false;
    for (Core::PlayerId p : others) {
        const bool ally = ECS::Diplomacy::Allied(registry, me, p);
        all = all && hears[p];
        allies_only = allies_only && hears[p] == ally;
        any = any || hears[p];
    }
    if (!any) return;
    if (all) {
        queue.PushCommand(Core::ChatCommand{0, 0, text, me});
    } else if (allies_only) {
        queue.PushCommand(Core::ChatCommand{1, 0, text, me});
    } else {
        for (Core::PlayerId p : others) {
            if (hears[p]) queue.PushCommand(Core::ChatCommand{2, p, text, me});
        }
    }
}

// A click in the Chat dialog; true when it closed it.
bool ChatClick(ECS::Simulation& sim, int x, int y, int w, int h) {
    const Theme& theme = GameDialogTheme(Sin::kChat, w, h);
    const auto& registry = sim.GetRegistry();
    Shared& s = S();
    const auto others = Others(registry);
    for (int row = 0; row < static_cast<int>(others.size()); ++row) {
        if (Hit(ChatCheck(theme, row), x, y)) {
            Click();
            s.hears[others[static_cast<size_t>(row)]] ^= true;
            return false;
        }
    }
    const ChatLayout l = ChatRects(theme);
    const Core::PlayerId me = Core::LocalPlayer();
    if (Hit(l.allies, x, y) || Hit(l.enemies, x, y) || Hit(l.everyone, x, y)) {
        Click();
        const bool allies = Hit(l.allies, x, y), enemies = Hit(l.enemies, x, y);
        for (Core::PlayerId p : others) {
            const bool ally = ECS::Diplomacy::Allied(registry, me, p);
            s.hears[p] = allies ? ally : enemies ? !ally : true;
        }
        return false;
    }
    if (Hit(l.ok, x, y)) {
        Click();
        SendChat(registry, s.chat.text, s.hears);
        return true;
    }
    if (Hit(l.cancel, x, y)) {
        Click();
        return true;
    }
    return false;
}

// The chat line: what Enter opens over the game, sent to everyone -- 111
// rows into the view, at the Options' Text size like the rest of the text
// over the world.
void DrawChatLine(SDL_Renderer* r, int w, int h) {
    const Theme& theme = GameDialogTheme(Sin::kChat, w, h);
    Shared& s = S();
    const int percent = Rendering::UIRenderer::TextPercent();
    auto sized = [percent](int pixels) { return (pixels * percent + 50) / 100; };
    const SDL_Rect view = Rendering::UIRenderer::ViewRect(w, h);
    s.chat.rect = SDL_Rect{sized(4), view.y + sized(111), std::min(w - 8, sized(520)), sized(24)};
    s.chat.font = Text::Scaled(Text::kGeorgia10B, percent);
    DrawTextField(r, theme, s.chat);
}

uint32_t SinOf(Dialog d) {
    switch (d) {
    case Dialog::Options: return Sin::kGameDialog;
    case Dialog::Objectives: return Sin::kObjectives;
    case Dialog::Diplomacy: return Sin::kDiplomacy;
    case Dialog::Chat: return Sin::kChat;
    case Dialog::TechTree: return Sin::kGameDialog;
    default: return Sin::kGameMenu;
    }
}

int TitleOf(Dialog d) {
    switch (d) {
    case Dialog::Options: return 9274;
    case Dialog::Objectives: return 10910;
    case Dialog::Diplomacy: return 9851;
    case Dialog::Chat: return 9832;
    case Dialog::TechTree: return 9799;
    default: return 9446;
    }
}

int g_w = 1024;
int g_h = 768;

} // namespace

void Open(Dialog dialog) {
    S().dialog = dialog;
    if (dialog == Dialog::SaveGame) OpenSave();
    // The diplomacy choices start from the world as it is, read on the
    // dialog's first frame.
    if (dialog == Dialog::Diplomacy) S().diplomacy_fresh = true;
    if (dialog == Dialog::Chat || dialog == Dialog::ChatLine) OpenChat();
    if (dialog == Dialog::Options) OpenOptions(g_w, g_h);
    if (dialog == Dialog::TechTree) TechTree::OpenInGame();
    // The screen opens on its first page.
    if (dialog == Dialog::Achievements) {
        Achievements::State fresh;
        fresh.opening = S().achievements.opening;
        S().achievements = fresh;
    }
    S().objectives_tab = 0;
    S().objectives_scroll = 0;
}

void ShowAchievementsPage(int page) {
    S().achievements.page = std::clamp(page, static_cast<int>(Achievements::kScore),
                                       static_cast<int>(Achievements::kTimeline));
}

void Reset() { S() = Shared{}; }

Dialog Current() { return S().dialog; }

bool WantsText() {
    const Dialog d = S().dialog;
    return d == Dialog::ChatLine || d == Dialog::Chat || d == Dialog::SaveGame;
}

bool Paused() {
    if (S().paused) return true;
    // A single-player game stands still behind its dialogs; the achievements
    // at the end, too. Typing a line of chat does not stop it.
    return S().dialog != Dialog::None && S().dialog != Dialog::ChatLine;
}

void TogglePause() { S().paused = !S().paused; }

void GameOver(bool won, bool single_player) {
    Shared& s = S();
    if (s.over_message >= 0) return;
    s.single_player = single_player;
    s.over_message = won ? Core::Strings::Id::Victorious : Core::Strings::Id::Defeated;
    s.over_at = SDL_GetTicks();
    HAL::Audio::Play(won ? Audio::InterfaceSound::kWon : Audio::InterfaceSound::kLost, 1.0f, 0.0f);
}

bool HandleEvent(const SDL_Event& e, ECS::Simulation& sim) {
    Shared& s = S();
    if (e.type == SDL_MOUSEMOTION) { s.mx = e.motion.x; s.my = e.motion.y; }
    // A button goes down and up where the mouse is.
    if (e.type == SDL_MOUSEBUTTONDOWN || e.type == SDL_MOUSEBUTTONUP) { s.mx = e.button.x; s.my = e.button.y; }
    if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) s.held = true;
    if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) s.held = false;
    if (s.dialog == Dialog::TechTree) {
        if (TechTree::HandleEvent(e, g_w, g_h) == TechTree::Result::Closed) s.dialog = Dialog::None;
        return true;
    }

    if (e.type == SDL_KEYDOWN) {
        const SDL_Keycode key = e.key.keysym.sym;
        namespace HK = Core::Hotkeys;
        if (s.dialog == Dialog::None) {
            // F10 is the menu's own key, not one of the table's.
            if (key == SDLK_F10) { Open(Dialog::Menu); return true; }
            if (HK::Is(e.key, HK::kGameCommands, HK::kPause)) { TogglePause(); return true; }
            if (HK::Is(e.key, HK::kGameCommands, HK::kDiplomacy)) { Open(Dialog::Diplomacy); return true; }
            if (HK::Is(e.key, HK::kGameCommands, HK::kObjectives)) { Open(Dialog::Objectives); return true; }
            if (HK::Is(e.key, HK::kGameCommands, HK::kChatDialog)) { Open(Dialog::Chat); return true; }
            if (HK::Is(e.key, HK::kGameCommands, HK::kTechTree)) { Open(Dialog::TechTree); return true; }
            if (HK::Is(e.key, HK::kGameCommands, HK::kSave)) { Open(Dialog::SaveGame); return true; }
            if (HK::Is(e.key, HK::kGameCommands, HK::kChat)) { Open(Dialog::ChatLine); return true; }
        }
        if ((s.dialog == Dialog::ChatLine || s.dialog == Dialog::Chat) && key == SDLK_RETURN) {
            // Enter sends: the line to everyone, the dialog to whoever is checked.
            bool everyone[Core::kMaxPlayers];
            for (bool& b : everyone) b = true;
            SendChat(sim.GetRegistry(), s.chat.text, s.dialog == Dialog::ChatLine ? everyone : s.hears);
            s.dialog = Dialog::None;
            return true;
        }
        if (s.dialog == Dialog::SaveGame && key == SDLK_RETURN && !s.save_name.text.empty()) {
            Click();
            s.chosen_name = s.save_name.text;
            s.outcome = Outcome::Save;
            s.dialog = Dialog::None;
            return true;
        }
        if (key == SDLK_ESCAPE && s.dialog != Dialog::None && s.dialog != Dialog::Achievements) {
            s.dialog = s.dialog == Dialog::Confirm ? Dialog::Menu : Dialog::None;
            return true;
        }
        // Escape is the Achievements' "Return to Game" while the game goes
        // on; at its end the screen waits for a button.
        if (key == SDLK_ESCAPE && s.dialog == Dialog::Achievements && s.over_message < 0) {
            s.dialog = Dialog::None;
            return true;
        }
    }
    if (s.dialog == Dialog::None) {
        // The end-of-game message gives way, on a click, to whatever comes
        // before the achievements, which the application then opens.
        if (s.over_message >= 0 && !s.ended && e.type == SDL_MOUSEBUTTONUP &&
            SDL_GetTicks() - s.over_at > 1500) {
            s.ended = true;
            s.outcome = Outcome::Ended;
            return true;
        }
        return false;
    }
    const bool up = e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT;
    const int x = up ? e.button.x : 0;
    const int y = up ? e.button.y : 0;
    if (s.dialog == Dialog::ChatLine || s.dialog == Dialog::Chat) {
        if (TypeIntoField(s.chat, e)) return true;
        if (s.dialog == Dialog::ChatLine) return e.type != SDL_MOUSEMOTION;
    }
    if (s.dialog == Dialog::Options) {
        if (OptionsEventIn(e, g_w, g_h)) s.dialog = Dialog::Menu;
        return true;
    }
    if (s.dialog == Dialog::SaveGame) {
        // Typing goes to the name; a saved game picked from the list gives
        // its own, to save over it.
        if (TypeIntoField(s.save_name, e)) return true;
        if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT &&
            ClickListBox(s.saves, e.button.x, e.button.y)) {
            if (s.saves.selected >= 0) s.save_name.text = s.saves.items[s.saves.selected];
            return true;
        }
        const Theme& theme = GameDialogTheme(Sin::kGameDialog, g_w, g_h);
        const SaveLayout l = SaveRects(theme);
        if (up && Hit(l.save, x, y) && !s.save_name.text.empty()) {
            Click();
            s.chosen_name = s.save_name.text;
            s.outcome = Outcome::Save;
            s.dialog = Dialog::None;
        } else if (up && Hit(l.cancel, x, y)) {
            Click();
            s.dialog = Dialog::None;
        }
        return true;
    }
    switch (s.dialog) {
    case Dialog::Diplomacy:
        if (up && !s.diplomacy_fresh && DiplomacyClick(sim, x, y, g_w, g_h)) s.dialog = Dialog::None;
        break;
    case Dialog::Chat:
        if (up && ChatClick(sim, x, y, g_w, g_h)) s.dialog = Dialog::None;
        break;
    case Dialog::Objectives:
        if (up && ObjectivesClick(sim, x, y, g_w, g_h)) s.dialog = Dialog::None;
        if (e.type == SDL_MOUSEWHEEL) {
            const int most = std::max(0, static_cast<int>(ObjectiveRows(sim.GetRegistry(), s.objectives_tab).size()) -
                                         ObjectiveRowsShown());
            s.objectives_scroll = std::clamp(s.objectives_scroll - e.wheel.y, 0, most);
        }
        break;
    case Dialog::Menu: {
        if (!up) break;
        const Theme& theme = MenuTheme(g_w, g_h);
        for (int i = 0; i < static_cast<int>(std::size(kMenuButtons)); ++i) {
            if (!Hit(MenuButtonRect(theme, i), x, y)) continue;
            Click();
            const MenuButton& b = kMenuButtons[i];
            if (b.confirm >= 0) {
                s.question = b.confirm;
                s.on_yes = b.outcome;
                s.dialog = Dialog::Confirm;
            } else if (b.opens != Dialog::None) {
                Open(b.opens);
            } else {
                s.outcome = b.outcome;
                s.dialog = Dialog::None;
            }
            break;
        }
        break;
    }
    case Dialog::Confirm: {
        if (!up) break;
        const Theme& theme = MenuTheme(g_w, g_h);
        if (Hit(ConfirmButton(theme, true), x, y)) {
            Click();
            s.outcome = s.on_yes;
            s.dialog = Dialog::None;
        } else if (Hit(ConfirmButton(theme, false), x, y)) {
            Click();
            s.dialog = Dialog::Menu;
        }
        break;
    }
    case Dialog::Achievements: {
        const bool down = e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT;
        if (!up && !down) break;
        const int page = s.achievements.page;
        const Achievements::Exit exit =
            Achievements::Mouse(s.achievements, g_w, g_h, e.button.x, e.button.y, down);
        if (s.achievements.page != page) Click();
        switch (exit) {
        case Achievements::Exit::None: break;
        case Achievements::Exit::ReturnToGame:
            Click();
            s.dialog = Dialog::None;
            break;
        case Achievements::Exit::MainMenu:
        case Achievements::Exit::CampaignMenu:
            // Both lead to the main menu.
            Click();
            s.outcome = Outcome::QuitToMenu;
            s.dialog = Dialog::None;
            break;
        case Achievements::Exit::PlayAgain:
            Click();
            s.outcome = Outcome::Restart;
            s.dialog = Dialog::None;
            break;
        }
        break;
    }
    default: {
        if (up && Hit(PlaceholderClose(SinOf(s.dialog), g_w, g_h), x, y)) {
            Click();
            s.dialog = Dialog::None;
        }
        break;
    }
    }
    // While a dialog is open nothing behind it hears the mouse or the keys.
    return true;
}

void Draw(SDL_Renderer* r, ECS::Simulation& sim, int w, int h) {
    g_w = w;
    g_h = h;
    Shared& s = S();
    // The message at the end of the game, large, in the middle of the view.
    if (s.over_message >= 0 && s.dialog == Dialog::None) {
        DrawText(r, Text::kBlackletter28B, w / 2, h / 2 - 60, Get(s.over_message),
                 SDL_Color{255, 255, 255, 255}, SDL_Color{0, 0, 0, 255}, Align::Centre);
    }
    // "Game Paused": a 600x60 text panel centred in the map view, in Lucida
    // Blackletter 21 bold, white.
    if (s.paused && s.dialog == Dialog::None) {
        const SDL_Rect view = Rendering::UIRenderer::ViewRect(w, h);
        const SDL_Rect box{view.x + (view.w - 600) / 2, view.y + (view.h - 60) / 2, 600, 60};
        DrawTextIn(r, Text::kBlackletter21B, box, Get(9001), SDL_Color{255, 255, 255, 255},
                   SDL_Color{0, 0, 0, 255});
    }
    switch (s.dialog) {
    case Dialog::None: break;
    case Dialog::Menu: DrawMenu(r, w, h); break;
    case Dialog::Confirm: DrawConfirm(r, w, h); break;
    case Dialog::Achievements: {
        // Why the screen is open decides its buttons: the game going on, or
        // over; a campaign's scenario; a game against the computer.
        Achievements::Opening& opening = s.achievements.opening;
        opening.game_over = s.over_message >= 0;
        const auto* setup = sim.GetRegistry().ctx().find<Core::GameSetup>();
        opening.campaign = setup != nullptr && !setup->scenario.empty() && setup->scenario_entry >= 0;
        opening.single_player = s.single_player;
        Achievements::Draw(r, sim, w, h, s.achievements, s.mx, s.my, s.held);
        break;
    }
    case Dialog::SaveGame: DrawSave(r, w, h); break;
    case Dialog::Objectives: DrawObjectives(r, sim, w, h); break;
    case Dialog::Options: DrawOptionsDialog(r, w, h); break;
    case Dialog::Chat: DrawChat(r, sim, w, h); break;
    case Dialog::ChatLine: DrawChatLine(r, w, h); break;
    case Dialog::TechTree: TechTree::Draw(r, &sim.GetRegistry(), w, h); break;
    case Dialog::Diplomacy:
        if (s.diplomacy_fresh) {
            OpenDiplomacy(sim.GetRegistry());
            s.diplomacy_fresh = false;
        }
        DrawDiplomacy(r, sim, w, h);
        break;
    default: DrawPlaceholder(r, SinOf(s.dialog), TitleOf(s.dialog), w, h); break;
    }
}

std::string SaveName() { return S().chosen_name; }

Outcome TakeOutcome() {
    const Outcome o = S().outcome;
    S().outcome = Outcome::None;
    return o;
}

} // namespace GameDialogs
