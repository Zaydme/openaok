#pragma once
#include "ai/Interpreter.h"
#include "ai/Script.h"

// The script facts, tested against the world.
//
// Each reads one thing: the player's own census (ai/Census), every seat's
// counts (ai/World), the brain's mind (goals, timers, escrow), or the lobby.
// Walls laid by the rules, spies and scenario triggers are answered as if
// there were none.
namespace AI::Facts {

bool Test(Turn& turn, const Script::Expr& fact);

// Whether `player` is one a player argument picks out -- a seat number, a
// wildcard's criteria, a rule variable's player.
bool Matches(const Turn& turn, int encoded, int player);

// Calls `each` for every player an action's player argument names: a seat,
// the rule variable's player, the first player an any-* wildcard picks out,
// every player an every-* one does.
template <class F>
void ForEach(Turn& turn, int encoded, F&& each) {
    if (encoded >= 0 && encoded < Core::kMaxPlayers) {
        each(encoded);
        return;
    }
    if (encoded >= Lang::kVariableBase) {
        const int p = turn.vars[encoded - Lang::kVariableBase];
        if (p > 0) each(p);
        return;
    }
    if (encoded >= Lang::kWildcardBase) {
        const auto who = static_cast<Lang::Who>(encoded - Lang::kWildcardBase);
        for (int p = 1; p < Core::kMaxPlayers; ++p) {
            if (!Matches(turn, encoded, p)) continue;
            each(p);
            if (!Lang::IsEvery(who)) return;
        }
    }
}

// The player's stance toward `other`, and `other`'s toward the player.
Lang::Stance StanceToward(const Turn& turn, int other);
Lang::Stance StanceOf(const Turn& turn, int other);

// A player's score total, as the game keeps it.
int ScoreOf(const Turn& turn, int player);

// The player's finished Market (the first, if several), or none.
entt::entity OwnMarket(const Turn& turn);

// How `from` regards `to` in the game's diplomacy, in the rules' words.
Lang::Stance StanceIn(const entt::registry& registry, Core::PlayerId from, Core::PlayerId to);
// The rules' stance as the game's diplomacy numbers it.
uint8_t DiplomacyStance(int stance);

} // namespace AI::Facts
