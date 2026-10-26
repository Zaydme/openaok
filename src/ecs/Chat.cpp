#include "ecs/Chat.h"

#include "ecs/Diplomacy.h"
#include "ecs/Events.h"
#include "ecs/Players.h"
#include "ecs/Victory.h"

namespace ECS::Chat {

namespace {

bool InGame(const entt::registry& registry, Core::PlayerId player) {
    if (player == Core::PLAYER_GAIA || player >= Core::kMaxPlayers) return false;
    if (Players::Find(registry, player) == nullptr) return false;
    const Victory::Outcome* outcome = Victory::Read(registry);
    return outcome == nullptr || !outcome->Defeated(player);
}

uint16_t Bit(Core::PlayerId player) {
    return static_cast<uint16_t>(1u << player);
}

} // namespace

void Setup(entt::registry& registry) {
    registry.ctx().insert_or_assign(Log{});
}

int TauntOf(const std::string& text) {
    size_t first = text.find_first_not_of(' ');
    if (first == std::string::npos) return 0;
    const size_t last = text.find_last_not_of(' ');
    int value = 0;
    for (size_t i = first; i <= last; ++i) {
        const char c = text[i];
        if (c < '0' || c > '9') return 0;
        value = value * 10 + (c - '0');
        if (value > kTaunts) return 0;
    }
    return value >= 1 ? value : 0;
}

bool Post(entt::registry& registry, Core::PlayerId from, Audience audience,
          Core::PlayerId player, const std::string& text) {
    Log* log = registry.ctx().find<Log>();
    if (log == nullptr || !InGame(registry, from)) return false;
    if (text.find_first_not_of(' ') == std::string::npos) return false;

    uint16_t to = Bit(from);
    switch (audience) {
    case Audience::Everyone:
        for (int p = 1; p < Core::kMaxPlayers; ++p) {
            if (Players::Find(registry, static_cast<Core::PlayerId>(p)) != nullptr) {
                to = static_cast<uint16_t>(to | Bit(static_cast<Core::PlayerId>(p)));
            }
        }
        break;
    case Audience::Allies:
        for (int p = 1; p < Core::kMaxPlayers; ++p) {
            const auto other = static_cast<Core::PlayerId>(p);
            if (other == from || Players::Find(registry, other) == nullptr) continue;
            if (Diplomacy::StanceOf(registry, from, other) == Diplomacy::Stance::Ally) {
                to = static_cast<uint16_t>(to | Bit(other));
            }
        }
        break;
    case Audience::Player:
        if (player == from || player == Core::PLAYER_GAIA || player >= Core::kMaxPlayers ||
            Players::Find(registry, player) == nullptr) {
            return false;
        }
        if (Diplomacy::Locked(registry) &&
            Diplomacy::StanceOf(registry, from, player) != Diplomacy::Stance::Ally) {
            return false;
        }
        to = static_cast<uint16_t>(to | Bit(player));
        break;
    default:
        return false;
    }

    Line line;
    const Events::Log* events = Events::Read(registry);
    line.tick = events != nullptr ? events->tick : 0;
    line.from = from;
    line.to = to;
    line.audience = audience;
    line.text = text.substr(0, kMostCharacters);
    line.taunt = TauntOf(line.text);
    log->lines.push_back(std::move(line));
    if (log->lines.size() > kKept) {
        log->lines.erase(log->lines.begin(),
                         log->lines.begin() + static_cast<long>(log->lines.size() - kKept));
    }
    return true;
}

const Log* Read(const entt::registry& registry) {
    return registry.ctx().find<Log>();
}

} // namespace ECS::Chat
