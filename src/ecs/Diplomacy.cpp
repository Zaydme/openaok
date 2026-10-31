#include "ecs/Diplomacy.h"

#include <vector>

#include "core/Rules.h"
#include "ecs/Effects.h"
#include "ecs/Events.h"
#include "ecs/Players.h"

namespace ECS::Diplomacy {

namespace {

bool Seated(const entt::registry& registry, Core::PlayerId player) {
    return player != Core::PLAYER_GAIA && player < Core::kMaxPlayers &&
           Players::Find(registry, player) != nullptr;
}

} // namespace

void Setup(entt::registry& registry, const Core::GameSetup& setup) {
    Table table;
    table.locked = setup.lock_teams;
    for (int from = 0; from < Core::kMaxPlayers; ++from) {
        for (int to = 0; to < Core::kMaxPlayers; ++to) {
            const Core::TeamId a = setup.players[from].team;
            const Core::TeamId b = setup.players[to].team;
            const bool mates = from == to ||
                (from != Core::PLAYER_GAIA && to != Core::PLAYER_GAIA && a != 0 && a == b);
            table.stance[from][to] = mates ? Stance::Ally : Stance::Enemy;
        }
        table.allied_victory[from] = from != Core::PLAYER_GAIA && setup.players[from].team != 0;
    }
    registry.ctx().insert_or_assign(table);
}

Stance StanceOf(const entt::registry& registry, Core::PlayerId from, Core::PlayerId to) {
    if (from == to) return Stance::Ally;
    if (from == Core::PLAYER_GAIA || to == Core::PLAYER_GAIA) return Stance::Enemy;
    if (from >= Core::kMaxPlayers || to >= Core::kMaxPlayers) return Stance::Enemy;
    if (const Table* table = registry.ctx().find<Table>()) return table->stance[from][to];
    // A world built without a lobby's table: team-mates by the players'
    // own record, everyone else an enemy.
    const Players::Player* a = Players::Find(registry, from);
    const Players::Player* b = Players::Find(registry, to);
    return a != nullptr && b != nullptr && a->team != 0 && a->team == b->team ? Stance::Ally
                                                                              : Stance::Enemy;
}

bool Allied(const entt::registry& registry, Core::PlayerId a, Core::PlayerId b) {
    return StanceOf(registry, a, b) == Stance::Ally && StanceOf(registry, b, a) == Stance::Ally;
}

bool AlliedVictory(const entt::registry& registry, Core::PlayerId player) {
    if (player >= Core::kMaxPlayers) return false;
    const Table* table = registry.ctx().find<Table>();
    if (table != nullptr) return table->allied_victory[player];
    const Players::Player* p = Players::Find(registry, player);
    return p != nullptr && p->team != 0;
}

bool Locked(const entt::registry& registry) {
    const Table* table = registry.ctx().find<Table>();
    return table == nullptr || table->locked;
}

bool SetStance(entt::registry& registry, Core::PlayerId player, Core::PlayerId other,
               Stance stance) {
    Table* table = registry.ctx().find<Table>();
    if (table == nullptr || table->locked || player == other) return false;
    if (!Seated(registry, player) || !Seated(registry, other)) return false;
    if (stance != Stance::Ally && stance != Stance::Neutral && stance != Stance::Enemy) return false;
    if (table->stance[player][other] == stance) return true;
    table->stance[player][other] = stance;
    Events::Emit(registry, {.kind = Events::Kind::Stance,
                            .player = player,
                            .id = static_cast<int>(stance),
                            .from = other});
    return true;
}

bool SetAlliedVictory(entt::registry& registry, Core::PlayerId player, bool on) {
    Table* table = registry.ctx().find<Table>();
    if (table == nullptr || table->locked || !Seated(registry, player)) return false;
    if (table->allied_victory[player] == on) return true;
    table->allied_victory[player] = on;
    Events::Emit(registry, {.kind = Events::Kind::Stance,
                            .player = player,
                            .id = on ? 1 : 0,
                            .from = player});
    return true;
}

bool SharesVictory(const entt::registry& registry, Core::PlayerId a, Core::PlayerId b) {
    if (a == b) return true;
    return Allied(registry, a, b) && AlliedVictory(registry, a) && AlliedVictory(registry, b);
}

void ApplyTeamBonuses(entt::registry& registry, const Core::GameSetup& setup) {
    for (int seat = 1; seat < Core::kMaxPlayers; ++seat) {
        const Core::PlayerSetup& chair = setup.players[seat];
        if (!chair.active || chair.team == 0) continue;
        std::vector<int> applied;
        for (int mate = 1; mate < Core::kMaxPlayers; ++mate) {
            const Core::PlayerSetup& other = setup.players[mate];
            if (!other.active || other.team != chair.team) continue;
            const Core::Rules::CivRules* civ = Core::Rules::Civ(other.civ);
            if (civ == nullptr || civ->team_bonus < 0) continue;
            bool seen = false;
            for (int id : applied) seen = seen || id == civ->team_bonus;
            if (seen) continue;
            applied.push_back(civ->team_bonus);
            Effects::Apply(registry, static_cast<Core::PlayerId>(seat), civ->team_bonus);
        }
    }
}

} // namespace ECS::Diplomacy
