#include "ecs/Cheats.h"

#include <cctype>
#include <string>

#include "core/ConfigManager.h"
#include "core/GameSetup.h"
#include "ecs/Buildings.h"
#include "ecs/Combat.h"
#include "ecs/Events.h"
#include "ecs/Navigation.h"
#include "ecs/Players.h"
#include "ecs/Units.h"
#include "ecs/Victory.h"
#include "ecs/Vision.h"
#include "ecs/components/Building.h"
#include "ecs/components/Identity.h"
#include "ecs/components/Owner.h"
#include "ecs/components/Transform.h"

namespace ECS::Cheats {

namespace {

// Who has the cheats that stay on until typed again.
struct Toggles {
    uint16_t aegis = 0;
    uint16_t nature = 0;
};

// What each typed line is.
struct Spelling {
    const char* text;
    int code;
};
constexpr Spelling kCodes[] = {
    {"cheese steak jimmy's", kFood},
    {"lumberjack", kWood},
    {"robin hood", kGold},
    {"rock on", kStone},
    {"marco", kMarco},
    {"polo", kPolo},
    {"aegis", kAegis},
    {"natural wonders", kNaturalWonders},
    {"i r winner", kWinner},
    {"black death", kBlackDeath},
    {"wimpywimpywimpy", kWimpy},
    {"how do you turn this on", kCobra},
    {"to smithereens", kSmithereens},
};

constexpr int kStockCheat = 1000;

uint16_t Bit(Core::PlayerId player) { return static_cast<uint16_t>(1u << (player & 15)); }

Toggles& TogglesOf(entt::registry& registry) {
    if (auto* t = registry.ctx().find<Toggles>()) return *t;
    return registry.ctx().emplace<Toggles>();
}

// A unit of `name` for `player`, stepping out of its first Town Center (or
// beside its first building, if it has none left).
void Gift(entt::registry& registry, const Physics::Grid& grid, Core::PlayerId player,
          const char* name) {
    auto& cfg = Core::ConfigManager::Get();
    const int unit_id = cfg.GetUnitIdByName(name);
    if (unit_id < 0) return;
    const int town_center = cfg.GetUnitIdByName("RTWC");
    entt::entity home = entt::null;
    bool home_is_tc = false;
    auto buildings = registry.view<const Components::Building, const Components::Identity,
                                   const Components::Owner>();
    for (auto b : buildings) {
        if (buildings.get<const Components::Owner>(b).player_id != player) continue;
        const bool tc = buildings.get<const Components::Identity>(b).unit_id == town_center;
        if (home == entt::null || (tc && !home_is_tc) || (tc == home_is_tc && b < home)) {
            home = b;
            home_is_tc = tc;
        }
    }
    if (home == entt::null) return;
    Components::Vector2D at{};
    if (Buildings::Doorstep(registry, grid, home, at,
                            Navigation::LayerOfUnit(registry, grid, player, unit_id))) {
        Units::Spawn(registry, unit_id, player, at);
    }
}

bool Allowed(const entt::registry& registry) {
    const auto* setup = registry.ctx().find<Core::GameSetup>();
    return setup != nullptr && setup->cheats;
}

bool Active(const entt::registry& registry, Core::PlayerId player) {
    const auto* setup = registry.ctx().find<Core::GameSetup>();
    return setup != nullptr && player > 0 && player < Core::kMaxPlayers &&
           setup->players[player].active;
}

} // namespace

int Recognise(std::string_view chat) {
    size_t begin = 0, end = chat.size();
    while (begin < end && std::isspace(static_cast<unsigned char>(chat[begin]))) ++begin;
    while (end > begin && std::isspace(static_cast<unsigned char>(chat[end - 1]))) --end;
    std::string line;
    for (size_t i = begin; i < end; ++i) {
        line += static_cast<char>(std::tolower(static_cast<unsigned char>(chat[i])));
    }
    for (const Spelling& s : kCodes) {
        if (line == s.text) return s.code;
    }
    constexpr std::string_view kTorpedoWord = "torpedo";
    if (line.size() == kTorpedoWord.size() + 1 &&
        line.compare(0, kTorpedoWord.size(), kTorpedoWord) == 0) {
        const char n = line.back();
        if (n >= '1' && n <= '8') return kTorpedo + (n - '0');
    }
    return kNone;
}

bool Apply(entt::registry& registry, Physics::Grid& grid, Core::PlayerId player, int code) {
    if (!Allowed(registry) || !Active(registry, player)) return false;
    Players::Player* me = Players::Find(registry, player);
    if (me == nullptr) return false;
    switch (code) {
    case kFood: Players::AddStock(*me, Core::ResourceType::Food, kStockCheat); break;
    case kWood: Players::AddStock(*me, Core::ResourceType::Wood, kStockCheat); break;
    case kGold: Players::AddStock(*me, Core::ResourceType::Gold, kStockCheat); break;
    case kStone: Players::AddStock(*me, Core::ResourceType::Stone, kStockCheat); break;
    case kMarco: Vision::ToggleExploreAll(registry, player); break;
    case kPolo: Vision::ToggleSeeAll(registry, player); break;
    case kAegis: TogglesOf(registry).aegis ^= Bit(player); break;
    case kNaturalWonders: TogglesOf(registry).nature ^= Bit(player); break;
    case kWinner: Victory::Win(registry, player); break;
    case kWimpy: Victory::Resign(registry, grid, player); break;
    case kBlackDeath: {
        // Every player still in the game who is not on the cheater's side.
        const Players::Player* mine = Players::Find(registry, player);
        for (int p = 1; p < Core::kMaxPlayers; ++p) {
            const auto other = static_cast<Core::PlayerId>(p);
            if (other == player || !Active(registry, other)) continue;
            const Players::Player* theirs = Players::Find(registry, other);
            if (mine != nullptr && theirs != nullptr && mine->team != 0 &&
                mine->team == theirs->team) {
                continue;
            }
            Victory::Resign(registry, grid, other);
        }
        break;
    }
    case kCobra: Gift(registry, grid, player, "COBRA"); break;
    case kSmithereens: Gift(registry, grid, player, "HDSQD"); break;
    default:
        if (code > kTorpedo && code <= kTorpedo + 8) {
            const auto victim = static_cast<Core::PlayerId>(code - kTorpedo);
            if (victim != player && Active(registry, victim)) Victory::Resign(registry, grid, victim);
            break;
        }
        return false;
    }
    Events::Emit(registry, {.kind = Events::Kind::Cheat, .player = player, .id = code});
    return true;
}

bool Aegis(const entt::registry& registry, Core::PlayerId player) {
    const auto* t = registry.ctx().find<Toggles>();
    return t != nullptr && (t->aegis & Bit(player)) != 0;
}

bool CommandsNature(const entt::registry& registry, Core::PlayerId player) {
    const auto* t = registry.ctx().find<Toggles>();
    return t != nullptr && (t->nature & Bit(player)) != 0;
}

} // namespace ECS::Cheats
