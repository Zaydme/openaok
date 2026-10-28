#include "rendering/Messages.h"

#include <algorithm>
#include <cstdio>
#include <deque>

#include "core/ConfigManager.h"
#include "core/GameSetup.h"
#include "core/Strings.h"
#include "ecs/Chat.h"
#include "ecs/Diplomacy.h"
#include "ecs/Events.h"
#include "ecs/Players.h"
#include "ecs/Techs.h"
#include "ecs/Triggers.h"
#include "ecs/Victory.h"
#include "ecs/components/Building.h"
#include "ecs/components/Identity.h"
#include "hal/GraphicCache.h"
#include "rendering/Text.h"
#include "rendering/UIRenderer.h"

namespace Rendering::Messages {

namespace {

// Where the lines go: eight panels 620x20 at x 4, y 20, 40 ... 160 into the
// map view -- 214 further down in a single-player game, below the place a
// scenario's instructions take. The text's cell sits one row into its panel.
constexpr int kFirstTop = 20;
constexpr int kSinglePlayerDrop = 214;
constexpr int kPitch = 20;
constexpr int kLeft = 4;
constexpr int kTextInset = 1;
constexpr int kSlots = 8;
constexpr uint32_t kShowMs = 10000;
constexpr size_t kHistory = 100;

// A scenario's instructions: three panels 622x51 at x 4, y 34, 94 and 154 into
// the map view, each the same line panel as the messages', in Georgia 10
// bold wrapped to the panel. A Display Instructions effect fills one for its
// time in seconds, or until a Clear Instructions empties it when that is 0.
constexpr int kPanels = 3;
constexpr int kPanelTop = 34;
constexpr int kPanelPitch = 60;
constexpr int kPanelW = 622;
constexpr int kPanelH = 51;

// LANGUAGE.DLL's words for what happened.
constexpr int kAdvancedBase = 3035;          // + age: "%s advanced to the Feudal Age."
constexpr int kDefeated = 3014;              // "%s defeated."
constexpr int kYouBeganWonder = 3058;        // "You have started building a Wonder!"
// "You need more houses to continue unit production.", and at the game's
// population limit "Unit production halted because you are at your
// population limit."
constexpr int kNeedMoreHouses = 3075;
constexpr int kHaltedAtLimit = 3076;
constexpr int kSiteObstructed = 3094;        // "Building site obstructed. Building canceled."
// An enemy's Spies: "Spies have infiltrated your nation."; an enemy's
// Treason: "--Warning: Spies have leaked knowledge of your king's
// location!!!--".
constexpr int kSpiesInfiltrated = 3099;
constexpr int kKingLeaked = 37166;
constexpr int kTheyBeganWonder = 3019;       // "%s started building a Wonder!"
constexpr int kYouCompletedWonder = 3020;    // "...You will win if it stands for %d years!"
constexpr int kAllyCompletedWonder = 3021;
constexpr int kEnemyCompletedWonder = 3022;
constexpr int kYourWonderDestroyed = 3023;
constexpr int kTheirWonderDestroyed = 3024;  // "%s's Wonder was destroyed!"
constexpr int kYouHoldRelics = 3028;         // "...Hold them for %d years to win!"
constexpr int kTheyHoldRelics = 3029;
constexpr int kRelicsLost = 3030;
// "%s paid %d gold in tribute to you." and its fellows, by dat resource.
constexpr int kTributeFood = 3016;
constexpr int kTributeWood = 3015;
constexpr int kTributeStone = 3017;
constexpr int kTributeGold = 3009;
// "%s changed diplomatic stance with you to Neutral/Ally/Enemy."
constexpr int kStanceNeutral = 3025;
constexpr int kStanceAlly = 3026;
constexpr int kStanceEnemy = 3027;

struct Line {
    std::string text;
    SDL_Color colour;
    uint32_t until = 0;       // SDL ticks: when it goes
};

struct Instruction {
    std::string text;
    uint32_t until = 0;       // SDL ticks; 0 while it stays until cleared
    bool shown = false;
};

struct State {
    std::deque<Line> lines;   // everything posted, oldest first
    Instruction panels[kPanels];
    uint64_t heard_display = 0;
    bool single_player = false;
    uint64_t heard = 0;
    uint64_t heard_chat = 0;
    Core::PlayerId relic_holder = 0;
    int review = 0;          // how many lines back the review keys have gone
    uint32_t review_at = 0;
};

State& S() {
    static State state;
    return state;
}

// printf with one string or one number, as the lines take them.
std::string Format(int string_id, const std::string& name) {
    char text[256];
    std::snprintf(text, sizeof(text), Core::Strings::Get(string_id).c_str(), name.c_str());
    return text;
}
std::string Format(int string_id, int number) {
    char text[256];
    std::snprintf(text, sizeof(text), Core::Strings::Get(string_id).c_str(), number);
    return text;
}

SDL_Color ColourOf(Core::PlayerId player) {
    const HAL::PaletteColor c = HAL::GraphicCache::Get().GetPlayerColor(player);
    return SDL_Color{c.r, c.g, c.b, 255};
}

bool SameSide(const entt::registry& registry, Core::PlayerId a, Core::PlayerId b) {
    if (a == b) return true;
    const ECS::Players::Player* pa = ECS::Players::Find(registry, a);
    const ECS::Players::Player* pb = ECS::Players::Find(registry, b);
    return pa != nullptr && pb != nullptr && pa->team != 0 && pa->team == pb->team;
}

bool IsWonder(const entt::registry& registry, entt::entity e) {
    const auto* id = registry.try_get<ECS::Components::Identity>(e);
    if (id == nullptr || !registry.all_of<ECS::Components::Building>(e)) return false;
    return id->unit_id == Core::ConfigManager::Get().GetUnitIdByName("WNDR");
}

int Years(const entt::registry& registry, bool wonder) {
    const auto* setup = registry.ctx().find<Core::GameSetup>();
    if (setup == nullptr) return 0;
    return wonder ? setup->wonder_years : setup->relic_years;
}

void Hear(const entt::registry& registry, const ECS::Events::Event& event,
          Core::PlayerId viewer) {
    using ECS::Events::Kind;
    switch (event.kind) {
    case Kind::Researched:
        if (event.player != viewer && ECS::Techs::Spies(event.id) &&
            ECS::Diplomacy::StanceOf(registry, event.player, viewer) == ECS::Diplomacy::Stance::Enemy) {
            Post(Core::Strings::Get(kSpiesInfiltrated));
        }
        for (int age = 1; age <= 3; ++age) {
            if (event.id == ECS::Techs::AgeTech(registry, event.player, age)) {
                Post(Format(kAdvancedBase + age, UIRenderer::PlayerName(registry, event.player)));
            }
        }
        break;
    case Kind::Begun:
        if (event.id == Core::ConfigManager::Get().GetUnitIdByName("WNDR")) {
            if (event.player == viewer) {
                Post(Core::Strings::Get(kYouBeganWonder));
            } else {
                Post(Format(kTheyBeganWonder, UIRenderer::PlayerName(registry, event.player)));
            }
        }
        break;
    case Kind::Treason:
        if (event.player != viewer &&
            ECS::Diplomacy::StanceOf(registry, event.player, viewer) == ECS::Diplomacy::Stance::Enemy) {
            Post(Core::Strings::Get(kKingLeaked));
        }
        break;
    case Kind::Obstructed:
        if (event.player == viewer) Post(Core::Strings::Get(kSiteObstructed));
        break;
    case Kind::Halted:
        if (event.player == viewer) {
            Post(Core::Strings::Get(event.amount != 0 ? kHaltedAtLimit : kNeedMoreHouses));
        }
        break;
    case Kind::Built:
        if (registry.valid(event.subject) && IsWonder(registry, event.subject)) {
            const int years = Years(registry, true);
            if (event.player == viewer) {
                Post(Format(kYouCompletedWonder, years));
            } else {
                // "%s completed a Wonder. You will win/lose if it stands for
                // %d years!": a name and then a number.
                char text[256];
                std::snprintf(text, sizeof(text),
                              Core::Strings::Get(SameSide(registry, event.player, viewer)
                                                     ? kAllyCompletedWonder
                                                     : kEnemyCompletedWonder).c_str(),
                              UIRenderer::PlayerName(registry, event.player).c_str(), years);
                Post(text);
            }
        }
        break;
    case Kind::Died:
        if (registry.valid(event.subject) && IsWonder(registry, event.subject)) {
            if (event.player == viewer) {
                Post(Core::Strings::Get(kYourWonderDestroyed));
            } else {
                Post(Format(kTheirWonderDestroyed, UIRenderer::PlayerName(registry, event.player)));
            }
        }
        break;
    case Kind::Defeated:
        Post(Format(kDefeated, UIRenderer::PlayerName(registry, event.player)));
        break;
    case Kind::Tribute:
        if (event.player == viewer) {
            const int line = event.id == 0 ? kTributeFood : event.id == 1 ? kTributeWood
                           : event.id == 2 ? kTributeStone : kTributeGold;
            char text[256];
            std::snprintf(text, sizeof(text), Core::Strings::Get(line).c_str(),
                          UIRenderer::PlayerName(registry, event.from).c_str(), event.amount);
            Post(text);
        }
        break;
    case Kind::Stance:
        // Another player's stance toward this one; an allied-victory toggle
        // (`from` the player itself) says nothing.
        if (event.from == viewer && event.player != viewer) {
            const int line = event.id == 0 ? kStanceAlly : event.id == 1 ? kStanceNeutral
                                                                         : kStanceEnemy;
            Post(Format(line, UIRenderer::PlayerName(registry, event.player)));
        }
        break;
    default:
        break;
    }
}

} // namespace

void Reset() {
    S() = State{};
}

void Post(const std::string& text, SDL_Color colour) {
    State& s = S();
    const uint32_t now = SDL_GetTicks();
    uint32_t until = now + kShowMs;
    // The same words still showing in a panel the new line leaves them: the
    // new line goes when they do.
    const size_t kept = std::min(s.lines.size(), static_cast<size_t>(kSlots - 1));
    for (size_t i = s.lines.size() - kept; i < s.lines.size(); ++i) {
        const Line& line = s.lines[i];
        if (line.text == text && static_cast<int32_t>(line.until - now) > 0) {
            until = line.until;
            break;
        }
    }
    s.lines.push_back(Line{text, colour, until});
    while (s.lines.size() > kHistory) s.lines.pop_front();
    s.review = 0;
}

void Chat(const entt::registry& registry, Core::PlayerId from, const std::string& words) {
    Post(UIRenderer::PlayerName(registry, from) + ": " + words, ColourOf(from));
}

void Update(const entt::registry& registry, Core::PlayerId viewer) {
    State& s = S();
    if (const ECS::Events::Log* log = ECS::Events::Read(registry)) {
        for (const ECS::Events::Event& event : log->events) {
            if (event.tick <= s.heard) continue;
            Hear(registry, event, viewer);
        }
        s.heard = log->tick;
    }
    // What the players say: the lines this player hears, in the speaker's
    // colour.
    if (const ECS::Chat::Log* chat = ECS::Chat::Read(registry)) {
        uint64_t latest = s.heard_chat;
        for (const ECS::Chat::Line& line : chat->lines) {
            if (line.tick <= s.heard_chat) continue;
            latest = std::max(latest, line.tick);
            if (line.Heard(viewer)) Chat(registry, line.from, line.text);
        }
        s.heard_chat = latest;
    }
    // A scenario's triggers: its instructions, and the chat it sends -- the
    // whole line its own, in the colour of the player it is sent to (no
    // name), heard by that player alone.
    if (const ECS::Triggers::Display* display = ECS::Triggers::ReadDisplay(registry)) {
        uint64_t latest = s.heard_display;
        for (const ECS::Triggers::Shown& line : display->lines) {
            if (line.tick <= s.heard_display) continue;
            latest = std::max(latest, line.tick);
            const int panel = std::clamp(line.panel, 0, kPanels - 1);
            switch (line.kind) {
            case ECS::Triggers::Show::Instructions: {
                Instruction& i = s.panels[panel];
                i.text = line.text;
                i.shown = true;
                i.until = line.seconds > 0 ? SDL_GetTicks() + static_cast<uint32_t>(line.seconds) * 1000u : 0;
                break;
            }
            case ECS::Triggers::Show::ClearInstructions:
                s.panels[panel] = Instruction{};
                break;
            case ECS::Triggers::Show::Chat:
                if (line.player == 0) Post(line.text);
                else if (line.player == viewer) Post(line.text, ColourOf(line.player));
                break;
            default:
                break;
            }
        }
        s.heard_display = latest;
    }
    // The relics changing hands is the victory module's state, not an event.
    if (const ECS::Victory::Outcome* outcome = ECS::Victory::Read(registry)) {
        if (outcome->relic_holder != s.relic_holder) {
            if (outcome->relic_holder == 0) {
                Post(Core::Strings::Get(kRelicsLost));
            } else {
                const bool ours = SameSide(registry, outcome->relic_holder, viewer);
                Post(Format(ours ? kYouHoldRelics : kTheyHoldRelics, Years(registry, false)));
            }
            s.relic_holder = outcome->relic_holder;
        }
    }
}

void Review(int step) {
    State& s = S();
    const int most = static_cast<int>(s.lines.size()) - 1;
    s.review = std::clamp(s.review + step, 0, std::max(0, most));
    s.review_at = SDL_GetTicks();
}

void Draw(SDL_Renderer* renderer, int win_w, int win_h) {
    State& s = S();
    const uint32_t now = SDL_GetTicks();
    const SDL_Color outline{0, 0, 0, 255};
    const SDL_Rect view = UIRenderer::ViewRect(win_w, win_h);
    // At the Options' Text size: the faces drawn afresh at it, and the
    // layout's distances grown with them.
    const int percent = UIRenderer::TextPercent();
    auto sized = [percent](int pixels) { return (pixels * percent + 50) / 100; };
    const int instruction_font = Text::Scaled(Text::kGeorgia10B, percent);
    const int line_font = Text::Scaled(Text::kGeorgia9B, percent);
    // The scenario's instructions, each panel's lines from its top, white
    // with the outline of text over the world.
    for (int k = 0; k < kPanels; ++k) {
        Instruction& i = s.panels[k];
        if (!i.shown) continue;
        if (i.until != 0 && static_cast<int32_t>(now - i.until) >= 0) {
            i = Instruction{};
            continue;
        }
        std::vector<std::string_view> lines;
        Text::Wrap(instruction_font, i.text, std::min(sized(kPanelW), view.w - 2 * sized(kLeft)), lines);
        const int lh = Text::Height(instruction_font);
        const int top = view.y + sized(kPanelTop + k * kPanelPitch);
        for (size_t n = 0; n < lines.size() && static_cast<int>(n + 1) * lh <= sized(kPanelH) + lh / 2; ++n) {
            Text::DrawOutlined(renderer, instruction_font, view.x + sized(kLeft),
                               top + sized(kTextInset) + static_cast<int>(n) * lh, lines[n],
                               SDL_Color{255, 255, 255, 255}, outline);
        }
    }
    const bool reviewing = s.review > 0 && now - s.review_at < kShowMs;
    if (!reviewing) s.review = 0;
    const int top = view.y + sized(kFirstTop + kTextInset + (s.single_player ? kSinglePlayerDrop : 0));
    const int pitch = sized(kPitch);
    const int left = sized(kLeft);
    // The newest line in the last panel and the ones before it above, as
    // many as fit; reviewing, from the line the keys have stepped back to.
    const int newest = static_cast<int>(s.lines.size()) - 1 - (reviewing ? s.review : 0);
    for (int k = 0; k < kSlots && newest - k >= 0; ++k) {
        const Line& line = s.lines[static_cast<size_t>(newest - k)];
        if (!reviewing && static_cast<int32_t>(line.until - now) <= 0) continue;
        Text::DrawOutlined(renderer, line_font, left, top + (kSlots - 1 - k) * pitch, line.text,
                           line.colour, outline);
    }
}

void SetSinglePlayer(bool single) {
    S().single_player = single;
}

void CatchUp(const entt::registry& registry) {
    State& s = S();
    if (const ECS::Events::Log* log = ECS::Events::Read(registry)) s.heard = log->tick;
    if (const ECS::Chat::Log* chat = ECS::Chat::Read(registry)) {
        for (const ECS::Chat::Line& line : chat->lines) s.heard_chat = std::max(s.heard_chat, line.tick);
    }
    if (const ECS::Triggers::Display* display = ECS::Triggers::ReadDisplay(registry)) {
        for (const ECS::Triggers::Shown& line : display->lines) {
            s.heard_display = std::max(s.heard_display, line.tick);
        }
    }
    if (const ECS::Victory::Outcome* outcome = ECS::Victory::Read(registry)) {
        s.relic_holder = outcome->relic_holder;
    }
}

} // namespace Rendering::Messages
