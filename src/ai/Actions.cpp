#include "ai/Actions.h"

#include <charconv>

#include "ai/Executor.h"
#include "ai/Facts.h"
#include "ai/Things.h"
#include "ai/Walls.h"
#include "core/Strings.h"
#include "ecs/Chat.h"
#include "ecs/Triggers.h"

namespace AI::Actions {

namespace {

using Lang::Action;

// The computer players' chat kept for the interface: the latest lines.
constexpr size_t kChatKept = 64;

const Lang::Thing& ThingOf(int32_t index) { return Lang::Things()[index]; }

int Civ(const Turn& turn) {
    return turn.world != nullptr ? turn.world->seats[turn.player].civ
                                 : (turn.setup != nullptr ? turn.setup->players[turn.player].civ : 0);
}

uint16_t Seats(Turn& turn, int encoded) {
    uint16_t mask = 0;
    Facts::ForEach(turn, encoded, [&](int p) { mask = static_cast<uint16_t>(mask | (1u << p)); });
    return mask;
}

// Everyone but the player, or those the player stands toward as `stances`
// says (a bit per Lang::Stance).
uint16_t Others(const Turn& turn, int stances) {
    uint16_t mask = 0;
    for (int p = 1; p < Core::kMaxPlayers; ++p) {
        if (p == turn.player || turn.world == nullptr || !turn.world->seats[p].valid) continue;
        const int stance = static_cast<int>(Facts::StanceToward(turn, p));
        if (stances & (1 << stance)) mask = static_cast<uint16_t>(mask | (1u << p));
    }
    return mask;
}

constexpr int kAll = 0b111;
constexpr int kAllies = 1 << static_cast<int>(Lang::Stance::Ally);
constexpr int kNotAllies = (1 << static_cast<int>(Lang::Stance::Neutral)) |
                           (1 << static_cast<int>(Lang::Stance::Enemy));

int Ranged(Turn& turn, int start, int range) {
    if (range <= 1 || turn.brain == nullptr) return start;
    return start + static_cast<int>(turn.brain->rng.NextInt(static_cast<uint32_t>(range)));
}

void ChatId(Turn& turn, uint16_t to, int id) { Chat(turn, to, id, {}); }

} // namespace

void Chat(Turn& turn, uint16_t to, int string_id, const std::string& text) {
    if (turn.brains == nullptr) return;
    ChatLine line{turn.now_ms, turn.player, to, string_id, text};
    // A message that is a number is a taunt. Computer players taunt each
    // other with such strings (LANGUAGE.DLL 22400 is "218", 22406 "201").
    const std::string& said = string_id >= 0 ? Core::Strings::Get(string_id) : text;
    int taunt = -1;
    const auto r = std::from_chars(said.data(), said.data() + said.size(), taunt);
    const bool is_taunt = r.ec == std::errc() && r.ptr == said.data() + said.size() && taunt >= 0 && taunt < 256;
    if (is_taunt) {
        for (Brain& b : turn.brains->brains) {
            if (b.player == turn.player || !(to & (1u << b.player))) continue;
            b.mind.taunts[turn.player].set(static_cast<size_t>(taunt));
        }
    }
    auto& chat = turn.brains->chat;
    chat.push_back(std::move(line));
    if (chat.size() > kChatKept) chat.erase(chat.begin());
    // And into the game's chat, for the people it is said to: everyone, one
    // player, or the speaker's allies.
    if (turn.registry == nullptr || turn.grid == nullptr || turn.world == nullptr) return;
    uint16_t others = 0;
    for (int p = 1; p < Core::kMaxPlayers; ++p) {
        if (p != turn.player && turn.world->seats[p].valid) others = static_cast<uint16_t>(others | (1u << p));
    }
    const uint16_t heard = static_cast<uint16_t>(to & others);
    if (heard == 0) return;
    Core::ChatCommand command{0, 0, said, turn.player};
    if (heard != others) {
        if ((heard & (heard - 1)) == 0) {
            command.audience = static_cast<uint8_t>(ECS::Chat::Audience::Player);
            while (!((heard >> command.to) & 1u)) ++command.to;
        } else {
            command.audience = static_cast<uint8_t>(ECS::Chat::Audience::Allies);
        }
    }
    Give(*turn.registry, *turn.grid, command);
}

void Run(Turn& turn, const Script::Act& act, bool& disable_self) {
    const int32_t* a = act.args;
    Mind& mind = *turn.mind;
    const auto text = [&](int32_t index) -> const std::string& { return turn.program->strings[static_cast<size_t>(index)]; };
    switch (static_cast<Action>(act.id)) {
    case Action::DoNothing:
        break;
    case Action::DisableSelf:
        disable_self = true;
        break;

    // --- The rules' own memory.
    case Action::SetGoal:
        if (a[0] > 0 && a[0] < Lang::kGoals) mind.goals[static_cast<size_t>(a[0])] = a[1];
        break;
    case Action::SetSharedGoal:
        if (turn.brains != nullptr && a[0] >= 0 && a[0] < Lang::kSharedGoals) {
            turn.brains->shared_goals[static_cast<size_t>(a[0])] = a[1];
        }
        break;
    case Action::SetStrategicNumber:
        SetStrategic(mind, static_cast<Lang::SN>(a[0]), a[1], turn.setup);
        break;
    case Action::EnableTimer:
        if (a[0] > 0 && a[0] < Lang::kTimers) {
            mind.timers[static_cast<size_t>(a[0])] = {true, turn.now_ms + static_cast<int64_t>(a[1]) * 1000};
        }
        break;
    case Action::DisableTimer:
        if (a[0] > 0 && a[0] < Lang::kTimers) mind.timers[static_cast<size_t>(a[0])].on = false;
        break;
    case Action::GenerateRandomNumber:
        if (turn.brain != nullptr && a[0] > 0) {
            mind.random_number = 1 + static_cast<int>(turn.brain->rng.NextInt(static_cast<uint32_t>(a[0])));
        }
        break;
    case Action::SetEscrowPercentage:
        mind.escrow_percent[static_cast<size_t>(a[0])] = std::clamp(a[1], 0, 100);
        break;
    case Action::ReleaseEscrow:
        mind.escrow[static_cast<size_t>(a[0])] = 0;
        break;
    case Action::SetDifficultyParameter:
        mind.difficulty_parameters[static_cast<size_t>(a[0])] = std::clamp(a[1], 0, 100);
        if (turn.registry != nullptr && turn.grid != nullptr) {
            Give(*turn.registry, *turn.grid,
                 Core::DifficultyCommand{static_cast<uint8_t>(a[0]), static_cast<uint8_t>(std::clamp(a[1], 0, 100)),
                                         turn.player});
        }
        break;
    case Action::SetDoctrine:
        mind.doctrine = a[0];
        break;
    case Action::SetSignal:
        if (a[0] >= 0 && a[0] < Lang::kSignals) mind.signals.set(static_cast<size_t>(a[0]));
        // A scenario's "AI Signal" condition waits on it.
        if (turn.registry != nullptr && a[0] >= 0 && a[0] < Lang::kSignals) {
            ECS::Triggers::RaiseSignal(*turn.registry, a[0]);
        }
        break;
    case Action::AcknowledgeEvent:
        if (a[1] >= 0 && a[1] < Lang::kEvents) mind.events.reset(static_cast<size_t>(a[1]));
        break;
    case Action::AcknowledgeTaunt:
        if (a[1] >= 0 && a[1] < 256) {
            Facts::ForEach(turn, a[0], [&](int p) { mind.taunts[static_cast<size_t>(p)].reset(static_cast<size_t>(a[1])); });
        }
        break;
    case Action::SetStance:
        // The player's own stance: an order to the game's diplomacy, which
        // refuses it while teams are locked. The rules see what came of it.
        Facts::ForEach(turn, a[0], [&](int p) {
            if (p == turn.player) return;
            if (turn.registry == nullptr || turn.grid == nullptr) {
                mind.stance[static_cast<size_t>(p)] = static_cast<uint8_t>(a[1]);
                return;
            }
            Give(*turn.registry, *turn.grid,
                 Core::DiplomacyCommand{static_cast<Core::PlayerId>(p), Facts::DiplomacyStance(a[1]),
                                        turn.player});
            mind.stance[static_cast<size_t>(p)] = static_cast<uint8_t>(
                Facts::StanceIn(*turn.registry, turn.player, static_cast<Core::PlayerId>(p)));
        });
        break;

    // --- Talking.
    case Action::ChatLocal:
    case Action::ChatLocalToSelf:
        Chat(turn, 0, -1, text(a[0]));
        break;
    case Action::ChatLocalUsingId:     ChatId(turn, 0, a[0]); break;
    case Action::ChatLocalUsingRange:  ChatId(turn, 0, Ranged(turn, a[0], a[1])); break;
    case Action::ChatToAll:            Chat(turn, Others(turn, kAll), -1, text(a[0])); break;
    case Action::ChatToAllUsingId:     ChatId(turn, Others(turn, kAll), a[0]); break;
    case Action::ChatToAllUsingRange:  ChatId(turn, Others(turn, kAll), Ranged(turn, a[0], a[1])); break;
    case Action::ChatToAllies:         Chat(turn, Others(turn, kAllies), -1, text(a[0])); break;
    case Action::ChatToAlliesUsingId:  ChatId(turn, Others(turn, kAllies), a[0]); break;
    case Action::ChatToAlliesUsingRange: ChatId(turn, Others(turn, kAllies), Ranged(turn, a[0], a[1])); break;
    case Action::ChatToEnemies:        Chat(turn, Others(turn, kNotAllies), -1, text(a[0])); break;
    case Action::ChatToEnemiesUsingId: ChatId(turn, Others(turn, kNotAllies), a[0]); break;
    case Action::ChatToEnemiesUsingRange: ChatId(turn, Others(turn, kNotAllies), Ranged(turn, a[0], a[1])); break;
    case Action::ChatToPlayer:         Chat(turn, Seats(turn, a[0]), -1, text(a[1])); break;
    case Action::ChatToPlayerUsingId:  ChatId(turn, Seats(turn, a[0]), a[1]); break;
    case Action::ChatToPlayerUsingRange: ChatId(turn, Seats(turn, a[0]), Ranged(turn, a[1], a[2])); break;
    case Action::Taunt:
        Chat(turn, Others(turn, kAll), -1, std::to_string(a[0]));
        break;
    case Action::TauntUsingRange:
        Chat(turn, Others(turn, kAll), -1, std::to_string(Ranged(turn, a[0], a[1])));
        break;
    // For a script's author: written nowhere.
    case Action::ChatTrace:
    case Action::Log:
    case Action::LogTrace:
        break;

    // --- The world, through the executor.
    case Action::Train: {
        if (turn.registry == nullptr || turn.world == nullptr) break;
        const int slot = Things::Slot(*turn.registry, *turn.world, turn.player, ThingOf(a[0]), Civ(turn));
        if (slot >= 0) Executor::Train(turn, slot);
        break;
    }
    case Action::Research: {
        if (turn.registry == nullptr) break;
        const int tech = Things::Tech(*turn.registry, turn.player, ThingOf(a[0]), Civ(turn));
        if (tech >= 0) Executor::Research(turn, tech);
        break;
    }
    case Action::Build:
    case Action::BuildForward: {
        if (turn.registry == nullptr || turn.world == nullptr) break;
        const int slot = Things::Slot(*turn.registry, *turn.world, turn.player, ThingOf(a[0]), Civ(turn));
        if (slot >= 0) Executor::Build(turn, slot, static_cast<Action>(act.id) == Action::BuildForward);
        break;
    }
    case Action::AttackNow:
        if (turn.registry != nullptr) Executor::AttackNow(turn);
        break;
    case Action::DeleteUnit:
    case Action::DeleteBuilding:
        if (turn.registry != nullptr) Executor::Delete(turn, a[0], static_cast<Action>(act.id) == Action::DeleteBuilding);
        break;
    case Action::Resign:
        if (turn.registry != nullptr) Executor::Resign(turn);
        break;

    // A cheat: an order of its own, granted to computer seats only.
    case Action::CcAddResource:
        if (turn.registry != nullptr && a[1] > 0) {
            Give(*turn.registry, *turn.grid, Core::ComputerResourceCommand{a[0], a[1], turn.player});
            Executor::Refresh(turn);
        }
        break;

    // The Market: one lot, at the player's Market, as a player's button
    // orders it.
    case Action::BuyCommodity:
    case Action::SellCommodity:
        if (turn.registry != nullptr && a[0] >= 0 && a[0] <= 3) {
            const entt::entity market = Facts::OwnMarket(turn);
            if (market == entt::null) break;
            Give(*turn.registry, *turn.grid,
                 Core::MarketCommand{market,
                                     static_cast<uint8_t>(Core::Rules::DatResource(static_cast<Core::ResourceType>(a[0]))),
                                     1, static_cast<Action>(act.id) == Action::BuyCommodity, turn.player});
            Executor::Refresh(turn);
        }
        break;
    // Tribute, refused as a player's is (no Market, the fee unpaid, teams
    // locked against an enemy).
    case Action::TributeToPlayer:
        if (turn.registry != nullptr && a[1] >= 0 && a[1] <= 3 && a[2] > 0) {
            Facts::ForEach(turn, a[0], [&](int p) {
                if (p == turn.player) return;
                Give(*turn.registry, *turn.grid,
                     Core::TributeCommand{static_cast<Core::PlayerId>(p),
                                          static_cast<uint8_t>(Core::Rules::DatResource(static_cast<Core::ResourceType>(a[1]))),
                                          a[2], turn.player});
            });
            Executor::Refresh(turn);
        }
        break;
    case Action::ClearTributeMemory:
        if (a[1] >= 0 && a[1] <= 3) {
            Facts::ForEach(turn, a[0], [&](int p) {
                mind.tribute_memory[static_cast<size_t>(p)][static_cast<size_t>(a[1])] = 0;
            });
        }
        break;

    // Walls about the town (ai/Walls).
    case Action::BuildGate:
        if (turn.registry != nullptr && Walls::BuildGate(turn, a[0])) Executor::Refresh(turn);
        break;
    case Action::BuildWall:
        if (turn.registry != nullptr && Walls::BuildWall(turn, a[0], a[1])) Executor::Refresh(turn);
        break;
    case Action::EnableWallPlacement:
        Walls::Reserve(turn, a[0]);
        break;
    // The spy: Treason at a Castle, with the gold over the escrow.
    case Action::Spy:
        if (turn.registry != nullptr) {
            const entt::entity castle = Things::SpyAt(*turn.registry, *turn.census, mind, turn.player, false);
            if (castle != entt::null) {
                Give(*turn.registry, *turn.grid, Core::TreasonCommand{castle, turn.player});
                Executor::Refresh(turn);
            }
        }
        break;

    case Action::COUNT:
        break;
    }
}

} // namespace AI::Actions
