#include "UIRenderer.h"

#include "Ground.h"
#include "HudLayout.h"
#include "Renderer.h"
#include "Alerts.h"
#include "Signals.h"
#include "Text.h"
#include "core/ConfigManager.h"
#include "core/GameSetup.h"
#include "core/Hotkeys.h"
#include "core/IsometricMath.h"
#include "core/InputManager.h"
#include "core/Profile.h"
#include "core/Rules.h"
#include "core/Strings.h"
#include "ecs/Diplomacy.h"
#include "ecs/Garrisons.h"
#include "ecs/Market.h"
#include "ecs/components/Formation.h"
#include "ecs/components/Garrison.h"
#include "ecs/components/Building.h"
#include "ecs/components/Doppelganger.h"
#include "ecs/components/Combat.h"
#include "ecs/components/Health.h"
#include "ecs/components/Transform.h"
#include "ecs/components/Identity.h"
#include "ecs/components/MinimapMarker.h"
#include "ecs/components/Monk.h"
#include "ecs/components/Owner.h"
#include "ecs/components/Relic.h"
#include "ecs/components/Resource.h"
#include "ecs/components/Selectable.h"
#include "ecs/components/Worker.h"
#include "ecs/BuildingGeometry.h"
#include "ecs/Buildings.h"
#include "ecs/Players.h"
#include "ecs/Population.h"
#include "ecs/Production.h"
#include "ecs/Selection.h"
#include "ecs/Spies.h"
#include "ecs/Techs.h"
#include "ecs/Score.h"
#include "ecs/Victory.h"
#include "ecs/Vision.h"
#include "ecs/Walls.h"
#include "ecs/Packing.h"
#include "ecs/components/Gate.h"
#include "ecs/components/ProductionQueue.h"
#include "ecs/components/RallyPoint.h"
#include "ecs/VillagerJobs.h"
#include "hal/GraphicCache.h"
#include "hal/Window.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

namespace Rendering {

namespace {

// The game palette's greys for the button bevel: two light lines along the
// top and left, two dark along the bottom and right.
constexpr SDL_Color kBevelLightOuter = {185, 185, 185, 255};   // palette 132
constexpr SDL_Color kBevelLightInner = {255, 255, 255, 255};   // palette 134
constexpr SDL_Color kBevelDarkInner = {47, 47, 47, 255};       // palette 11
constexpr SDL_Color kBevelDarkOuter = {28, 28, 28, 255};       // palette 128

constexpr SDL_Color kBlack = {0, 0, 0, 255};
constexpr SDL_Color kWhite = {255, 255, 255, 255};
// The hit point bars: what is left, and what has been lost (palette 241, 36).
constexpr SDL_Color kHealthGreen = {0, 255, 0, 255};
constexpr SDL_Color kHealthRed = {255, 0, 0, 255};
// The portrait's shadow on the parchment.
constexpr SDL_Color kPortraitShadowOuter = {102, 70, 25, 255};
constexpr SDL_Color kPortraitShadowInner = {111, 86, 53, 255};

// The command icons, in sheet 50721.
constexpr int kIconCancel = 0;
// The Garrison button is the cursor sheet's (51000): a door, an arrow in.
constexpr int kIconGarrison = 14;
constexpr int kIconUngarrison = 2;      // an arrow out of a doorway
constexpr int kIconStop = 3;            // a raised hand
constexpr int kIconPatrol = 6;
constexpr int kIconGuard = 7;
constexpr int kIconFollow = 8;
constexpr int kIconBuildEconomic = 30;
constexpr int kIconBuildMilitary = 31;
constexpr int kIconRepair = 33;
constexpr int kIconTownBell = 49;
constexpr int kIconTreason = 58;        // Treason, a Regicide game's Castle
constexpr int kIconDelete = 59;         // the skull
constexpr int kIconTownBellRung = 61;   // the bell, with an arrow back out
constexpr int kIconBackToWork = 64;     // a man leaving a doorway
// The Market's commodity buttons, selling and buying alike: the goods
// themselves, food 19, wood 18, stone 21.
constexpr int kIconCommodity[3] = {19, 18, 21};   // by dat resource: food, wood, stone
// The trebuchet's and the gate's.
constexpr int kIconPack = 12;           // a wheel: put it on wheels
constexpr int kIconUnpack = 13;         // the engine's frame, set up
constexpr int kIconLockGate = 47;       // the gate, open
constexpr int kIconUnlockGate = 48;     // the gate, its portcullis down
// A Monk's, a transport's and a building's gather point.
constexpr int kIconConvert = 14;        // a Monk at prayer
constexpr int kIconHeal = 15;           // a hand in light
constexpr int kIconUnload = 17;         // a boat, an arrow down to the shore
constexpr int kIconGatherPoint = 45;    // a banner, an arrow down
constexpr int kIconRemoveGatherPoint = 46;   // a banner, an arrow up
// The four stances, aggressive to no attack, and the same four lit green for
// the one in force.
constexpr int kIconStances[4] = {9, 10, 11, 50};
constexpr int kIconStancesLit[4] = {53, 52, 51, 54};
// The four formations in order -- line, box, staggered, flank -- and lit.
constexpr int kIconFormations[4] = {40, 43, 63, 62};
constexpr int kIconFormationsLit[4] = {65, 66, 68, 67};
constexpr Core::Formation kFormationOrder[4] = {Core::Formation::Line, Core::Formation::Box,
                                                Core::Formation::Staggered,
                                                Core::Formation::Flank};
// Disband Formation, and its help.
constexpr int kIconDisband = 39;
constexpr int kDisbandHelp = 41006;

// Where each sits. A villager: the two build menus, repair, delete and
// garrison along the top, stop at the end of the second row. A fighting unit:
// its standing orders, delete and garrison along the top, the stances on the
// second row, the formations on the third.
constexpr int kSlotBuildEconomic = 1;
constexpr int kSlotBuildMilitary = 2;
constexpr int kSlotRepair = 3;
constexpr int kSlotPatrol = 1;
constexpr int kSlotGuard = 2;
constexpr int kSlotFollow = 3;
constexpr int kSlotUnitDelete = 4;
constexpr int kSlotGarrison = 5;
constexpr int kSlotFirstStance = 6;
constexpr int kSlotStop = 10;
constexpr int kSlotFirstFormation = 11;
constexpr int kSlotDisband = 15;
// A build menu puts its way back in the bottom right corner: the buildings
// take their own slots from the dat, and none of them uses that one.
constexpr int kSlotCancel = 15;
// A building's garrison: Back to Work and Ungarrison at the end of the
// second row, the Town Center's bell in the last slot. A building has no
// delete button; the Delete key deletes it.
constexpr int kSlotBackToWork = 9;
constexpr int kSlotUngarrison = 10;
constexpr int kSlotTownBell = 15;
// The buildings whose villagers can be sent back to work: the Town Center,
// the Castle, and the towers by their class.
constexpr int kTownCenter = 109;
constexpr int kCastle = 82;
constexpr int kTowerClass = 52;
// The Market's commodity buttons: "Sell:" in the second row's first slot and
// wood, food and stone after it, "Buy:" and the same along the third. The top
// row is the dat's: the Trade Cart and the Market's four technologies.
constexpr int kSlotSellCaption = 6;
constexpr int kSlotFirstSell = 7;
constexpr int kSlotBuyCaption = 11;
constexpr int kSlotFirstBuy = 12;
constexpr int kSellCaption = 20650;   // "Sell:"
constexpr int kBuyCaption = 20652;    // "Buy:"
constexpr int kTradeOrder[3] = {Core::Rules::Res::Wood, Core::Rules::Res::Food,
                                Core::Rules::Res::Stone};
// Their help, LANGUAGE.DLL's, by dat resource: food, wood, stone.
constexpr int kSellHelp[3] = {41073, 41072, 41074};
constexpr int kBuyHelp[3] = {41077, 41076, 41078};
// A gate's lock, and a trebuchet's Unpack and Pack, both always shown, with
// Attack Ground after.
constexpr int kSlotLockGate = 1;
constexpr int kSlotUnpack = 1;
constexpr int kSlotPack = 2;
// A Monk's Convert first and Heal after it (first when it cannot convert); a
// transport's Unload first; a building's Set Gather Point at the end of the
// top row. While a gather point is being chosen the grid holds only Remove
// Gather Point, first, and Cancel.
constexpr int kSlotConvert = 1;
constexpr int kSlotUnload = 1;
constexpr int kSlotGatherPoint = 5;
constexpr int kSlotRemoveGatherPoint = 1;
// The player attribute (dat resource) that lets a player's Monks convert.
constexpr int kCanConvert = 67;
// The dat task actions of a unit that converts and one that heals.
constexpr int kConvertTask = 104;
constexpr int kHealTask = 105;

// The mouse cursors, in sheet 51000.
constexpr uint32_t kCursorSlp = 51000;
enum CursorFrame : int {
    kCursorNormal = 0,
    kCursorHand = 3,         // a villager's work
    kCursorSword = 4,
    kCursorRing = 5,         // a monk's
    kCursorHammer = 7,
    kCursorShield = 9,
    kCursorHorn = 10,
    kCursorTracks = 11,      // follow
    kCursorDoor = 14,
    kCursorStairsDown = 15,  // unload
    kCursorStairsUp = 16,
    kCursorFlag = 18,        // patrol, a gather point
};

// The top bar's food symbol, drawn over the art's own.
constexpr uint32_t kFoodIconSlp = 53010;

// The little symbols the panel's numbers stand beside, in sheet 50731.
constexpr uint32_t kStatIconSlp = 50731;
constexpr int kStatWood = 0;
constexpr int kStatStone = 1;
constexpr int kStatFood = 2;
constexpr int kStatGold = 3;
constexpr int kStatGarrison = 4;
constexpr int kStatRange = 6;
constexpr int kStatAttack = 7;
constexpr int kStatArmour = 8;
constexpr int kStatProgress = 9;   // a siege engine packing or setting up

// A score line's panel height, and where its text sits in it.
constexpr int kScoreLineH = 20;
constexpr int kScoreInset = 3;
constexpr int kScoreTextY = 3;

// LANGUAGE.DLL: the civilisations' names, and each one's computer players.
constexpr int kCivNameBase = 10230;
constexpr int kComputerNameBase = 4400;
constexpr int kComputerNamesPerCiv = 20;
constexpr int kStringCreating = 4310;
constexpr int kStringResearching = 4309;
constexpr int kStringNeedHouses = 20607;
// Spies' help, its price for <cost>, and Treason's, its price written in.
constexpr int kSpiesHelp = 41113;
constexpr int kTreasonHelp = 41112;
constexpr int kStringAlly = 9853;
constexpr int kStringEnemy = 9855;

// Whether `selected` is a Castle of the player at this machine in a Regicide
// game: where Treason is done.
bool TreasonAt(const entt::registry& registry, entt::entity selected) {
    const auto* setup = registry.ctx().find<Core::GameSetup>();
    const auto* owner = registry.try_get<ECS::Components::Owner>(selected);
    const auto* identity = registry.try_get<ECS::Components::Identity>(selected);
    return setup != nullptr && setup->regicide && owner != nullptr && identity != nullptr &&
           owner->player_id == Core::LocalPlayer() &&
           identity->unit_id == Core::ConfigManager::Get().GetUnitIdByName(setup->regicide_castle);
}

// The button Spies takes in the dat, which Treason takes in its place.
int TreasonButton() {
    static const int button = [] {
        const int count = static_cast<int>(Core::Rules::Get().techs.size());
        for (int id = 0; id < count; ++id) {
            if (ECS::Techs::Spies(id)) return Core::Rules::Tech(id)->button;
        }
        return -1;
    }();
    return button;
}

// The page the command grid shows: the unit's own commands, or a build menu.
constexpr int kPageRoot = 0;   // not an InterfaceKind: the unit's own commands

// The little state the interface itself owns. Kept in one place so both the
// drawing and the clicking see the same thing.
struct Hud {
    int page = kPageRoot;
    entt::entity following = entt::null;
    // The locked formation the selection is in, Line when none: a group sent
    // together marches in it.
    Core::Formation formation = Core::Formation::Line;
    // Advanced commands start shown.
    bool advanced = true;
    bool help = true;
    bool statistics = false;
    bool clock = false;
    int speed_percent = 150;
    int minimap_mode = 0;
    // A line for the lower-left corner from whoever knows what the cursor is
    // over in the world, for this frame only.
    int world_hint = -1;
    // A top bar button clicked, for the screens to open.
    int dialog_request = -1;

    void Follow(entt::entity entity) {
        if (entity == following) return;
        following = entity;
        page = kPageRoot;
    }
};

Hud& HudState() {
    static Hud state;
    return state;
}

// The player whose interface this is -- the same one input gives orders as,
// so the two cannot disagree.
inline Core::PlayerId LocalViewer() { return Core::LocalPlayer(); }

// The local player's civilisation: the interface art and the name the panel
// prints are both per civ.
int LocalCiv(const entt::registry& registry) {
    const ECS::Players::Player* player = ECS::Players::Find(registry, LocalViewer());
    return player != nullptr && player->civ >= 1 ? player->civ : 1;
}

const HudLayout& Lay(int win_w) { return LayoutFor(win_w); }

uint32_t HudArt(const HudLayout& layout, int civ) {
    return layout.art_base + static_cast<uint32_t>(std::max(0, std::min(12, civ - 1)));
}

void Fill(SDL_Renderer* r, int x, int y, int w, int h, SDL_Color c) {
    if (w <= 0 || h <= 0) return;
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
    const SDL_Rect rect = {x, y, w, h};
    SDL_RenderFillRect(r, &rect);
}

// A two-pixel button bevel round a box, lit from the top left;
// pressed, the light and dark swap.
void Bevel(SDL_Renderer* r, int x, int y, int w, int h, bool pressed) {
    const SDL_Color lo = pressed ? kBevelDarkOuter : kBevelLightOuter;
    const SDL_Color li = pressed ? kBevelDarkInner : kBevelLightInner;
    const SDL_Color di = pressed ? kBevelLightInner : kBevelDarkInner;
    const SDL_Color dout = pressed ? kBevelLightOuter : kBevelDarkOuter;
    Fill(r, x, y, w - 1, 1, lo);
    Fill(r, x, y, 1, h - 1, lo);
    Fill(r, x + 1, y + 1, w - 3, 1, li);
    Fill(r, x + 1, y + 1, 1, h - 3, li);
    Fill(r, x + 1, y + h - 2, w - 2, 1, di);
    Fill(r, x + w - 2, y + 1, 1, h - 2, di);
    Fill(r, x, y + h - 1, w, 1, dout);
    Fill(r, x + w - 1, y, 1, h, dout);
}

// Draws one frame of an interface sheet into a box, with the owning player's
// colour on top: unit icons carry player-coloured pixels the same way unit
// sprites do.
void DrawIcon(SDL_Renderer* sdl_r, uint32_t slp_id, int frame,
              const SDL_Rect& dst, int player, uint8_t dim) {
    auto& graphics = HAL::GraphicCache::Get();
    const HAL::GraphicAtlas* atlas = graphics.GetInterfaceAtlas(slp_id);
    if (!atlas || frame < 0 || frame >= static_cast<int>(atlas->frames.size())) return;
    const SDL_Rect& src = atlas->frames[frame].rect;
    if (src.w == 0) return;

    if (atlas->base) {
        SDL_SetTextureColorMod(atlas->base, dim, dim, dim);
        SDL_RenderCopy(sdl_r, atlas->base, &src, &dst);
        SDL_SetTextureColorMod(atlas->base, 255, 255, 255);
    }
    if (SDL_Texture* tint = graphics.GetInterfacePlayerLayer(slp_id, player)) {
        SDL_SetTextureColorMod(tint, dim, dim, dim);
        SDL_RenderCopy(sdl_r, tint, &src, &dst);
        SDL_SetTextureColorMod(tint, 255, 255, 255);
    }
}

// The same at the frame's own size, its top-left at (x, y) less its hotspot.
void DrawFrame(SDL_Renderer* sdl_r, uint32_t slp_id, int frame, int x, int y, int player = 1) {
    auto& graphics = HAL::GraphicCache::Get();
    const HAL::GraphicAtlas* atlas = graphics.GetInterfaceAtlas(slp_id);
    if (!atlas || frame < 0 || frame >= static_cast<int>(atlas->frames.size())) return;
    const HAL::GraphicFrame& f = atlas->frames[frame];
    if (f.rect.w == 0) return;
    DrawIcon(sdl_r, slp_id, frame,
             SDL_Rect{x - f.hotspot_x, y - f.hotspot_y, f.rect.w, f.rect.h}, player, 255);
}

// The icon sheet a unit's icon indexes into: buildings and units have
// separate sheets (ConfigManager::UsesBuildingIcons).
uint32_t IconSheetFor(int unit_id) {
    auto& cfg = Core::ConfigManager::Get();
    return cfg.UsesBuildingIcons(unit_id) ? cfg.BuildingIconSlp() : cfg.UnitIconSlp();
}

// What the player can give orders to -- the command grid and the delete button
// act on this, so it is only ever something the player owns.
entt::entity PrimarySelection(ECS::Simulation& sim) {
    return ECS::Selection::Commanded(sim.GetRegistry(), LocalViewer());
}

// What the panel describes: anything selected, whoever owns it -- a tree, a
// gold mine, a sheep.
entt::entity InspectedSelection(ECS::Simulation& sim) {
    return ECS::Selection::Inspected(sim.GetRegistry(), LocalViewer());
}

// Everything of the player's selected, in the order the cards show it: by
// kind, oldest first within a kind.
void SelectedEntities(entt::registry& registry, std::vector<entt::entity>& out) {
    out.clear();
    auto view = registry.view<ECS::Components::Selectable, ECS::Components::Owner>();
    for (auto e : view) {
        if (!view.get<ECS::Components::Selectable>(e).is_selected) continue;
        if (view.get<ECS::Components::Owner>(e).player_id != LocalViewer()) continue;
        out.push_back(e);
    }
    // EnTT views run newest first.
    std::reverse(out.begin(), out.end());
    std::stable_sort(out.begin(), out.end(), [&](entt::entity a, entt::entity b) {
        const auto* ia = registry.try_get<ECS::Components::Identity>(a);
        const auto* ib = registry.try_get<ECS::Components::Identity>(b);
        return (ia ? ia->unit_id : -1) < (ib ? ib->unit_id : -1);
    });
}

bool CanAfford(const entt::registry& registry, const Core::Rules::Cost* costs, int count) {
    const ECS::Players::Player* player = ECS::Players::Find(registry, LocalViewer());
    return player != nullptr && ECS::Players::CanPay(*player, costs, count);
}

// A price as the help text's four stockpiles, in the engine's resource order.
void StockpileCosts(const Core::Rules::Cost* costs, int count, int out[4]) {
    for (int r = 0; r < 4; ++r) out[r] = 0;
    for (int i = 0; i < count; ++i) {
        if (!costs[i].paid) continue;
        const Core::ResourceType kind = Core::Rules::StockpileOf(costs[i].resource);
        if (kind != Core::ResourceType::COUNT) out[static_cast<int>(kind)] += costs[i].amount;
    }
}

// The numbers the panel prints for a unit, from the player's rules: attack is
// the melee class, or the pierce class for something that only shoots; armour
// is the melee and pierce armour classes.
constexpr int kMeleeClass = 4;
constexpr int kPierceClass = 3;
int DisplayedAttack(const Core::Rules::UnitRules& rules) {
    const int melee = Core::Rules::ClassValue(rules.attacks, rules.attack_count, kMeleeClass, 0);
    if (melee > 0) return melee;
    return Core::Rules::ClassValue(rules.attacks, rules.attack_count, kPierceClass, 0);
}
int MeleeArmour(const Core::Rules::UnitRules& rules) {
    return Core::Rules::ClassValue(rules.armours, rules.armour_count, kMeleeClass, 0);
}
int PierceArmour(const Core::Rules::UnitRules& rules) {
    return Core::Rules::ClassValue(rules.armours, rules.armour_count, kPierceClass, 0);
}

Core::ConfigManager::HelpValues HelpValuesFor(const Core::Rules::UnitRules& rules) {
    Core::ConfigManager::HelpValues values;
    StockpileCosts(rules.costs, rules.cost_count, values.costs);
    values.hp = rules.hp;
    values.attack = DisplayedAttack(rules);
    values.melee_armour = MeleeArmour(rules);
    values.pierce_armour = PierceArmour(rules);
    values.range = rules.max_range.ToInt();
    return values;
}

// A stat as the panel prints it: the unit's own number, and what its
// owner's technologies add to it, as "4+1".
std::string WithBonus(int base, int now) {
    char text[32];
    if (now > base) std::snprintf(text, sizeof(text), "%d+%d", base, now - base);
    else std::snprintf(text, sizeof(text), "%d", now);
    return text;
}

SDL_FRect CommandSlotRect(int slot, int win_w) {
    const HudLayout& layout = Lay(win_w);
    const int col = slot % 5;
    const int row = slot / 5;
    return SDL_FRect{
        static_cast<float>(layout.grid.x + col * layout.pitch_x),
        static_cast<float>(layout.grid.y + row * layout.pitch_y),
        40.0f, 40.0f,
    };
}

bool Inside(const SDL_FRect& r, int x, int y) {
    return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}
bool Inside(const SDL_Rect& r, int x, int y) {
    return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

// The part of the interface being drawn, while one is drawn at a size of its
// own (UIRenderer::DrawInterface): the mouse is in its terms then.
const UIRenderer::HudSpace* g_space = nullptr;

// The mouse, in the game's pixels -- or the part's being drawn -- and whether
// its left button is held.
void Mouse(int& x, int& y, bool& held) {
    if (HAL::Window* window = HAL::MainWindow()) {
        window->MouseState(x, y);
    } else {
        SDL_GetMouseState(&x, &y);
    }
    if (g_space != nullptr) g_space->ToSpace(x, y, x, y);
    held = (SDL_GetMouseState(nullptr, nullptr) & SDL_BUTTON(SDL_BUTTON_LEFT)) != 0;
}

// A part of the interface laid out `percent` big: as big as asked, while its
// own art still fits across the frame -- a 4:3 frame has nothing to spare.
UIRenderer::HudSpace SpaceFor(int win_w, int win_h, int percent, bool bottom) {
    UIRenderer::HudSpace space;
    space.w = win_w;
    space.h = win_h;
    space.bottom = bottom;
    const HudLayout& base = LayoutForHeight(win_h);
    percent = std::clamp(percent, Core::Profile::kInterfaceSizeLeast, Core::Profile::kInterfaceSizeMost);
    const int narrowest = std::max(base.width, (win_w * 100 + percent - 1) / percent);
    if (narrowest >= win_w) return space;
    space.w = narrowest;
    space.scale = static_cast<float>(win_w) / static_cast<float>(narrowest);
    return space;
}

// A player's name: the lobby's, or for a computer player one of its civ's
// leaders from LANGUAGE.DLL, picked by seat and seed so it never changes.
std::string PlayerName(const entt::registry& registry, Core::PlayerId id) {
    const auto* setup = registry.ctx().find<Core::GameSetup>();
    if (setup != nullptr && id < Core::kMaxPlayers && !setup->players[id].name.empty()) {
        return setup->players[id].name;
    }
    const ECS::Players::Player* player = ECS::Players::Find(registry, id);
    const int civ = player != nullptr && player->civ >= 1 ? player->civ : 1;
    const int base = kComputerNameBase + (civ - 1) * kComputerNamesPerCiv;
    const int count = std::atoi(Core::Strings::Get(base).c_str());
    if (count > 0) {
        const uint32_t seed = setup != nullptr ? setup->seed : 0;
        return Core::Strings::Get(base + 1 + static_cast<int>((seed + id * 7u) % count));
    }
    char text[16];
    std::snprintf(text, sizeof(text), "Player %d", id);
    return text;
}

// A line of rich text: the help's <b> switches to the bold face.
struct Run {
    std::string text;
    int font;
};
void LayoutRich(const std::string& text, int base_font, int width,
                std::vector<std::vector<Run>>& lines) {
    lines.clear();
    lines.emplace_back();
    bool bold = false;
    int pen = 0;
    std::string word;
    auto font_now = [&]() { return bold ? Text::BoldOf(base_font) : base_font; };
    auto flush_word = [&]() {
        if (word.empty()) return;
        const int f = font_now();
        if (pen > 0 && pen + Text::Width(f, word) > width) {
            lines.emplace_back();
            pen = 0;
            while (!word.empty() && word.front() == ' ') word.erase(word.begin());
        }
        if (!lines.back().empty() && lines.back().back().font == f) {
            lines.back().back().text += word;
        } else {
            lines.back().push_back({word, f});
        }
        pen += Text::Width(f, word);
        word.clear();
    };
    for (size_t i = 0; i < text.size();) {
        if (text[i] == '<') {
            const size_t close = text.find('>', i);
            if (close != std::string::npos) {
                const std::string tag = text.substr(i + 1, close - i - 1);
                if (tag == "b" || tag == "i") {
                    flush_word();
                    if (tag == "b") bold = !bold;
                    i = close + 1;
                    continue;
                }
            }
        }
        if (text[i] == '\n') {
            flush_word();
            lines.emplace_back();
            pen = 0;
            ++i;
            continue;
        }
        if (text[i] == ' ' && !word.empty() && word.back() != ' ') flush_word();
        word += text[i++];
    }
    flush_word();
    // A line that starts with the space after a break reads as indented.
    for (auto& line : lines) {
        if (!line.empty()) {
            std::string& first = line.front().text;
            while (!first.empty() && first.front() == ' ') first.erase(first.begin());
        }
    }
}

// Which game this is, so pictures of the last one are not kept for the next.
uint32_t g_game = 1;

bool MinimapButtonShown(int button) {
    switch (button) {
    case kIdleVillager:
    case kHelpToggle:
    case kAdvancedCommands:
        return true;
    default:
        return HudState().advanced;
    }
}

SDL_Rect MinimapButtonRect(const HudLayout& layout, int button) {
    const HAL::GraphicAtlas* atlas =
        HAL::GraphicCache::Get().GetInterfaceAtlas(layout.minimap_slp);
    int w = 25, h = 25;
    if (atlas != nullptr) {
        const int frame = kMinimapButtonFrame[button];
        if (frame < static_cast<int>(atlas->frames.size())) {
            w = atlas->frames[frame].rect.w;
            h = atlas->frames[frame].rect.h;
        }
    }
    return SDL_Rect{layout.minimap_button[button].x, layout.minimap_button[button].y, w, h};
}

} // namespace

// The map fills its well as a diamond: tile (0,0) at the top, (w,h) at the
// bottom, spanning the well's full width and height.
SDL_FPoint UIRenderer::MinimapProject(const SDL_FRect& well,
                                      float wx, float wy, int grid_w, int grid_h) {
    const float dx = wx / static_cast<float>(grid_w);
    const float dy = wy / static_cast<float>(grid_h);
    return SDL_FPoint{
        well.x + well.w * 0.5f + (dx - dy) * (well.w * 0.5f),
        well.y + (dx + dy) * (well.h * 0.5f)
    };
}

bool UIRenderer::MinimapUnproject(const SDL_FRect& well, float px, float py,
                                  int grid_w, int grid_h, float& wx, float& wy) {
    if (well.w <= 0.0f || well.h <= 0.0f) return false;
    const float u = (px - (well.x + well.w * 0.5f)) / (well.w * 0.5f);  // dx - dy
    const float v = (py - well.y) / (well.h * 0.5f);                    // dx + dy
    const float dx = (u + v) * 0.5f;
    const float dy = (v - u) * 0.5f;
    if (dx < 0.0f || dx > 1.0f || dy < 0.0f || dy > 1.0f) return false;
    wx = dx * static_cast<float>(grid_w);
    wy = dy * static_cast<float>(grid_h);
    return true;
}

SDL_FRect UIRenderer::MinimapRect(int win_w, int win_h) {
    (void)win_h;
    const SDL_Rect& m = Lay(win_w).minimap;
    return SDL_FRect{static_cast<float>(m.x), static_cast<float>(m.y),
                     static_cast<float>(m.w), static_cast<float>(m.h)};
}

bool UIRenderer::PointOverHud(int win_w, int win_h, int x, int y) {
    if (x < 0 || y < 0 || x >= win_w || y >= win_h) return false;
    const HudLayout& layout = Lay(win_w);
    // The art's own pixels decide: the world shows through wherever the frame
    // is transparent, and is clickable there too.
    const HAL::GraphicAtlas* atlas =
        HAL::GraphicCache::Get().GetInterfaceAtlas(layout.art_base);
    // In a frame wider than the art, the point's pixel of the art: the part
    // left of the cut is at the left edge, the part right of it at the right
    // edge, and between them a plain stretch of the bar or the panel.
    const HudLayout& base = LayoutForHeight(win_h);
    const int cut = y < HudSplit(base) ? BarCut(base) : PanelCut(base);
    const int dx = std::max(0, win_w - base.width);
    const bool stretch = x >= cut && x < cut + dx;
    const int art_x = x < cut ? x : x - dx;
    if (stretch) {
        if (y < layout.top_bar_h || y >= layout.panel_top) return true;
    } else if (atlas != nullptr && !atlas->opaque.empty()) {
        if (atlas->Covers(0, art_x, y)) return true;
    } else if (y < layout.top_bar_h || y >= layout.panel_top) {
        return true;
    }
    // The buttons drawn over the world count too.
    return BarButtonAt(win_w, x, y) >= 0 || MinimapButtonAt(win_w, x, y) >= 0;
}

int UIRenderer::BarButtonAt(int win_w, int x, int y) {
    const HudLayout& layout = Lay(win_w);
    for (int b = 0; b < kBarButtonCount; ++b) {
        const SDL_Rect r = {layout.bar_button[b].x, layout.bar_button[b].y, 50, 17};
        if (Inside(r, x, y)) return b;
    }
    return -1;
}

int UIRenderer::MinimapButtonAt(int win_w, int x, int y) {
    const HudLayout& layout = Lay(win_w);
    for (int b = 0; b < kMinimapButtonCount; ++b) {
        if (!MinimapButtonShown(b)) continue;
        if (Inside(MinimapButtonRect(layout, b), x, y)) return b;
    }
    return -1;
}

bool UIRenderer::ActivateMinimapButton(int button) {
    Hud& hud = HudState();
    switch (button) {
    case kAdvancedCommands: hud.advanced = !hud.advanced; break;
    case kHelpToggle: hud.help = !hud.help; break;
    case kStatistics: hud.statistics = !hud.statistics; break;
    case kNormalMode: hud.minimap_mode = 0; break;
    case kCombatMode: hud.minimap_mode = 1; break;
    case kEconomicMode: hud.minimap_mode = 2; break;
    case kIdleVillager: return true;
    default: break;
    }
    return false;
}

bool UIRenderer::AdvancedCommands() { return HudState().advanced; }
bool UIRenderer::HelpShown() { return HudState().help; }
int UIRenderer::MinimapMode() { return HudState().minimap_mode; }
void UIRenderer::SetWorldHint(int string_id) { HudState().world_hint = string_id; }
void UIRenderer::ToggleClock() { HudState().clock = !HudState().clock; }
// The world between the bar and the panel, in the frame, at their sizes.
SDL_Rect UIRenderer::ViewRect(int win_w, int win_h) {
    const HudSpace top = TopSpace(win_w, win_h);
    const HudSpace panel = PanelSpace(win_w, win_h);
    const int y = static_cast<int>(std::lround(Lay(top.w).top_bar_h * top.scale));
    const int bottom = static_cast<int>(std::lround(panel.FrameY(static_cast<float>(Lay(panel.w).panel_top))));
    return SDL_Rect{0, y, win_w, bottom - y};
}

void UIRenderer::HudSpace::ToSpace(int fx, int fy, int& x, int& y) const {
    if (scale == 1.0f) {
        x = fx;
        y = fy;
        return;
    }
    x = static_cast<int>(std::floor(fx / scale));
    y = bottom ? h - static_cast<int>(std::ceil((h - fy) / scale))
               : static_cast<int>(std::floor(fy / scale));
}

UIRenderer::HudSpace UIRenderer::TopSpace(int win_w, int win_h) {
    return SpaceFor(win_w, win_h, Core::Profile::Current().top_bar_size, false);
}

UIRenderer::HudSpace UIRenderer::PanelSpace(int win_w, int win_h) {
    return SpaceFor(win_w, win_h, Core::Profile::Current().panel_size, true);
}

int UIRenderer::TextPercent() {
    return std::clamp(Core::Profile::Current().text_size, Core::Profile::kInterfaceSizeLeast,
                      Core::Profile::kInterfaceSizeMost);
}

bool UIRenderer::OverInterface(int win_w, int win_h, int x, int y) {
    if (x < 0 || y < 0 || x >= win_w || y >= win_h) return false;
    const int split = HudSplit(LayoutForHeight(win_h));
    const HudSpace top = TopSpace(win_w, win_h);
    int px = 0, py = 0;
    top.ToSpace(x, y, px, py);
    if (py < split && PointOverHud(top.w, win_h, px, py)) return true;
    const HudSpace panel = PanelSpace(win_w, win_h);
    panel.ToSpace(x, y, px, py);
    return py >= split && PointOverHud(panel.w, win_h, px, py);
}
void UIRenderer::SetGameSpeed(int percent) { HudState().speed_percent = percent; }

namespace {

// The game's font 11 -- Georgia 9 bold (Text::GameFont) -- in which the time
// panels and the scores are set.
const int kPanelFont = Text::GameFont(11);

// A time panel's text: over its black shadow one pixel down and right.
void TimeText(SDL_Renderer* sdl_r, int font, int x, int y, const char* text, SDL_Color colour) {
    Text::Draw(sdl_r, font, x + 1, y + 1, text, kBlack);
    Text::Draw(sdl_r, font, x, y, text, colour);
}

// A distance in the text over the world, scaled to the Text size.
int TextScaled(int pixels) { return (pixels * UIRenderer::TextPercent() + 50) / 100; }

} // namespace

void UIRenderer::DrawTimePanels(ECS::Simulation& sim, SDL_Renderer* sdl_r, int win_w, int win_h) {
    const Hud& hud = HudState();
    const int view_top = ViewRect(win_w, win_h).y;
    const int font = Text::Scaled(kPanelFont, TextPercent());
    if (hud.clock) {
        // "%02ld:%02ld:%02ld (%3.1f)": the time of the game and its speed as
        // a multiple.
        const uint64_t seconds = sim.Tick() * 16 / 1000;
        char text[64];
        std::snprintf(text, sizeof(text), "%02ld:%02ld:%02ld (%3.1f)",
                      static_cast<long>(seconds / 3600), static_cast<long>(seconds / 60 % 60),
                      static_cast<long>(seconds % 60), hud.speed_percent / 100.0);
        TimeText(sdl_r, font, TextScaled(4), view_top + TextScaled(4), text, kWhite);
    }
    // The countdowns: a Wonder standing, the relics held. A year is three
    // seconds of game time.
    const ECS::Victory::Outcome* outcome = ECS::Victory::Read(sim.GetRegistry());
    if (outcome == nullptr) return;
    struct Count { Core::PlayerId holder; int32_t ms; int line; };
    const Count counts[2] = {{outcome->wonder_holder, outcome->wonder_ms, 11301},
                             {outcome->relic_holder, outcome->relic_ms, 11302}};
    int row = 0;
    for (const Count& c : counts) {
        if (c.holder == 0 || outcome->over) continue;
        const int years = (std::max(0, c.ms) + 2999) / 3000;
        char text[96];
        std::snprintf(text, sizeof(text), Core::Strings::Get(c.line).c_str(), years);
        const int right = win_w - TextScaled(4);
        const int width = Text::Width(font, text);
        TimeText(sdl_r, font, right - width, view_top + TextScaled(24 + row * 20), text,
                 UIRenderer::PlayerTextColour(c.holder));
        ++row;
    }
}
SDL_Color UIRenderer::PlayerTextColour(Core::PlayerId player) {
    // Blue, red, green, yellow, cyan, magenta, light grey, orange -- each
    // drawn over black.
    static constexpr SDL_Color kText[8] = {
        {0, 0, 255, 255},   {255, 0, 0, 255},     {0, 255, 0, 255},     {255, 255, 0, 255},
        {0, 255, 255, 255}, {255, 0, 255, 255},   {223, 223, 223, 255}, {255, 130, 1, 255},
    };
    const int slot = HAL::GraphicCache::Get().ColourSlot(player);
    return slot >= 0 && slot < 8 ? kText[slot] : kWhite;
}

void UIRenderer::DrawScores(ECS::Simulation& sim, SDL_Renderer* sdl_r, int win_w, int win_h) {
    if (!HudState().statistics) return;
    const auto& registry = sim.GetRegistry();
    const ECS::Victory::Outcome* outcome = ECS::Victory::Read(registry);
    const auto* setup = registry.ctx().find<Core::GameSetup>();
    if (setup == nullptr) return;
    // One line a player, "name: score/team": a team's members by their team's
    // average, the best team first, and within it the best score; [W] and [R]
    // before the holders of a Wonder and of the relics.
    struct Line {
        Core::PlayerId player;
        int32_t score;
        int32_t team;
        int group;
    };
    Line lines[Core::kMaxPlayers];
    int count = 0;
    for (int p = 1; p < Core::kMaxPlayers; ++p) {
        if (!setup->players[p].active) continue;
        const ECS::Score::Sheet* sheet = ECS::Score::Of(registry, static_cast<Core::PlayerId>(p));
        if (sheet == nullptr) continue;
        int group = p;   // the lowest seat it wins with
        for (int q = 1; q < p; ++q) {
            if (setup->players[q].active &&
                ECS::Diplomacy::SharesVictory(registry, static_cast<Core::PlayerId>(p), static_cast<Core::PlayerId>(q))) {
                group = q;
                break;
            }
        }
        lines[count++] = {static_cast<Core::PlayerId>(p), sheet->total,
                          ECS::Score::TeamAverage(registry, static_cast<Core::PlayerId>(p)), group};
    }
    std::sort(lines, lines + count, [](const Line& a, const Line& b) {
        if (a.team != b.team) return a.team > b.team;
        if (a.group != b.group) return a.group > b.group;
        if (a.score != b.score) return a.score > b.score;
        return a.player > b.player;
    });
    // Fifteen 251x20 panels stacked up from the view's lower right corner,
    // four pixels in; the list ends on the lowest.
    const SDL_Rect view = ViewRect(win_w, win_h);
    const int right = view.x + view.w - TextScaled(4);
    const int bottom = view.y + view.h - TextScaled(4);
    const int line_h = TextScaled(kScoreLineH);
    const int top = bottom - count * line_h;
    const int font = Text::Scaled(kPanelFont, TextPercent());
    const SDL_Color outline = kBlack;
    for (int i = 0; i < count; ++i) {
        const Line& line = lines[i];
        std::string text;
        if (outcome != nullptr && outcome->wonder_holder == line.player) text += Core::Strings::Get(3106);
        if (outcome != nullptr && outcome->relic_holder == line.player) text += Core::Strings::Get(3107);
        if (!text.empty()) text += ' ';
        char numbers[48];
        std::snprintf(numbers, sizeof(numbers), ": %d/%d", line.score, line.team);
        text += PlayerName(registry, line.player) + numbers;
        // A player out of the game is struck through: the game's font 8, the
        // same Georgia with its strike-out.
        const bool out = outcome != nullptr && outcome->Defeated(line.player);
        const int width = Text::Width(font, text);
        const int x = right - TextScaled(kScoreInset) - width;
        const int y = top + i * line_h + TextScaled(kScoreTextY);
        const SDL_Color colour = PlayerTextColour(line.player);
        Text::DrawOutlined(sdl_r, font, x, y, text, colour, outline);
        if (out) Text::StrikeOut(sdl_r, font, x, y, width, colour);
    }
}

void UIRenderer::NewGame() {
    ++g_game;
    Hud& hud = HudState();
    hud.page = kPageRoot;
    hud.following = entt::null;
    hud.world_hint = -1;
    hud.dialog_request = -1;
}
void UIRenderer::RequestDialog(int bar_button) { HudState().dialog_request = bar_button; }
int UIRenderer::TakeDialogRequest() {
    const int request = HudState().dialog_request;
    HudState().dialog_request = -1;
    return request;
}

void UIRenderer::DrawMinimap(ECS::Simulation& sim, Renderer& renderer,
                             SDL_Renderer* sdl_r, int win_w, int win_h) {
    // The view's outline is the whole frame's, whatever the panel's size.
    const Frame& frame = CurrentFrame();
    DrawMinimapIn(sim, renderer, sdl_r, MinimapRect(win_w, win_h),
                  SDL_FRect{0.0f, 0.0f, static_cast<float>(frame.w > 0 ? frame.w : win_w),
                            static_cast<float>(frame.h > 0 ? frame.h : win_h)});

    // The buttons round it, over everything: lit while pressed, and the mode
    // in force lit for good.
    const HudLayout& layout = Lay(win_w);
    int mx = 0, my = 0;
    bool held = false;
    Mouse(mx, my, held);
    const Hud& hud = HudState();
    for (int b = 0; b < kMinimapButtonCount; ++b) {
        if (!MinimapButtonShown(b)) continue;
        const SDL_Rect r = MinimapButtonRect(layout, b);
        bool lit = held && Inside(r, mx, my);
        if (b == kNormalMode) lit = lit || hud.minimap_mode == 0;
        if (b == kCombatMode) lit = lit || hud.minimap_mode == 1;
        if (b == kEconomicMode) lit = lit || hud.minimap_mode == 2;
        DrawFrame(sdl_r, layout.minimap_slp, kMinimapButtonFrame[b] + (lit ? 1 : 0), r.x, r.y);
    }
}

void UIRenderer::DrawMinimapIn(ECS::Simulation& sim, Renderer& renderer, SDL_Renderer* sdl_r,
                               const SDL_FRect& well, const SDL_FRect& view) {
    auto& grid = sim.GetGrid();
    auto& graphics = HAL::GraphicCache::Get();
    const int tex_w = static_cast<int>(well.w);
    const int tex_h = static_cast<int>(well.h);
    if (tex_w <= 0 || tex_h <= 0) return;

    // Rasterised once at the well's own size, so every map pixel is a screen
    // pixel and nothing is resampled; again when the ground changes, or a
    // new game starts.
    static SDL_Texture* minimap_tex = nullptr;
    static int cached_w = 0;
    static int cached_h = 0;
    static uint32_t cached_revision = 0;
    static uint32_t cached_game = 0;
    const uint32_t revision = grid.TerrainRevision();
    if (!minimap_tex || cached_w != tex_w || cached_h != tex_h || cached_revision != revision ||
        cached_game != g_game) {
        cached_game = g_game;
        if (minimap_tex && (cached_w != tex_w || cached_h != tex_h)) {
            SDL_DestroyTexture(minimap_tex);
            minimap_tex = nullptr;
        }
        if (!minimap_tex) {
            minimap_tex = SDL_CreateTexture(sdl_r, SDL_PIXELFORMAT_RGBA8888,
                                            SDL_TEXTUREACCESS_TARGET, tex_w, tex_h);
        }
        cached_w = tex_w;
        cached_h = tex_h;
        cached_revision = revision;
        if (!minimap_tex) return;
        SDL_SetTextureBlendMode(minimap_tex, SDL_BLENDMODE_BLEND);

        SDL_Texture* prev = SDL_GetRenderTarget(sdl_r);
        SDL_SetRenderTarget(sdl_r, minimap_tex);
        SDL_SetRenderDrawColor(sdl_r, 0, 0, 0, 0);
        SDL_RenderClear(sdl_r);

        // Project into a well-sized target sitting at the origin.
        const SDL_FRect local = {0.0f, 0.0f, well.w, well.h};
        // One sample per output pixel column of the diamond, so no tile is
        // missed and none is drawn twice.
        const int step = std::max(1, grid.GetWidth() / std::max(1, tex_w / 2));
        const float cell_w =
            std::max(1.0f, well.w * step / static_cast<float>(grid.GetWidth()));
        const float cell_h =
            std::max(1.0f, well.h * step / static_cast<float>(grid.GetHeight()));

        for (int y = 0; y < grid.GetHeight(); y += step) {
            for (int x = 0; x < grid.GetWidth(); x += step) {
                const auto& tile = grid.GetTile(x, y);
                // Shaded by slope: the dat gives each terrain a bright, a
                // flat and a dark minimap colour.
                const auto& art = Core::ConfigManager::Get().GetTerrainArt(tile.terrain);
                const float light = Ground::Light(grid, x, y);
                const int shade = light > 0.25f ? 0 : light < -0.25f ? 2 : 1;
                const HAL::PaletteColor colour =
                    graphics.GetPaletteColor(static_cast<size_t>(art.minimap_shades[shade]));
                SDL_SetRenderDrawColor(sdl_r, colour.r, colour.g, colour.b, 255);
                const SDL_FPoint p = MinimapProject(local, static_cast<float>(x),
                                                    static_cast<float>(y),
                                                    grid.GetWidth(), grid.GetHeight());
                SDL_FRect cell = {p.x - cell_w * 0.5f, p.y, cell_w, cell_h};
                SDL_RenderFillRectF(sdl_r, &cell);
            }
        }
        SDL_SetRenderTarget(sdl_r, prev);
    }

    SDL_RenderCopyF(sdl_r, minimap_tex, nullptr, &well);

    auto& registry = sim.GetRegistry();

    // The fog over it, for this machine's player: black where it has never
    // been, dimmed where it cannot see now. Redrawn every 250 ms.
    {
        static SDL_Texture* fog_tex = nullptr;
        static int fog_w = 0;
        static int fog_h = 0;
        static uint32_t fog_drawn = 0;
        if (!fog_tex || fog_w != tex_w || fog_h != tex_h) {
            if (fog_tex) SDL_DestroyTexture(fog_tex);
            fog_tex = SDL_CreateTexture(sdl_r, SDL_PIXELFORMAT_RGBA8888,
                                        SDL_TEXTUREACCESS_TARGET, tex_w, tex_h);
            if (fog_tex) SDL_SetTextureBlendMode(fog_tex, SDL_BLENDMODE_BLEND);
            fog_w = tex_w;
            fog_h = tex_h;
            fog_drawn = 0;
        }
        const uint32_t now = SDL_GetTicks();
        static uint32_t fog_game = 0;
        if (fog_game != g_game) { fog_game = g_game; fog_drawn = 0; }
        if (fog_tex && (fog_drawn == 0 || now - fog_drawn > 250)) {
            fog_drawn = now == 0 ? 1 : now;
            SDL_Texture* prev = SDL_GetRenderTarget(sdl_r);
            SDL_SetRenderTarget(sdl_r, fog_tex);
            SDL_SetRenderDrawColor(sdl_r, 0, 0, 0, 0);
            SDL_RenderClear(sdl_r);
            const SDL_FRect local = {0.0f, 0.0f, well.w, well.h};
            const int step = std::max(1, grid.GetWidth() / std::max(1, tex_w / 2));
            const float cell_w =
                std::max(1.0f, well.w * step / static_cast<float>(grid.GetWidth()));
            const float cell_h =
                std::max(1.0f, well.h * step / static_cast<float>(grid.GetHeight()));
            for (int y = 0; y < grid.GetHeight(); y += step) {
                for (int x = 0; x < grid.GetWidth(); x += step) {
                    if (ECS::Vision::Visible(registry, LocalViewer(), x, y)) continue;
                    const bool seen = ECS::Vision::Explored(registry, LocalViewer(), x, y);
                    SDL_SetRenderDrawColor(sdl_r, 0, 0, 0, seen ? 110 : 255);
                    const SDL_FPoint p = MinimapProject(local, static_cast<float>(x),
                                                        static_cast<float>(y),
                                                        grid.GetWidth(), grid.GetHeight());
                    SDL_FRect cell = {p.x - cell_w * 0.5f, p.y, cell_w, cell_h};
                    SDL_RenderFillRectF(sdl_r, &cell);
                }
            }
            SDL_SetRenderTarget(sdl_r, prev);
        }
        if (fog_tex) SDL_RenderCopyF(sdl_r, fog_tex, nullptr, &well);
    }
    auto dot_at = [&](const ECS::Components::Transform& transform, int size) {
        const SDL_FPoint p = MinimapProject(well,
                                            transform.position.x.ToFloat(),
                                            transform.position.y.ToFloat(),
                                            grid.GetWidth(), grid.GetHeight());
        const SDL_Rect dot = {static_cast<int>(std::floor(p.x)) - size / 2,
                              static_cast<int>(std::floor(p.y)) - size / 2, size, size};
        SDL_RenderFillRect(sdl_r, &dot);
    };

    // A unit a dot, a building a square its footprint's size.
    auto dot_size = [&](entt::entity entity) {
        if (!registry.all_of<ECS::Components::Building>(entity)) return 2;
        const auto* identity = registry.try_get<ECS::Components::Identity>(entity);
        const auto* info = identity != nullptr
            ? Core::ConfigManager::Get().GetUnitInterface(identity->unit_id) : nullptr;
        const int footprint = info != nullptr && info->footprint > 0 ? info->footprint : 1;
        return std::max(3, footprint * 2);
    };

    // The mini-map's mode decides what it shows: everything, only the
    // fighting, or only the economy's buildings and traders.
    const int mode = HudState().minimap_mode;

    // The map's own contents first, so a unit standing on a gold mine is the
    // one you see. Each carries the colour the unit table gives it -- gold a
    // dull brown, stone grey, game and forage the same yellow-green -- and
    // anything left off the minimap has no marker at all.
    if (mode != 1) {
        auto markers = registry.view<ECS::Components::Transform,
                                     ECS::Components::MinimapMarker>();
        for (auto entity : markers) {
            if (!ECS::Vision::Shows(registry, LocalViewer(), entity)) continue;
            const HAL::PaletteColor colour = graphics.GetPaletteColor(
                markers.get<ECS::Components::MinimapMarker>(entity).palette_index);
            SDL_SetRenderDrawColor(sdl_r, colour.r, colour.g, colour.b, 255);
            dot_at(markers.get<ECS::Components::Transform>(entity), 2);
        }
    }

    // The notices -- an attack, a flare, a unit trained -- go over the map's
    // own objects and under the players'.
    Alerts::Draw(sdl_r, well, grid.GetWidth(), grid.GetHeight());

    // Then units and buildings, in their owner's colour: a unit a dot, a
    // building a square its footprint's size.
    auto owned = registry.view<ECS::Components::Transform, ECS::Components::Owner>();
    for (auto entity : owned) {
        const int player = owned.get<ECS::Components::Owner>(entity).player_id;
        if (player == Core::PLAYER_GAIA) continue;
        if (!ECS::Vision::Shows(registry, LocalViewer(), entity)) continue;
        const bool building = registry.all_of<ECS::Components::Building>(entity);
        const bool worker = registry.all_of<ECS::Components::Worker>(entity);
        if (mode == 1 && (building || worker)) continue;
        if (mode == 2 && !building && !worker) continue;

        // The pure player colour, not the mid-ramp shade a unit's body is
        // painted with.
        const HAL::PaletteColor colour = graphics.GetPaletteColor(
            static_cast<size_t>(Core::ConfigManager::Get().PlayerMinimapColour(player)));
        SDL_SetRenderDrawColor(sdl_r, colour.r, colour.g, colour.b, 255);
        dot_at(owned.get<ECS::Components::Transform>(entity), dot_size(entity));
    }
    // The buildings the viewer keeps pictures of in the fog, in the colour of
    // whose they were.
    if (mode != 1) {
        auto pictures = registry.view<ECS::Components::Transform, ECS::Components::Doppelganger>();
        for (auto entity : pictures) {
            const auto& picture = pictures.get<ECS::Components::Doppelganger>(entity);
            if (!picture.main || picture.player != LocalViewer() || picture.colour == Core::PLAYER_GAIA) continue;
            const HAL::PaletteColor colour = graphics.GetPaletteColor(
                static_cast<size_t>(Core::ConfigManager::Get().PlayerMinimapColour(picture.colour)));
            const auto* info = Core::ConfigManager::Get().GetUnitInterface(picture.unit_id);
            const int footprint = info != nullptr && info->footprint > 0 ? info->footprint : 1;
            SDL_SetRenderDrawColor(sdl_r, colour.r, colour.g, colour.b, 255);
            dot_at(ECS::Components::Transform{picture.origin, picture.origin}, std::max(3, footprint * 2));
        }
    }

    // What is selected, drawn again in white over everything else, and a
    // selected Town Center's, Dock's or barracks' gather point as a small
    // white X (palette 255, two pixels each way; civilian and military
    // buildings are interface kinds 2 and 10).
    {
        const HAL::PaletteColor white = graphics.GetPaletteColor(0xff);
        SDL_SetRenderDrawColor(sdl_r, white.r, white.g, white.b, 255);
        auto selected = registry.view<ECS::Components::Transform, ECS::Components::Selectable>();
        for (auto entity : selected) {
            if (!selected.get<ECS::Components::Selectable>(entity).is_selected) continue;
            dot_at(selected.get<ECS::Components::Transform>(entity), dot_size(entity));
            const auto* rally = registry.try_get<ECS::Components::RallyPoint>(entity);
            const auto* identity = registry.try_get<ECS::Components::Identity>(entity);
            if (rally == nullptr || identity == nullptr) continue;
            const auto* info = Core::ConfigManager::Get().GetUnitInterface(identity->unit_id);
            if (info == nullptr || (info->interface_kind != 2 && info->interface_kind != 10)) continue;
            const SDL_FPoint p = MinimapProject(well, rally->at.x.ToFloat(), rally->at.y.ToFloat(),
                                                grid.GetWidth(), grid.GetHeight());
            const int x = static_cast<int>(std::floor(p.x));
            const int y = static_cast<int>(std::floor(p.y));
            SDL_RenderDrawLine(sdl_r, x - 2, y - 2, x + 2, y + 2);
            SDL_RenderDrawLine(sdl_r, x - 2, y + 2, x + 2, y - 2);
        }
    }

    // Flares the player is shown: a flashing X.
    Signals::DrawOnMinimap(sim, sdl_r, well, LocalViewer());

    // Viewport outline, projected through the same function as the map itself.
    const float inv_z = 1.0f / renderer.camera_zoom;
    const float left = renderer.camera_x + view.x * inv_z;
    const float top = renderer.camera_y + view.y * inv_z;
    const float right = left + view.w * inv_z;
    const float bottom = top + view.h * inv_z;
    const auto tl = Core::IsometricMath::ScreenToWorld(left, top);
    const auto tr = Core::IsometricMath::ScreenToWorld(right, top);
    const auto br = Core::IsometricMath::ScreenToWorld(right, bottom);
    const auto bl = Core::IsometricMath::ScreenToWorld(left, bottom);

    const SDL_FPoint corners[4] = {
        MinimapProject(well, tl.first, tl.second, grid.GetWidth(), grid.GetHeight()),
        MinimapProject(well, tr.first, tr.second, grid.GetWidth(), grid.GetHeight()),
        MinimapProject(well, br.first, br.second, grid.GetWidth(), grid.GetHeight()),
        MinimapProject(well, bl.first, bl.second, grid.GetWidth(), grid.GetHeight()),
    };
    SDL_SetRenderDrawColor(sdl_r, 255, 255, 255, 255);
    for (int i = 0; i < 4; ++i) {
        SDL_RenderDrawLineF(sdl_r, corners[i].x, corners[i].y,
                            corners[(i + 1) % 4].x, corners[(i + 1) % 4].y);
    }
}

namespace {

// The hotkey group a building's own buttons answer to: the Town Center's, the
// Dock's and each military building's keys are filed apart, so S is the
// Militia at a Barracks and the Scout at a Stable.
int HotkeyGroupOfBuilding(int unit_id) {
    const auto* ui = Core::ConfigManager::Get().GetUnitInterface(unit_id);
    if (ui == nullptr) return -1;
    static const struct { const char* name; int group; } kGroups[] = {
        {"Town Center", Core::Hotkeys::kTownCenter},
        {"Dock", Core::Hotkeys::kDock},
        {"Barracks", Core::Hotkeys::kBarracks},
        {"Archery Range", Core::Hotkeys::kArcheryRange},
        {"Stable", Core::Hotkeys::kStable},
        {"Siege Workshop", Core::Hotkeys::kSiegeWorkshop},
        {"Monastery", Core::Hotkeys::kMonastery},
        {"Market", Core::Hotkeys::kMarket},
        {"Castle", Core::Hotkeys::kCastle},
    };
    for (const auto& g : kGroups) {
        if (ui->name == g.name) return g.group;
    }
    return -1;
}

// The item of `group` that trains or places `unit_id`. The hotkey list names
// an item by its unit line -- "Militia, Man-at-Arms, etc.
// (swordsmen)", "Build Trebuchet" -- so a unit answers to the item whose
// names include its own; failing an exact name, one that a word shorter or
// longer names it ("Tower" the Watch Tower, "Transport" the Transport Ship).
// The Castle's other button is its unique unit. Remembered per unit: the grid
// is rebuilt every frame.
int HotkeyItemFor(int group, int unit_id) {
    static std::unordered_map<int, int> known[Core::Hotkeys::kGroupCount];
    if (group < 0 || group >= Core::Hotkeys::kGroupCount) return -1;
    if (auto it = known[group].find(unit_id); it != known[group].end()) return it->second;
    const auto* ui = Core::ConfigManager::Get().GetUnitInterface(unit_id);
    const std::string unit_name = ui ? ui->name : std::string{};
    int items[64];
    const int count = Core::Hotkeys::Items(group, items, 64);
    auto names_of = [](int string_id) {
        std::vector<std::string> names;
        std::string text = Core::Strings::Get(string_id);
        if (const size_t paren = text.find(" ("); paren != std::string::npos) text.resize(paren);
        size_t start = 0;
        while (start <= text.size()) {
            size_t comma = text.find(", ", start);
            std::string part = text.substr(start, comma == std::string::npos ? std::string::npos
                                                                             : comma - start);
            for (const char* prefix : {"Build ", "Create "}) {
                if (part.rfind(prefix, 0) == 0) part.erase(0, std::strlen(prefix));
            }
            if (!part.empty() && part != "etc.") names.push_back(part);
            if (comma == std::string::npos) break;
            start = comma + 2;
        }
        return names;
    };
    int found = -1;
    for (int pass = 0; pass < 2 && found < 0 && !unit_name.empty(); ++pass) {
        for (int i = 0; i < count && found < 0; ++i) {
            for (const std::string& name : names_of(Core::Hotkeys::NameString(group, items[i]))) {
                const bool exact = unit_name == name;
                const bool near = unit_name.rfind(name + " ", 0) == 0 ||
                                  (unit_name.size() > name.size() &&
                                   unit_name.compare(unit_name.size() - name.size() - 1,
                                                     std::string::npos, " " + name) == 0);
                if (pass == 0 ? exact : near) { found = items[i]; break; }
            }
        }
    }
    if (found < 0 && group == Core::Hotkeys::kCastle) found = 1;   // Build Unique Unit
    known[group][unit_id] = found;
    return found;
}

// Whether a unit's panel has the Garrison button: by its class -- foot
// soldiers, villagers, cavalry, monks, the King -- and never for unit 700.
bool GarrisonButton(const entt::registry& registry, entt::entity unit) {
    const auto* id = registry.try_get<ECS::Components::Identity>(unit);
    if (id == nullptr || id->unit_id == 700) return false;
    const auto* rules = ECS::Players::UnitOf(registry, LocalViewer(), id->unit_id);
    if (rules == nullptr) return false;
    switch (rules->cls) {
    case 0: case 4: case 6: case 12: case 18: case 36: case 43: case 44:
    case 45: case 46: case 47: case 50: case 56: case 57: case 59:
        return true;
    default:
        return false;
    }
}

void BindHotkey(UIRenderer::Command* cmd, int group, int item) {
    if (cmd == nullptr) return;
    cmd->hotkey_group = group;
    cmd->hotkey_item = item;
}

} // namespace

int UIRenderer::BuildCommands(ECS::Simulation& sim, int win_w, int win_h,
                              Command* out, int max_out) {
    (void)win_h;
    auto& registry = sim.GetRegistry();
    auto& cfg = Core::ConfigManager::Get();
    const entt::entity selected = PrimarySelection(sim);
    if (selected == entt::null || max_out <= 0) {
        HudState().page = kPageRoot;
        return 0;
    }

    // Selecting something else always lands on the first page.
    HudState().Follow(selected);
    const bool advanced = HudState().advanced;

    // Slots are claimed by ButtonID; the first claim of a slot wins.
    Command slots[kCommandSlots];

    auto claim = [&](int button) -> Command* {
        if (button < 1 || button > kCommandSlots) return nullptr;
        Command& cmd = slots[button - 1];
        if (cmd.icon_frame >= 0) return nullptr;   // first claim wins
        cmd.rect = CommandSlotRect(button - 1, win_w);
        cmd.slot = button;
        return &cmd;
    };
    // A command from the command icon sheet.
    auto order = [&](int button, CommandKind kind, int frame, int name) -> Command* {
        Command* cmd = claim(button);
        if (!cmd) return nullptr;
        cmd->kind = kind;
        cmd->icon_slp = cfg.CommandIconSlp();
        cmd->icon_frame = frame;
        cmd->name_string = name;
        cmd->affordable = true;
        cmd->enabled = true;
        return cmd;
    };
    // The Garrison button, whose picture is the cursor sheet's.
    auto garrison = [&]() -> Command* {
        Command* cmd = order(kSlotGarrison, CommandKind::Garrison, kIconGarrison, 41068);
        if (cmd) cmd->icon_slp = kCursorSlp;
        return cmd;
    };

    // Placing a building: only the way out. Choosing a building's gather
    // point: removing it, or the way out.
    int placing = -1;
    const bool placing_building = Core::InputManager::Get().PlacingBuilding(placing);
    if (placing_building || (Core::InputManager::Get().ChoosingGatherPoint() &&
                             registry.all_of<ECS::Components::Building>(selected))) {
        if (!placing_building) {
            order(kSlotRemoveGatherPoint, CommandKind::RemoveGatherPoint, kIconRemoveGatherPoint,
                  4949);
        }
        order(kSlotCancel, CommandKind::CancelOrder, kIconCancel, 4911);
        int written = 0;
        for (int i = 0; i < kCommandSlots && written < max_out; ++i) {
            if (slots[i].icon_frame >= 0) out[written++] = slots[i];
        }
        return written;
    }

    // The hotkey group the unit buttons answer to: the build list for a
    // builder, the building's own for what it trains.
    int hotkey_group = -1;

    // A unit's button, at the slot its own rules name: something to train, or
    // a building to place from a villager's build menu.
    auto place = [&](int unit_id, bool building, bool enabled) {
        const auto* rules = ECS::Players::UnitOf(registry, LocalViewer(), unit_id);
        if (!rules) return;
        Command* cmd = claim(rules->button);
        if (!cmd) return;
        cmd->kind = CommandKind::Produce;
        cmd->unit_id = unit_id;
        cmd->places_building = building;
        cmd->icon_slp = IconSheetFor(unit_id);
        cmd->icon_frame = rules->icon;
        cmd->affordable = CanAfford(registry, rules->costs, rules->cost_count);
        cmd->enabled = enabled;
        cmd->hotkey_group = hotkey_group;
        cmd->hotkey_item = HotkeyItemFor(hotkey_group, unit_id);
    };

    // A technology's button, at the slot the tech names.
    auto research = [&](int tech_id, bool enabled) {
        const auto* tech = Core::Rules::Tech(tech_id);
        const auto* player = ECS::Players::Find(registry, LocalViewer());
        if (!tech || !player) return;
        Command* cmd = claim(tech->button);
        if (!cmd) return;
        const auto& status = player->techs[tech_id];
        cmd->kind = CommandKind::Research;
        cmd->tech_id = tech_id;
        cmd->icon_slp = cfg.ResearchIconSlp();
        cmd->icon_frame = tech->icon;
        cmd->affordable = CanAfford(registry, status.costs, status.cost_count);
        cmd->enabled = enabled;
    };

    auto page_switch = [&](int slot, int frame, int page, int name) {
        if (Command* cmd = order(slot, CommandKind::SwitchPage, frame, name)) {
            cmd->target_page = page;
        }
    };

    // How many of the player's units are selected, buildings aside.
    int selected_units = 0;
    {
        auto picked = registry.view<ECS::Components::Selectable, ECS::Components::Owner>();
        for (auto e : picked) {
            if (picked.get<ECS::Components::Selectable>(e).is_selected &&
                picked.get<ECS::Components::Owner>(e).player_id == LocalViewer() &&
                !registry.all_of<ECS::Components::Building>(e)) {
                ++selected_units;
            }
        }
    }

    const auto& unit_rules = Core::Rules::Get().units;
    if (const auto* own_worker = registry.try_get<ECS::Components::Worker>(selected);
        own_worker != nullptr && own_worker->gatherer >= 0) {
        // Something that works as itself -- a fishing ship -- has no build
        // menus: what the dat trains at it (the fish trap) is on its own button.
        hotkey_group = Core::Hotkeys::kBuild;
        for (int unit_id = 0; unit_id < static_cast<int>(unit_rules.size()); ++unit_id) {
            const auto* rules = ECS::Players::UnitOf(registry, LocalViewer(), unit_id);
            if (!rules || !rules->enabled || rules->train_loc != own_worker->gatherer) continue;
            place(unit_id, true, true);
        }
    } else if (registry.all_of<ECS::Components::Worker>(selected)) {
        const int page = HudState().page;
        if (page == kPageRoot) {
            // A villager's own commands: the two build menus, repair, delete,
            // garrison, and stop.
            namespace HK = Core::Hotkeys;
            page_switch(kSlotBuildEconomic, kIconBuildEconomic,
                        Core::ConfigManager::kInterfaceEconomic, 41061);
            BindHotkey(&slots[kSlotBuildEconomic - 1], HK::kUnitCommands, HK::kEconomicBuildings);
            page_switch(kSlotBuildMilitary, kIconBuildMilitary,
                        Core::ConfigManager::kInterfaceMilitary, 41062);
            BindHotkey(&slots[kSlotBuildMilitary - 1], HK::kUnitCommands, HK::kMilitaryBuildings);
            BindHotkey(order(kSlotRepair, CommandKind::Repair, kIconRepair, 4927),
                       HK::kUnitCommands, HK::kRepair);
            BindHotkey(order(kSlotUnitDelete, CommandKind::Delete, kIconDelete, 4941),
                       HK::kUnitCommands, HK::kDelete);
            BindHotkey(garrison(),
                       HK::kUnitCommands, HK::kGarrison);
            BindHotkey(order(kSlotStop, CommandKind::Stop, kIconStop, 4905),
                       HK::kUnitCommands, HK::kStop);
        } else {
            hotkey_group = Core::Hotkeys::kBuild;
            // Inside a build menu: every building the player can build that
            // belongs to this page. The dat lists a building as trained by the
            // builder villager, and its interface kind says which page.
            const int builder = ECS::Villagers::UnitId(
                ECS::Components::VillagerJob::Build, false);
            for (int unit_id = 0; unit_id < static_cast<int>(unit_rules.size()); ++unit_id) {
                const auto* rules = ECS::Players::UnitOf(registry, LocalViewer(), unit_id);
                if (!rules || !rules->enabled || rules->train_loc != builder) continue;
                const auto* ui = cfg.GetUnitInterface(unit_id);
                if (ui && ui->interface_kind == page) place(unit_id, true, true);
            }
            page_switch(kSlotCancel, kIconCancel, kPageRoot, 4911);
        }
    } else if (ECS::Packing::Packs(registry, selected) &&
               !registry.all_of<ECS::Components::Combat>(selected)) {
        // A packed trebuchet: unpack, stop, delete, garrison.
        if (Command* cmd = order(kSlotUnpack, CommandKind::Pack, kIconUnpack, 41056)) {
            cmd->choice = 0;
            BindHotkey(cmd, Core::Hotkeys::kUnitCommands, Core::Hotkeys::kUnpack);
        }
        order(kSlotStop, CommandKind::Stop, kIconStop, 4905);
        order(kSlotUnitDelete, CommandKind::Delete, kIconDelete, 4941);
        garrison();
    } else if (const auto* combat = registry.try_get<ECS::Components::Combat>(selected);
               combat != nullptr && !registry.all_of<ECS::Components::Building>(selected)) {
        // A trebuchet set up packs again, its Pack where another unit's
        // standing orders go.
        const bool trebuchet = ECS::Packing::Packs(registry, selected);
        if (trebuchet) {
            if (Command* cmd = order(kSlotPack, CommandKind::Pack, kIconPack, 41055)) {
                cmd->choice = 1;
                BindHotkey(cmd, Core::Hotkeys::kUnitCommands, Core::Hotkeys::kPack);
            }
        }
        // A fighting unit's own commands. Its standing orders, its stances and
        // the formations are advanced commands; delete and garrison are always
        // there.
        namespace HK = Core::Hotkeys;
        if (advanced) {
            if (!trebuchet) {
                BindHotkey(order(kSlotPatrol, CommandKind::Patrol, kIconPatrol, 4938),
                           HK::kMilitary, HK::kPatrol);
                BindHotkey(order(kSlotGuard, CommandKind::Guard, kIconGuard, 4936),
                           HK::kMilitary, HK::kGuard);
                BindHotkey(order(kSlotFollow, CommandKind::Follow, kIconFollow, 4937),
                           HK::kMilitary, HK::kFollow);
            }
            static const int kStanceHelp[4] = {4933, 4934, 4922, 4935};
            static const int kStanceKey[4] = {HK::kAggressive, HK::kDefensive,
                                              HK::kStandGround, HK::kNoAttack};
            for (int s = 0; s < 4; ++s) {
                const bool lit = static_cast<int>(combat->stance) == s;
                if (Command* cmd = order(kSlotFirstStance + s, CommandKind::Stance,
                                         lit ? kIconStancesLit[s] : kIconStances[s],
                                         kStanceHelp[s])) {
                    cmd->choice = s;
                    BindHotkey(cmd, HK::kMilitary, kStanceKey[s]);
                }
            }
            // The formations, for two or more soldiers: the one they are
            // locked in lit. One in a formation can disband it.
            const auto* locked = registry.try_get<ECS::Components::InFormation>(selected);
            HudState().formation = locked != nullptr ? locked->formation : Core::Formation::Line;
            if (selected_units > 1) {
                static const int kFormationHelp[4] = {41021, 41020, 41022, 41023};
                static const int kFormationKey[4] = {HK::kLine, HK::kBox, HK::kStaggered,
                                                     HK::kFlank};
                for (int f = 0; f < 4; ++f) {
                    const bool lit = locked != nullptr && locked->formation == kFormationOrder[f];
                    if (Command* cmd = order(kSlotFirstFormation + f, CommandKind::Formation,
                                             lit ? kIconFormationsLit[f] : kIconFormations[f],
                                             kFormationHelp[f])) {
                        cmd->choice = static_cast<int>(kFormationOrder[f]);
                        BindHotkey(cmd, HK::kMilitary, kFormationKey[f]);
                    }
                }
            }
            if (locked != nullptr) order(kSlotDisband, CommandKind::Disband, kIconDisband, kDisbandHelp);
        } else {
            BindHotkey(order(kSlotStop, CommandKind::Stop, kIconStop, 4905),
                       HK::kUnitCommands, HK::kStop);
        }
        BindHotkey(order(kSlotUnitDelete, CommandKind::Delete, kIconDelete, 4941),
                   HK::kUnitCommands, HK::kDelete);
        if (GarrisonButton(registry, selected)) {
            BindHotkey(garrison(),
                       HK::kUnitCommands, HK::kGarrison);
        }
    } else if (!registry.all_of<ECS::Components::Building>(selected) &&
               registry.all_of<ECS::Components::Identity, ECS::Components::Transform>(selected)) {
        // A unit that does not fight -- the King, a trade cart: stop, delete,
        // and the Garrison button by its class. A Monk, unless it carries a
        // relic, converts (when its player may) and heals; a loaded transport
        // unloads.
        namespace HK = Core::Hotkeys;
        if (registry.all_of<ECS::Components::Monk>(selected) &&
            !registry.all_of<ECS::Components::RelicBearer>(selected)) {
            const int unit_id = registry.get<ECS::Components::Identity>(selected).unit_id;
            const auto* rules = ECS::Players::UnitOf(registry, LocalViewer(), unit_id);
            auto performs = [&](int action) {
                for (int t = 0; rules != nullptr && t < rules->task_count; ++t) {
                    if (rules->tasks[t].action == action) return true;
                }
                return false;
            };
            const auto* me = ECS::Players::Find(registry, LocalViewer());
            const bool may_convert = me != nullptr &&
                ECS::Players::Resource(*me, kCanConvert) > Core::Fixed32(0);
            int slot = kSlotConvert;
            if (performs(kConvertTask) && may_convert) {
                BindHotkey(order(slot++, CommandKind::Convert, kIconConvert, 4925),
                           HK::kUnitCommands, HK::kConvert);
            }
            if (performs(kHealTask)) {
                BindHotkey(order(slot, CommandKind::Heal, kIconHeal, 4924),
                           HK::kUnitCommands, HK::kHeal);
            }
        }
        if (ECS::Garrisons::IsTransport(registry, selected) &&
            ECS::Garrisons::Count(registry, selected) > 0) {
            BindHotkey(order(kSlotUnload, CommandKind::Unload, kIconUnload, 4907),
                       HK::kUnitCommands, HK::kUnload);
        }
        BindHotkey(order(kSlotStop, CommandKind::Stop, kIconStop, 4905),
                   HK::kUnitCommands, HK::kStop);
        BindHotkey(order(kSlotUnitDelete, CommandKind::Delete, kIconDelete, 4941),
                   HK::kUnitCommands, HK::kDelete);
        if (GarrisonButton(registry, selected)) {
            BindHotkey(garrison(),
                       HK::kUnitCommands, HK::kGarrison);
        }
    } else if (registry.all_of<ECS::Components::Identity>(selected)) {
        // A building offers whatever the player's rules say it trains and
        // researches, usable once it is finished and its queue has room; the
        // price is checked by `place` and `research`.
        const int building_id = registry.get<ECS::Components::Identity>(selected).unit_id;
        const auto* site = registry.try_get<ECS::Components::Building>(selected);
        const auto* queue = registry.try_get<ECS::Components::ProductionQueue>(selected);
        // Not a building at all -- a transport ship -- counts as finished, so
        // a loaded one offers the unload button below.
        const bool finished =
            site == nullptr || site->state == ECS::Components::BuildingState::Complete;
        const bool can_queue = finished &&
            (queue == nullptr || queue->count < ECS::Components::ProductionQueue::kCapacity);
        hotkey_group = HotkeyGroupOfBuilding(building_id);
        if (finished) {
            bool trains = false;
            for (int unit_id = 0; unit_id < static_cast<int>(unit_rules.size()); ++unit_id) {
                const auto* rules = ECS::Players::UnitOf(registry, LocalViewer(), unit_id);
                if (!rules || !rules->enabled || rules->train_loc != building_id) continue;
                place(unit_id, false, can_queue);
                trains = true;
            }
            // Where what it trains gathers.
            if (trains) {
                BindHotkey(order(kSlotGatherPoint, CommandKind::GatherPoint, kIconGatherPoint, 4944),
                           Core::Hotkeys::kUnitCommands, Core::Hotkeys::kGatherPoint);
            }
            for (int tech_id : ECS::Techs::ResearchableAt(registry, LocalViewer(), building_id)) {
                research(tech_id, can_queue);
            }
            // The next age is offered from the start, greyed until its
            // requirements are met -- unlike any other technology, which stays
            // hidden until it could be researched. Already placed above if it
            // is available.
            const int next_age = ECS::Techs::NextAgeTech(registry, LocalViewer());
            const auto* age_tech = Core::Rules::Tech(next_age);
            const auto* me = ECS::Players::Find(registry, LocalViewer());
            if (age_tech != nullptr && me != nullptr && age_tech->location == building_id &&
                me->techs[next_age].state == ECS::Players::TechState::Locked) {
                research(next_age, false);
            }
        }
        // Its garrison: turning everyone out; the villagers back to work from
        // a Town Center, a Castle or a tower; and a Town Center's bell.
        if (finished && ECS::Garrisons::Count(registry, selected) > 0) {
            order(kSlotUngarrison, CommandKind::Ungarrison, kIconUngarrison, 41014);
        }
        const auto* kind = ECS::Players::UnitOf(registry, LocalViewer(), building_id);
        if (finished && ECS::Garrisons::Villagers(registry, selected) > 0 &&
            (building_id == kTownCenter || building_id == kCastle ||
             (kind != nullptr && kind->cls == kTowerClass))) {
            BindHotkey(order(kSlotBackToWork, CommandKind::BackToWork, kIconBackToWork, 41015),
                       Core::Hotkeys::kTownCenter, Core::Hotkeys::kBackToWork);
        }
        if (finished && ECS::Garrisons::HasBell(registry, selected)) {
            const auto* bell = registry.try_get<ECS::Components::Garrison>(selected);
            const bool rung = bell != nullptr && bell->bell_rung;
            if (Command* cmd = order(kSlotTownBell, CommandKind::TownBell,
                                     rung ? kIconTownBellRung : kIconTownBell, 41111)) {
                cmd->choice = rung ? 1 : 0;
                BindHotkey(cmd, Core::Hotkeys::kTownCenter, Core::Hotkeys::kRingBell);
            }
        }
        // A Castle of the player's in a Regicide game: Treason, at the button
        // Spies would have, usable with the gold for it.
        if (finished && TreasonAt(registry, selected)) {
            if (Command* cmd = order(TreasonButton(), CommandKind::Treason, kIconTreason, kTreasonHelp)) {
                cmd->affordable = ECS::Spies::CanTreason(registry, LocalViewer());
            }
        }
        // A Market of the player's own: selling and buying, each button
        // showing the price of the moment for this player.
        if (ECS::Market::IsMarket(registry, selected, LocalViewer())) {
            const auto* me = ECS::Players::Find(registry, LocalViewer());
            for (const auto& [slot, caption] : {std::pair{kSlotSellCaption, kSellCaption},
                                                std::pair{kSlotBuyCaption, kBuyCaption}}) {
                if (Command* cmd = claim(slot)) {
                    cmd->kind = CommandKind::Caption;
                    cmd->caption = caption;
                    cmd->icon_frame = 0;   // the slot is taken, though nothing is drawn
                    cmd->enabled = true;
                }
            }
            for (int i = 0; i < 3; ++i) {
                const int res = kTradeOrder[i];
                if (Command* cmd = order(kSlotFirstSell + i, CommandKind::Sell, kIconCommodity[res],
                                         kSellHelp[res])) {
                    cmd->choice = res;
                    cmd->label = ECS::Market::SellPrice(registry, LocalViewer(), res);
                    cmd->affordable = me != nullptr &&
                        ECS::Players::Resource(*me, res) >= Core::Fixed32(ECS::Market::kLot);
                }
                if (Command* cmd = order(kSlotFirstBuy + i, CommandKind::Buy, kIconCommodity[res],
                                         kBuyHelp[res])) {
                    cmd->choice = res;
                    cmd->label = ECS::Market::BuyPrice(registry, LocalViewer(), res);
                    cmd->affordable = me != nullptr &&
                        ECS::Players::Resource(*me, Core::Rules::Res::Gold) >=
                            Core::Fixed32(cmd->label);
                }
            }
        }
        // A gate's lock: Lock Gate while it is open, Unlock Gate while it is
        // locked (LANGUAGE.DLL 41104, 41105).
        if (const auto* gate = registry.try_get<ECS::Components::Gate>(selected);
            gate != nullptr && finished) {
            if (Command* cmd = order(kSlotLockGate, CommandKind::LockGate,
                                     gate->locked ? kIconUnlockGate : kIconLockGate,
                                     gate->locked ? 41105 : 41104)) {
                cmd->choice = gate->locked ? 0 : 1;
            }
        }
    }

    int written = 0;
    for (int i = 0; i < kCommandSlots && written < max_out; ++i) {
        if (slots[i].icon_frame >= 0) out[written++] = slots[i];
    }
    return written;
}

Core::Formation UIRenderer::CurrentFormation() {
    return HudState().formation;
}

void UIRenderer::OpenBuildMenu(ECS::Simulation& sim, int page) {
    HudState().Follow(PrimarySelection(sim));
    HudState().page = page;
}

UIRenderer::Action UIRenderer::ActivateCommand(const Command& command) {
    switch (command.kind) {
    case CommandKind::SwitchPage:
        HudState().page = command.target_page;
        return Action::None;
    case CommandKind::Delete:
        return Action::DeleteBuilding;
    case CommandKind::Produce:
        if (!command.enabled) return Action::None;
        // A building button places a building; a unit button trains a unit.
        return command.places_building ? Action::PlaceBuilding : Action::TrainUnit;
    case CommandKind::Research:
        return command.enabled ? Action::Research : Action::None;
    case CommandKind::Stance:
        return Action::SetStance;
    case CommandKind::Formation:
        return Action::Form;
    case CommandKind::Disband:
        return Action::Disband;
    case CommandKind::Stop:
        return Action::Stop;
    case CommandKind::Ungarrison:
        return Action::Ungarrison;
    case CommandKind::TownBell:
        return Action::TownBell;
    case CommandKind::Patrol:
        return Action::Patrol;
    case CommandKind::Guard:
        return Action::Guard;
    case CommandKind::Follow:
        return Action::Follow;
    case CommandKind::Garrison:
        return Action::Garrison;
    case CommandKind::Repair:
        return Action::Repair;
    case CommandKind::Sell:
        return Action::Sell;
    case CommandKind::Buy:
        return Action::Buy;
    case CommandKind::LockGate:
        return Action::LockGate;
    case CommandKind::Treason:
        return Action::Treason;
    case CommandKind::Pack:
        return Action::Pack;
    case CommandKind::Convert:
        return Action::Convert;
    case CommandKind::Heal:
        return Action::Heal;
    case CommandKind::Unload:
        return Action::Unload;
    case CommandKind::GatherPoint:
        return Action::GatherPoint;
    case CommandKind::RemoveGatherPoint:
        return Action::RemoveGatherPoint;
    case CommandKind::CancelOrder:
        return Action::CancelOrder;
    case CommandKind::BackToWork:
        return Action::BackToWork;
    case CommandKind::Caption:
        return Action::None;
    }
    return Action::None;
}

int UIRenderer::BuildQueueSlots(ECS::Simulation& sim, int win_w, int win_h,
                                QueueSlot* out, int max_out) {
    (void)win_h;
    // Only the player's own buildings show a queue it could act on.
    const entt::entity building = SelectedBuilding(sim);
    if (building == entt::null || max_out <= 0) return 0;
    const auto* queue =
        sim.GetRegistry().try_get<ECS::Components::ProductionQueue>(building);
    if (queue == nullptr || queue->count <= 0) return 0;

    // The queue sits right of the portrait's column, where a unit's owner
    // would be: one button per run of the same unit, its count on it, the one
    // under way first.
    const HudLayout& layout = Lay(win_w);
    int written = 0;
    for (int slot = 0; slot < queue->count && written < max_out; ++slot) {
        const auto& item = queue->items[slot];
        if (written > 0 && !item.tech && !out[written - 1].tech && out[written - 1].id == item.id) {
            out[written - 1].count += 1;
            out[written - 1].slot = slot;
            continue;
        }
        const int x = layout.civ.x - 6 + written * 41;
        if (x + 40 > layout.selection.x + layout.selection.w) break;
        out[written++] = QueueSlot{slot, item.id, 1, item.tech,
                                   SDL_FRect{static_cast<float>(x),
                                             static_cast<float>(layout.portrait.y - 2),
                                             40.0f, 40.0f}};
    }
    return written;
}

int UIRenderer::BuildSelectionCards(ECS::Simulation& sim, int win_w, int win_h,
                                    SelectionCard* out, int max_out) {
    (void)win_h;
    auto& registry = sim.GetRegistry();
    std::vector<entt::entity> selected;
    SelectedEntities(registry, selected);
    if (selected.size() < 2) return 0;

    // The cards: a 36x36 icon in a two-pixel bevel, 40 across, three rows 42
    // apart. A row holds as many
    // as fit side by side; past three full rows each row takes a third of the
    // group and its cards close up, overlapping, to keep within the
    // parchment -- 23 selected at 800x600 is three rows of 8 at 35 apart.
    const HudLayout& layout = Lay(win_w);
    constexpr int kCard = 40;
    constexpr int kRowPitch = 42;
    constexpr int kRows = 3;
    const SDL_Rect& area = layout.selection;
    const int per_row_open = std::max(1, (area.w - kCard) / kCard + 1);
    const int n = std::min(static_cast<int>(selected.size()), max_out);
    int per_row = per_row_open;
    if (n > per_row_open * kRows) per_row = (n + kRows - 1) / kRows;
    const int step = per_row <= per_row_open ? kCard
                   : std::max(1, (area.w - kCard) / std::max(1, per_row - 1));
    for (int i = 0; i < n; ++i) {
        const int col = i % per_row;
        const int row = i / per_row;
        out[i] = SelectionCard{selected[i], SDL_Rect{area.x + col * step,
                                                     area.y + row * kRowPitch, kCard, kCard}};
    }
    return n;
}

int UIRenderer::Refusal(ECS::Simulation& sim, const Command& command) {
    auto& registry = sim.GetRegistry();
    const ECS::Players::Player* player = ECS::Players::Find(registry, LocalViewer());
    if (player == nullptr) return -1;
    const Core::Rules::Cost* costs = nullptr;
    int count = 0;
    if (command.kind == CommandKind::Produce) {
        const auto* rules = ECS::Players::UnitOf(registry, LocalViewer(), command.unit_id);
        if (rules == nullptr) return -1;
        costs = rules->costs;
        count = rules->cost_count;
    } else if (command.kind == CommandKind::Research) {
        const auto& status = player->techs[command.tech_id];
        costs = status.costs;
        count = status.cost_count;
    } else {
        return -1;
    }
    // The first stockpile short, checked in this order.
    static const struct { Core::ResourceType type; int line; } kShort[] = {
        {Core::ResourceType::Food, 3001}, {Core::ResourceType::Wood, 3002},
        {Core::ResourceType::Stone, 3003}, {Core::ResourceType::Gold, 3004},
    };
    int price[4];
    StockpileCosts(costs, count, price);
    for (const auto& s : kShort) {
        if (price[static_cast<int>(s.type)] > ECS::Players::Stock(*player, s.type)) return s.line;
    }
    // A unit needs room in the houses.
    if (command.kind == CommandKind::Produce && !command.places_building) {
        int headroom = 0;
        for (int i = 0; i < count; ++i) {
            if (!costs[i].paid) headroom += costs[i].amount;
        }
        if (headroom > 0 && !ECS::Population::HasRoom(registry, LocalViewer(), headroom)) {
            return kNeedHouses;
        }
    }
    return -1;
}

bool UIRenderer::ShowsGarrison(const entt::registry& registry, entt::entity selected) {
    const auto* owner = registry.try_get<ECS::Components::Owner>(selected);
    return owner != nullptr && owner->player_id == LocalViewer() &&
           ECS::Garrisons::Capacity(registry, selected) > 0;
}

std::string UIRenderer::PlayerName(const entt::registry& registry, Core::PlayerId id) {
    return Rendering::PlayerName(registry, id);
}

entt::entity UIRenderer::SelectedBuilding(ECS::Simulation& sim) {
    const entt::entity selected = PrimarySelection(sim);
    if (selected == entt::null) return entt::null;
    return sim.GetRegistry().all_of<ECS::Components::Building>(selected)
         ? selected : entt::null;
}

namespace {

// The top bar: the stockpiles and population, the age, and the buttons.
void DrawTopBar(SDL_Renderer* sdl_r, ECS::Simulation& sim, const HudLayout& layout) {
    auto& registry = sim.GetRegistry();
    const ECS::Players::Player* player = ECS::Players::Find(registry, LocalViewer());

    // Population: current/supportable, both from the population module that
    // production acts on.
    const int used = ECS::Population::Used(registry, LocalViewer());
    const int supported = ECS::Population::Capacity(registry, LocalViewer());

    static constexpr Core::ResourceType kOrder[4] = {
        Core::ResourceType::Wood, Core::ResourceType::Food,
        Core::ResourceType::Gold, Core::ResourceType::Stone};
    for (int slot = 0; slot < 5; ++slot) {
        char text[24];
        if (slot == 4) {
            std::snprintf(text, sizeof(text), "%d/%d", used, supported);
        } else {
            std::snprintf(text, sizeof(text), "%d",
                          player != nullptr ? ECS::Players::Stock(*player, kOrder[slot]) : 0);
        }
        const int w = Text::Width(Text::kGeorgia9B, text);
        Text::Draw(sdl_r, Text::kGeorgia9B, layout.resource_right[slot] - w, layout.text_y,
                   text, kWhite);
    }

    // The age the player is in, by its tech's label, centred between the
    // stockpiles and the buttons.
    const int age_tech = ECS::Techs::AgeTech(registry, LocalViewer(),
                                             ECS::Techs::Age(registry, LocalViewer()));
    const std::string& age = Core::Rules::TechLabel(age_tech);
    if (!age.empty()) {
        const int w = Text::Width(Text::kGeorgia9B, age);
        const int x = (layout.age_left + layout.age_right - w) / 2;
        Text::DrawShadowed(sdl_r, Text::kGeorgia9B, x, layout.age_y, age, kWhite, kBlack);
    }

    int mx = 0, my = 0;
    bool held = false;
    Mouse(mx, my, held);
    for (int b = 0; b < kBarButtonCount; ++b) {
        const SDL_Rect r = {layout.bar_button[b].x, layout.bar_button[b].y, 50, 17};
        const bool pressed = held && Inside(r, mx, my);
        DrawFrame(sdl_r, kBarButtonSlp, kBarButtonFrame[b] + (pressed ? 1 : 0), r.x, r.y);
    }
}

// The command grid, in the left block: each button its icon in a bevel,
// pressed while the mouse holds it.
void DrawCommandPanel(SDL_Renderer* sdl_r, ECS::Simulation& sim, int win_w, int win_h) {
    UIRenderer::Command commands[UIRenderer::kCommandSlots];
    const int n = UIRenderer::BuildCommands(sim, win_w, win_h, commands,
                                            UIRenderer::kCommandSlots);
    int mx = 0, my = 0;
    bool held = false;
    Mouse(mx, my, held);
    for (int i = 0; i < n; ++i) {
        const UIRenderer::Command& cmd = commands[i];
        const int x = static_cast<int>(cmd.rect.x);
        const int y = static_cast<int>(cmd.rect.y);
        // Words with no button: the Market's "Sell:" and "Buy:", centred in
        // their slot.
        if (cmd.kind == UIRenderer::CommandKind::Caption) {
            const std::string& words = Core::Strings::Get(cmd.caption);
            Text::DrawShadowed(sdl_r, Text::kArial8, x + (40 - Text::Width(Text::kArial8, words)) / 2,
                               y + (40 - Text::Height(Text::kArial8)) / 2, words, kWhite, kBlack);
            continue;
        }
        const bool pressed = held && Inside(cmd.rect, mx, my);
        DrawIcon(sdl_r, cmd.icon_slp, cmd.icon_frame, SDL_Rect{x + 2, y + 2, 36, 36},
                 LocalViewer(), cmd.enabled ? 255 : 110);
        Bevel(sdl_r, x, y, 40, 40, pressed);
        // A number on the button's face -- a Market button's price: white at
        // (3, 2) from the corner, a pixel further while pressed, over black at
        // the four diagonals.
        if (cmd.label >= 0) {
            const std::string number = std::to_string(cmd.label);
            const int tx = x + 3 + (pressed ? 1 : 0);
            const int ty = y + 2 + (pressed ? 1 : 0);
            for (const auto& [dx, dy] : {std::pair{-1, -1}, std::pair{-1, 1}, std::pair{1, -1},
                                          std::pair{1, 1}}) {
                Text::Draw(sdl_r, Text::kArial8, tx + dx, ty + dy, number, kBlack);
            }
            Text::Draw(sdl_r, Text::kArial8, tx, ty, number, kWhite);
        }
    }
}

// A hit point bar: green for what is left, red for what is gone.
void HealthBar(SDL_Renderer* sdl_r, int x, int y, int w, int h, int hp, int max_hp) {
    const int left = max_hp > 0 ? std::max(0, std::min(w, (hp * w + max_hp - 1) / max_hp)) : 0;
    Fill(sdl_r, x, y, left, h, kHealthGreen);
    Fill(sdl_r, x + left, y, w - left, h, kHealthRed);
}

// One stat line: its symbol from the panel's sheet, and its number beside it.
void StatLine(SDL_Renderer* sdl_r, const HudLayout& layout, int row, int icon,
              const std::string& text) {
    DrawFrame(sdl_r, kStatIconSlp, icon, layout.stat_icon.x,
              layout.stat_icon.y + row * layout.stat_pitch);
    Text::Draw(sdl_r, Text::kArial8, layout.stat_text.x,
               layout.stat_text.y + row * layout.stat_pitch, text, kBlack);
}

int StatIconFor(Core::ResourceType type) {
    switch (type) {
    case Core::ResourceType::Wood: return kStatWood;
    case Core::ResourceType::Food: return kStatFood;
    case Core::ResourceType::Gold: return kStatGold;
    case Core::ResourceType::Stone: return kStatStone;
    default: return kStatFood;
    }
}

// The selected object: its name, its portrait with the hit point bar under it,
// its numbers each beside its symbol, and -- whoever owns it -- the owner's
// civilisation, name and standing. A building also shows what it is making.
void DrawUnitPanel(SDL_Renderer* sdl_r, ECS::Simulation& sim, int win_w, int win_h) {
    auto& registry = sim.GetRegistry();
    const entt::entity selected = InspectedSelection(sim);
    if (selected == entt::null) return;
    if (!registry.all_of<ECS::Components::Identity>(selected)) return;

    auto& cfg = Core::ConfigManager::Get();
    const HudLayout& layout = Lay(win_w);
    // A villager at work is a different dat unit with its own name and icon --
    // a lumberjack, a miner, a builder -- while the entity keeps its identity
    // as a plain villager. The job module owns that resolution.
    //
    // An upgraded unit shows what it has become: the owner's slot for a
    // militia holds a Man-at-Arms after the upgrade, name, icon and all.
    const auto* owner_of = registry.try_get<ECS::Components::Owner>(selected);
    const Core::PlayerId owner = owner_of ? owner_of->player_id : Core::PLAYER_GAIA;
    const int slot_id = ECS::Villagers::DisplayUnit(registry, selected);
    const int unit_id = ECS::Players::ArtOf(registry, owner, slot_id);
    const auto* ui = cfg.GetUnitInterface(unit_id);
    if (!ui) return;
    // The owner's own numbers where it has them; a map object's from the
    // Gaia table the interface data carries. What technologies add is the
    // difference from the dat's own unit.
    const auto* rules = owner != Core::PLAYER_GAIA
        ? ECS::Players::UnitOf(registry, owner, slot_id) : nullptr;
    const auto& base_units = Core::Rules::Get().units;
    const Core::Rules::UnitRules* base =
        unit_id >= 0 && unit_id < static_cast<int>(base_units.size()) ? &base_units[unit_id]
                                                                      : nullptr;

    const std::string& name = cfg.GetUnitName(unit_id);
    if (!name.empty()) {
        Text::Draw(sdl_r, Text::kGeorgia9B, layout.name.x, layout.name.y, name, kBlack);
    }

    // The portrait sits a little into the parchment: two rows of its shadow
    // along the top and left.
    Fill(sdl_r, layout.portrait.x - 2, layout.portrait.y - 2, 38, 1, kPortraitShadowOuter);
    Fill(sdl_r, layout.portrait.x - 2, layout.portrait.y - 1, 1, 37, kPortraitShadowOuter);
    Fill(sdl_r, layout.portrait.x - 1, layout.portrait.y - 1, 37, 1, kPortraitShadowInner);
    Fill(sdl_r, layout.portrait.x - 1, layout.portrait.y, 1, 36, kPortraitShadowInner);
    DrawIcon(sdl_r, IconSheetFor(unit_id), ui->icon,
             SDL_Rect{layout.portrait.x, layout.portrait.y, 36, 36},
             owner != Core::PLAYER_GAIA ? owner : LocalViewer(), 255);

    int row = 0;
    if (const auto* health = registry.try_get<ECS::Components::Health>(selected)) {
        HealthBar(sdl_r, layout.portrait.x - 2, layout.portrait.y + 38, 40, 4,
                  health->current_hp, health->max_hp);
        char hp[32];
        std::snprintf(hp, sizeof(hp), "%d/%d", health->current_hp, health->max_hp);
        Text::Draw(sdl_r, Text::kArial8, layout.hp_text.x, layout.hp_text.y, hp, kBlack);

        // A trebuchet packing or setting up: how far along, first, as
        // "%d%%" (Packing and Unpacking Progress, strings 42022-42023).
        if (ECS::Packing::Busy(registry, selected)) {
            StatLine(sdl_r, layout, row++, kStatProgress,
                     std::to_string(ECS::Packing::ProgressPercent(registry, selected)) + "%");
        }
        // Attack and armour, as base and what technologies add. A tree has no
        // hit points and shows none of this.
        const int attack = rules ? DisplayedAttack(*rules) : ui->attack;
        const int base_attack = base ? DisplayedAttack(*base) : attack;
        if (attack > 0) StatLine(sdl_r, layout, row++, kStatAttack, WithBonus(base_attack, attack));
        const int melee = rules ? MeleeArmour(*rules) : ui->melee_armour;
        const int pierce = rules ? PierceArmour(*rules) : ui->pierce_armour;
        const int base_melee = base ? MeleeArmour(*base) : melee;
        const int base_pierce = base ? PierceArmour(*base) : pierce;
        // Armour only when there is some.
        if (melee > 0 || pierce > 0) {
            StatLine(sdl_r, layout, row++, kStatArmour,
                     WithBonus(base_melee, melee) + "/" + WithBonus(base_pierce, pierce));
        }
        const int range = rules ? rules->max_range.ToInt() : ui->range;
        const int base_range = base ? base->max_range.ToInt() : range;
        if (range > 0) StatLine(sdl_r, layout, row++, kStatRange, WithBonus(base_range, range));
    }

    // What is left in it, for anything that holds a resource: a tree, a gold
    // mine, a forage bush, a farm.
    if (const auto* held = registry.try_get<ECS::Components::Resource>(selected)) {
        StatLine(sdl_r, layout, row++, StatIconFor(held->type),
                 std::to_string(held->current_amount));
    }

    // What it is carrying: every resource it holds, not just the last one
    // picked up.
    if (const auto* worker = registry.try_get<ECS::Components::Worker>(selected)) {
        for (int r = 0; r < static_cast<int>(Core::ResourceType::COUNT); ++r) {
            if (worker->carried[r] <= 0) continue;
            StatLine(sdl_r, layout, row++, StatIconFor(static_cast<Core::ResourceType>(r)),
                     std::to_string(worker->carried[r]));
        }
    }

    // Who is sheltering inside, of how many it holds.
    if (UIRenderer::ShowsGarrison(registry, selected)) {
        const int room = ECS::Garrisons::Capacity(registry, selected);
        const int inside = ECS::Garrisons::Count(registry, selected);
        StatLine(sdl_r, layout, row++, kStatGarrison,
                 std::to_string(inside) + "/" + std::to_string(room));
    }

    // The production queue, for one of the player's own buildings: what it is
    // making, how far along the first one is, and -- when it is held up for
    // want of houses -- the message saying so.
    UIRenderer::QueueSlot slots[UIRenderer::kMaxQueueSlots];
    const int shown_slots = UIRenderer::BuildQueueSlots(sim, win_w, win_h, slots,
                                                        UIRenderer::kMaxQueueSlots);
    const auto* queue = registry.try_get<ECS::Components::ProductionQueue>(selected);
    if (shown_slots > 0 && queue != nullptr) {
        int mx = 0, my = 0;
        bool held = false;
        Mouse(mx, my, held);
        for (int i = 0; i < shown_slots; ++i) {
            const auto& slot = slots[i];
            const int x = static_cast<int>(slot.rect.x);
            const int y = static_cast<int>(slot.rect.y);
            const bool pressed = held && Inside(slot.rect, mx, my);
            const SDL_Rect icon = {x + 2, y + 2, 36, 36};
            if (slot.tech) {
                if (const auto* tech = Core::Rules::Tech(slot.id)) {
                    DrawIcon(sdl_r, cfg.ResearchIconSlp(), tech->icon, icon, LocalViewer(), 255);
                }
            } else if (const auto* item = ECS::Players::UnitOf(registry, LocalViewer(), slot.id)) {
                DrawIcon(sdl_r, IconSheetFor(slot.id), item->icon, icon, LocalViewer(), 255);
            }
            Bevel(sdl_r, x, y, 40, 40, pressed);
            if (slot.count > 1) {
                Text::DrawShadowed(sdl_r, Text::kArial8, x + 4, y + 3,
                                   std::to_string(slot.count), kWhite, kBlack);
            }
        }
        // Under the first: how far along it is, and the word for what it is
        // doing.
        const int32_t front_ms = ECS::Production::FrontDuration(registry, selected);
        const int bx = static_cast<int>(slots[0].rect.x);
        const int by = static_cast<int>(slots[0].rect.y + slots[0].rect.h) + 2;
        if (front_ms > 0) {
            const int done = std::min(40, static_cast<int>(
                static_cast<int64_t>(queue->progress_ms) * 40 / front_ms));
            Fill(sdl_r, bx, by, 40, 4, kBlack);
            Fill(sdl_r, bx, by, done, 4, kHealthGreen);
        }
        const std::string& doing = Core::Strings::Get(
            queue->items[0].tech ? kStringResearching : kStringCreating);
        Text::Draw(sdl_r, Text::kArial8, bx, by + 6, doing, kBlack);
        if (queue->housed) {
            Text::Draw(sdl_r, Text::kArial8, bx, by + 6 + Text::Height(Text::kArial8),
                       Core::Strings::Get(kStringNeedHouses), kBlack);
        }
        return;   // the queue takes the place of the owner's name
    }

    // The owner, on the right of the field: its civilisation, its name, and
    // to anyone else its standing with them. A tree belongs to nobody, and
    // nothing is printed for it.
    if (owner_of != nullptr && owner != Core::PLAYER_GAIA) {
        const ECS::Players::Player* who = ECS::Players::Find(registry, owner);
        const int civ = who != nullptr ? who->civ : 1;
        Text::Draw(sdl_r, Text::kGeorgia9B, layout.civ.x, layout.civ.y,
                   Core::Strings::Get(kCivNameBase + civ), kBlack);
        Text::Draw(sdl_r, Text::kGeorgia9B, layout.civ.x, layout.civ.y + layout.civ_pitch,
                   PlayerName(registry, owner), kBlack);
        if (owner != LocalViewer()) {
            const ECS::Players::Player* me = ECS::Players::Find(registry, LocalViewer());
            const bool ally = me != nullptr && who != nullptr && me->team != 0 &&
                              me->team == who->team;
            Text::Draw(sdl_r, Text::kGeorgia9B, layout.civ.x,
                       layout.civ.y + 2 * layout.civ_pitch,
                       Core::Strings::Get(ally ? kStringAlly : kStringEnemy), kBlack);
        }
    }
}

// A selected group: a card per unit, its icon filling the card and its hit
// points under it, in up to three rows.
void DrawSelectionCards(SDL_Renderer* sdl_r, ECS::Simulation& sim, int win_w, int win_h) {
    UIRenderer::SelectionCard cards[UIRenderer::kMaxSelectionCards];
    const int n = UIRenderer::BuildSelectionCards(sim, win_w, win_h, cards,
                                                  UIRenderer::kMaxSelectionCards);
    auto& registry = sim.GetRegistry();
    auto& cfg = Core::ConfigManager::Get();
    // Right to left, so where cards overlap the one further left is on top.
    for (int i = n - 1; i >= 0; --i) {
        const auto& card = cards[i];
        if (!registry.valid(card.unit)) continue;
        const auto* owner_of = registry.try_get<ECS::Components::Owner>(card.unit);
        const Core::PlayerId owner = owner_of ? owner_of->player_id : LocalViewer();
        const int slot_id = ECS::Villagers::DisplayUnit(registry, card.unit);
        const int unit_id = ECS::Players::ArtOf(registry, owner, slot_id);
        const auto* ui = cfg.GetUnitInterface(unit_id);
        const SDL_Rect& r = card.rect;
        // The icon at its own size, its hit points over its bottom two rows.
        Fill(sdl_r, r.x + 2, r.y + 2, r.w - 4, r.h - 4, kBlack);
        if (ui) {
            DrawIcon(sdl_r, IconSheetFor(unit_id), ui->icon,
                     SDL_Rect{r.x + 2, r.y + 2, r.w - 4, r.h - 4}, owner, 255);
        }
        if (const auto* health = registry.try_get<ECS::Components::Health>(card.unit)) {
            HealthBar(sdl_r, r.x + 2, r.y + r.h - 4, r.w - 4, 2, health->current_hp,
                      health->max_hp);
        }
        Bevel(sdl_r, r.x, r.y, r.w, r.h, false);
    }
}

// What the cursor is over, or null.
const UIRenderer::Command* HoveredCommand(const UIRenderer::Command* commands, int n,
                                          int mx, int my) {
    for (int i = 0; i < n; ++i) {
        if (Inside(commands[i].rect, mx, my)) return &commands[i];
    }
    return nullptr;
}

} // namespace

void UIRenderer::DrawHelpText(ECS::Simulation& sim, SDL_Renderer* sdl_r, int win_w, int win_h) {
    Hud& hud = HudState();
    int fx = 0, fy = 0;
    bool held = false;
    Mouse(fx, fy, held);
    // The cursor in each part of the interface's terms, at their sizes.
    const HudSpace top = TopSpace(win_w, win_h);
    const HudSpace panel = PanelSpace(win_w, win_h);
    int tx = 0, ty = 0, mx = 0, my = 0;
    top.ToSpace(fx, fy, tx, ty);
    panel.ToSpace(fx, fy, mx, my);

    // What the help says: the button under the cursor, or else what a
    // right-click would do in the world.
    auto& cfg = Core::ConfigManager::Get();
    std::string text;
    Command commands[kCommandSlots];
    const int n = BuildCommands(sim, panel.w, win_h, commands, kCommandSlots);
    if (const Command* hovered = HoveredCommand(commands, n, mx, my)) {
        if (hovered->kind == CommandKind::Research) {
            const auto& texts = Core::Rules::Get().tech_text;
            const auto* player = ECS::Players::Find(sim.GetRegistry(), LocalViewer());
            if (hovered->tech_id >= 0 && hovered->tech_id < static_cast<int>(texts.size()) &&
                player != nullptr) {
                Core::ConfigManager::HelpValues values;
                const auto& status = player->techs[hovered->tech_id];
                StockpileCosts(status.costs, status.cost_count, values.costs);
                // Spies has help of its own, its price as it stands now.
                const std::string help = ECS::Techs::Spies(hovered->tech_id)
                    ? Core::Strings::Get(kSpiesHelp) : texts[hovered->tech_id].help;
                text = cfg.ExpandHelp(help, values, true);
            }
        } else if (hovered->kind == CommandKind::Produce) {
            const auto* ui = cfg.GetUnitInterface(hovered->unit_id);
            const auto* rules =
                ECS::Players::UnitOf(sim.GetRegistry(), LocalViewer(), hovered->unit_id);
            // The help text is a template; the numbers in it are the player's
            // own, after whatever its technologies and civ changed.
            if (ui && !ui->help.empty() && rules) {
                text = cfg.ExpandHelp(ui->help, HelpValuesFor(*rules), true);
            }
        } else if (hovered->kind == CommandKind::Sell || hovered->kind == CommandKind::Buy) {
            // "Click to sell 100 wood for %d gold": the price on the button.
            const std::string help = Core::Strings::Get(hovered->name_string);
            const size_t at = help.find("%d");
            text = at == std::string::npos
                ? help
                : help.substr(0, at) + std::to_string(hovered->label) + help.substr(at + 2);
        } else if (hovered->name_string >= 0) {
            text = Core::Strings::Get(hovered->name_string);
        }
    } else if (const int bar = BarButtonAt(top.w, tx, ty); bar >= 0) {
        static const int kBarHelp[kBarButtonCount] = {4945, 41101, 4913, 4914, 4915};
        text = Core::Strings::Get(kBarHelp[bar]);
    } else if (const int mm = MinimapButtonAt(panel.w, mx, my); mm >= 0) {
        switch (mm) {
        case kSignalAllies: text = Core::Strings::Get(4940); break;
        case kIdleVillager: text = Core::Strings::Get(41108); break;
        case kStatistics: text = Core::Strings::Get(hud.statistics ? 4917 : 4916); break;
        case kHelpToggle: text = Core::Strings::Get(hud.help ? 41110 : 41109); break;
        case kAdvancedCommands: text = Core::Strings::Get(hud.advanced ? 4929 : 4928); break;
        case kNormalMode: text = Core::Strings::Get(41050); break;
        case kCombatMode: text = Core::Strings::Get(41051); break;
        case kEconomicMode: text = Core::Strings::Get(41053); break;
        default: break;
        }
    } else if (hud.world_hint >= 0) {
        text = Core::Strings::Get(hud.world_hint);
    }
    hud.world_hint = -1;
    if (text.empty() || !hud.help) return;

    // White on the world with a shadow, wrapped to the view, its last line
    // just above the panel -- at the Text size, above the panel at its own.
    const HudLayout& layout = Lay(panel.w);
    const int font = Text::Scaled(Text::kGeorgia9B, TextPercent());
    const int left = static_cast<int>(std::lround(layout.help_x * panel.scale));
    std::vector<std::vector<Run>> lines;
    LayoutRich(text, font, win_w - left * 2, lines);
    const int line_h = Text::Height(font);
    int y = static_cast<int>(std::lround(panel.FrameY(static_cast<float>(layout.help_bottom)))) -
            static_cast<int>(lines.size()) * line_h;
    for (const auto& line : lines) {
        int x = left;
        for (const Run& run : line) {
            Text::DrawOutlined(sdl_r, run.font, x, y, run.text, kWhite, kBlack);
            x += Text::Width(run.font, run.text);
        }
        y += line_h;
    }
}

namespace {

// The interface art for this size and civilisation, one part of it -- the
// rows above HudSplit, or those below -- carried out to a frame `w` wide: the
// art left of the part's cut at the left edge, the rest at the right edge,
// and the art's own plain stretch repeated across the gap between (rendering/
// HudLayout.h). In a frame the art's own width, the art 1:1.
void DrawArtPart(SDL_Renderer* sdl_r, ECS::Simulation& sim, int w, int h, bool bar) {
    const HudLayout& layout = Lay(w);
    const HudLayout& base = LayoutForHeight(h);
    const HAL::GraphicAtlas* atlas =
        HAL::GraphicCache::Get().GetInterfaceAtlas(HudArt(layout, LocalCiv(sim.GetRegistry())));
    if (atlas == nullptr || atlas->base == nullptr || atlas->frames.empty()) return;
    const SDL_Rect& src = atlas->frames[0].rect;
    const int split = HudSplit(base);
    const int top = bar ? 0 : split;
    const int rows = bar ? split : src.h - split;
    const int cut = bar ? BarCut(base) : PanelCut(base);
    const int dx = std::max(0, w - src.w);
    const SDL_Rect left_from = {src.x, src.y + top, cut, rows};
    const SDL_Rect left_to = {0, top, cut, rows};
    SDL_RenderCopy(sdl_r, atlas->base, &left_from, &left_to);
    const SDL_Rect right_from = {src.x + cut, src.y + top, src.w - cut, rows};
    const SDL_Rect right_to = {cut + dx, top, src.w - cut, rows};
    SDL_RenderCopy(sdl_r, atlas->base, &right_from, &right_to);
    const SDL_Rect band = bar ? BarBand(base) : PanelBand(base);
    for (int x = 0; x < dx; x += band.w) {
        const int piece = std::min(band.w, dx - x);
        const SDL_Rect from = {src.x + band.x, src.y + band.y, piece, band.h};
        const SDL_Rect to = {cut + x, band.y, piece, band.h};
        SDL_RenderCopy(sdl_r, atlas->base, &from, &to);
    }
}

// A part of the interface, drawn by `draw` in its own terms: straight into the
// frame at 100%; otherwise into a transparent frame of the part's width and
// that scaled into the frame from the part's corner, its rows only.
template <typename Draw>
void DrawPart(SDL_Renderer* sdl_r, const UIRenderer::HudSpace& space, int win_w, Draw&& draw) {
    if (space.scale == 1.0f) {
        draw();
        return;
    }
    struct Canvas {
        SDL_Texture* texture = nullptr;
        int w = 0;
        int h = 0;
    };
    static Canvas canvases[2];
    Canvas& canvas = canvases[space.bottom ? 1 : 0];
    if (canvas.texture == nullptr || canvas.w != space.w || canvas.h != space.h) {
        if (canvas.texture != nullptr) SDL_DestroyTexture(canvas.texture);
        canvas.texture = SDL_CreateTexture(sdl_r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET,
                                           space.w, space.h);
        canvas.w = space.w;
        canvas.h = space.h;
        if (canvas.texture == nullptr) {
            draw();
            return;
        }
        SDL_SetTextureBlendMode(canvas.texture, SDL_BLENDMODE_BLEND);
    }
    SDL_Texture* frame = SDL_GetRenderTarget(sdl_r);
    SDL_SetRenderTarget(sdl_r, canvas.texture);
    SDL_SetRenderDrawColor(sdl_r, 0, 0, 0, 0);
    SDL_RenderClear(sdl_r);
    SDL_SetRenderDrawBlendMode(sdl_r, SDL_BLENDMODE_BLEND);
    g_space = &space;
    draw();
    g_space = nullptr;
    SDL_SetRenderTarget(sdl_r, frame);
    const int split = HudSplit(LayoutForHeight(space.h));
    const SDL_Rect from = space.bottom ? SDL_Rect{0, split, space.w, space.h - split}
                                       : SDL_Rect{0, 0, space.w, split};
    const SDL_FRect to = {0.0f, space.FrameY(static_cast<float>(from.y)), static_cast<float>(win_w),
                          from.h * space.scale};
    // A whole-number size keeps every pixel square; any other is filtered.
    const bool whole = std::fabs(space.scale - std::round(space.scale)) < 0.001f;
    SDL_SetTextureScaleMode(canvas.texture, whole ? SDL_ScaleModeNearest : SDL_ScaleModeLinear);
    SDL_RenderCopyF(sdl_r, canvas.texture, &from, &to);
}

} // namespace

void UIRenderer::DrawInterface(ECS::Simulation& sim, Renderer& renderer, SDL_Renderer* sdl_r,
                               int win_w, int win_h) {
    SDL_SetRenderDrawBlendMode(sdl_r, SDL_BLENDMODE_BLEND);
    const HudSpace panel = PanelSpace(win_w, win_h);
    DrawPart(sdl_r, panel, win_w, [&] {
        DrawArtPart(sdl_r, sim, panel.w, win_h, false);
        DrawCommandPanel(sdl_r, sim, panel.w, win_h);
        SelectionCard probe[2];
        if (BuildSelectionCards(sim, panel.w, win_h, probe, 2) >= 2) {
            DrawSelectionCards(sdl_r, sim, panel.w, win_h);
        } else {
            DrawUnitPanel(sdl_r, sim, panel.w, win_h);
        }
        DrawMinimap(sim, renderer, sdl_r, panel.w, win_h);
    });
    const HudSpace top = TopSpace(win_w, win_h);
    DrawPart(sdl_r, top, win_w, [&] {
        DrawArtPart(sdl_r, sim, top.w, win_h, true);
        const HudLayout& layout = Lay(top.w);
        DrawFrame(sdl_r, kFoodIconSlp, 0, layout.food_icon.x, layout.food_icon.y);
        DrawTopBar(sdl_r, sim, layout);
    });
    DrawHelpText(sim, sdl_r, win_w, win_h);
}

void UIRenderer::DrawCursor(ECS::Simulation& sim, Renderer& renderer, SDL_Renderer* sdl_r) {
    (void)sim;
    (void)renderer;
    int mx = 0;
    int my = 0;
    bool held = false;
    Mouse(mx, my, held);

    // Which cursor: what an order would do here, which the input layer works
    // out as the mouse moves.
    int frame = kCursorNormal;
    switch (Core::InputManager::Get().CursorIntent()) {
    case Core::InputManager::Intent::None: frame = kCursorNormal; break;
    case Core::InputManager::Intent::Attack: frame = kCursorSword; break;
    case Core::InputManager::Intent::Work: frame = kCursorHand; break;
    case Core::InputManager::Intent::Monk: frame = kCursorRing; break;
    case Core::InputManager::Intent::Garrison: frame = kCursorDoor; break;
    case Core::InputManager::Intent::Board: frame = kCursorStairsUp; break;
    case Core::InputManager::Intent::Place: frame = kCursorHammer; break;
    case Core::InputManager::Intent::Patrol: frame = kCursorFlag; break;
    case Core::InputManager::Intent::GatherPoint: frame = kCursorFlag; break;
    case Core::InputManager::Intent::Unload: frame = kCursorStairsDown; break;
    case Core::InputManager::Intent::Follow: frame = kCursorTracks; break;
    case Core::InputManager::Intent::Guard: frame = kCursorShield; break;
    case Core::InputManager::Intent::Signal: frame = kCursorHorn; break;
    }

    // The cursors, from the interface archive, at their true size on the
    // hotspot each frame carries.
    auto& graphics = HAL::GraphicCache::Get();
    const HAL::GraphicAtlas* atlas = graphics.GetInterfaceAtlas(kCursorSlp);
    if (atlas && atlas->base && frame < static_cast<int>(atlas->frames.size())) {
        const HAL::GraphicFrame& f = atlas->frames[frame];
        if (f.rect.w > 0) {
            const SDL_Rect dest = {mx - f.hotspot_x, my - f.hotspot_y, f.rect.w, f.rect.h};
            SDL_RenderCopy(sdl_r, atlas->base, &f.rect, &dest);
            return;
        }
    }

    SDL_SetRenderDrawColor(sdl_r, 255, 255, 255, 255);
    SDL_RenderDrawLine(sdl_r, mx - 10, my, mx + 10, my);
    SDL_RenderDrawLine(sdl_r, mx, my - 10, mx, my + 10);
}

void UIRenderer::DrawFPS(SDL_Renderer* sdl_r, int fps, int win_w) {
    const Frame& frame = CurrentFrame();
    const int top = ViewRect(win_w, frame.h > 0 ? frame.h : win_w * 3 / 4).y;
    const std::string text = std::to_string(fps);
    const int w = Text::Width(Text::kArial8, text);
    Text::DrawShadowed(sdl_r, Text::kArial8, (win_w - w) / 2, top + 4, text,
                       kWhite, kBlack);
}

} // namespace Rendering
