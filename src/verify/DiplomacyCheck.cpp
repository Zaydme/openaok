#include "verify/Harness.h"

#include <cstdio>
#include <cstring>
#include <vector>

#include "core/Command.h"
#include "core/CommandQueue.h"
#include "core/ConfigManager.h"
#include "core/GameSetup.h"
#include "core/Rules.h"
#include "ecs/Diplomacy.h"
#include "ecs/Events.h"
#include "ecs/Mortality.h"
#include "ecs/Players.h"
#include "ecs/Simulation.h"
#include "ecs/Techs.h"
#include "ecs/Units.h"
#include "ecs/Victory.h"
#include "ecs/Vision.h"
#include "ecs/components/Combat.h"
#include "ecs/components/Health.h"
#include "ecs/components/Owner.h"
#include "ecs/components/Transform.h"

namespace Verify {

namespace {

using Core::Fixed32;
using ECS::Diplomacy::Stance;
using ECS::Components::Vector2D;
namespace R = Core::Rules;

constexpr int kTicksPerSecond = 1000 / 16;

void Order(ECS::Simulation& sim, const Core::Command& command) {
    Core::CommandQueue::Get().PushCommand(command);
    sim.Update(16);
}

void Run(ECS::Simulation& sim, int seconds) {
    for (int t = 0; t < seconds * kTicksPerSecond; ++t) sim.Update(16);
}

Core::GameSetup ThreePlayers(bool teams, bool locked) {
    Core::GameSetup setup = Core::DefaultSetup();
    setup.players[1].team = teams ? 1 : 0;   // Teutons
    setup.players[2].team = teams ? 1 : 0;   // Franks
    setup.players[3] = {true, 7, 0};         // Byzantines, on no team
    setup.lock_teams = locked;
    return setup;
}

// Destroys everything a player owns.
void Wipe(ECS::Simulation& sim, Core::PlayerId player) {
    auto& reg = sim.GetRegistry();
    std::vector<entt::entity> theirs;
    auto all = reg.view<ECS::Components::Owner, ECS::Components::Health>();
    for (auto e : all) {
        if (all.get<ECS::Components::Owner>(e).player_id == player) theirs.push_back(e);
    }
    for (auto e : theirs) {
        if (reg.valid(e)) ECS::Mortality::Kill(reg, sim.GetGrid(), e);
    }
}

// Open ground round (x, y) with room for a skirmish: every tile within two
// walkable, searched outward.
bool OpenPatch(const Physics::Grid& grid, int& x, int& y) {
    for (int r = 0; r < 30; ++r) {
        for (int dy = -r; dy <= r; ++dy) {
            for (int dx = -r; dx <= r; ++dx) {
                if (std::abs(dx) != r && std::abs(dy) != r) continue;
                bool open = true;
                for (int oy = -2; oy <= 2 && open; ++oy) {
                    for (int ox = -3; ox <= 3 && open; ++ox) {
                        open = grid.IsWalkable(x + dx + ox, y + dy + oy);
                    }
                }
                if (open) {
                    x += dx;
                    y += dy;
                    return true;
                }
            }
        }
    }
    return false;
}

Vector2D At(int x, int y) {
    return {Fixed32(x) + Fixed32(1) / Fixed32(3), Fixed32(y) + Fixed32(1) / Fixed32(5)};
}

int Hp(const entt::registry& reg, entt::entity e) {
    return reg.valid(e) ? reg.get<ECS::Components::Health>(e).current_hp : -1;
}

bool Fighting(const entt::registry& reg, entt::entity e, entt::entity target) {
    const auto* combat = reg.valid(e) ? reg.try_get<ECS::Components::Combat>(e) : nullptr;
    return combat != nullptr && combat->state != ECS::Components::CombatState::Idle &&
           combat->target == target;
}

bool Idle(const entt::registry& reg, entt::entity e) {
    const auto* combat = reg.valid(e) ? reg.try_get<ECS::Components::Combat>(e) : nullptr;
    return combat != nullptr && combat->state == ECS::Components::CombatState::Idle;
}

} // namespace

// Diplomacy: the lobby's stances, team bonuses and explored team-mates'
// towns, Lock Teams refusing every change; stances changed in play and
// one-sided; units that leave a neutral's alone until ordered, never attack an
// ally even when ordered, answer a neutral's attack and not an ally's;
// Cartography sharing allies' sight -- allied each way only; and allied
// victory, from the lobby and chosen in play.
bool CheckDiplomacy() {
    const char* verdict = "ok";
    auto ok = [&]() { return std::strcmp(verdict, "ok") == 0; };
    auto& cfg = Core::ConfigManager::Get();
    const int militia_id = cfg.GetUnitIdByName("SPRMN");
    const int villager_id = cfg.GetUnitIdByName("VMBAS");
    const int knight_id = cfg.GetUnitIdByName("KNGHT");
    constexpr int kConversionResistance = 77;
    constexpr int kCartography = 19;

    // The lobby: team-mates allied with allied victory, the rest enemies,
    // and Lock Teams holding it so.
    {
        ECS::Simulation sim;
        sim.Init(ThreePlayers(true, true));
        auto& reg = sim.GetRegistry();
        using ECS::Diplomacy::StanceOf;
        if (StanceOf(reg, 1, 2) != Stance::Ally || StanceOf(reg, 2, 1) != Stance::Ally ||
            StanceOf(reg, 1, 3) != Stance::Enemy || StanceOf(reg, 3, 2) != Stance::Enemy) {
            verdict = "the lobby's teams did not set the stances";
        } else if (!ECS::Diplomacy::AlliedVictory(reg, 1) || ECS::Diplomacy::AlliedVictory(reg, 3)) {
            verdict = "a team did not choose allied victory";
        }
        if (ok()) {
            Order(sim, Core::DiplomacyCommand{3, static_cast<uint8_t>(Stance::Ally), 1});
            Order(sim, Core::DiplomacyCommand{2, static_cast<uint8_t>(Stance::Enemy), 1});
            Order(sim, Core::AlliedVictoryCommand{false, 2});
            if (StanceOf(reg, 1, 3) != Stance::Enemy || StanceOf(reg, 1, 2) != Stance::Ally ||
                !ECS::Diplomacy::AlliedVictory(reg, 2)) {
                verdict = "a stance changed with teams locked";
            }
        }
        // Team bonuses: every member has each member's -- the Franks' +2
        // sight for knights, the Teutons' conversion resistance -- and the
        // player on no team neither.
        if (ok()) {
            const int knight_sight = R::Unit(knight_id)->los.ToInt();
            const auto* teuton = ECS::Players::Find(reg, 1);
            const auto* frank = ECS::Players::Find(reg, 2);
            const auto* alone = ECS::Players::Find(reg, 3);
            const Fixed32 frank_base = R::Civ(2)->resources[kConversionResistance];
            const Fixed32 alone_base = R::Civ(7)->resources[kConversionResistance];
            if (ECS::Players::UnitOf(reg, 1, knight_id)->los.ToInt() != knight_sight + 2 ||
                ECS::Players::UnitOf(reg, 2, knight_id)->los.ToInt() != knight_sight + 2) {
                verdict = "a team did not get the Franks' team bonus";
            } else if (ECS::Players::Resource(*frank, kConversionResistance) != frank_base + Fixed32(2) ||
                       teuton == nullptr) {
                verdict = "a team did not get the Teutons' team bonus";
            } else if (ECS::Players::UnitOf(reg, 3, knight_id)->los.ToInt() != knight_sight ||
                       ECS::Players::Resource(*alone, kConversionResistance) != alone_base) {
                verdict = "a player on no team got a team bonus";
            }
        }
        // A team-mate's town is on the map from the start; an enemy's is not.
        if (ok()) {
            const auto [bx, by] = sim.player_starts[1];
            if (!ECS::Vision::Explored(reg, 1, bx, by)) {
                verdict = "a team-mate's Town Center was not on the map at the start";
            } else if (ECS::Vision::Explored(reg, 3, bx, by)) {
                verdict = "an enemy's Town Center was on the map at the start";
            }
        }
        // The team wins together once the third is gone.
        if (ok()) {
            Run(sim, 2);
            Wipe(sim, 3);
            Run(sim, 3);
            const auto* outcome = ECS::Victory::Read(reg);
            if (!outcome->over || !outcome->Won(1) || !outcome->Won(2) || !outcome->Defeated(3)) {
                verdict = "a team did not win together";
            }
        }
    }

    // In play, with teams unlocked: stances changed by order, one-sided.
    ECS::Simulation sim;
    sim.Init(ThreePlayers(false, false));
    auto& reg = sim.GetRegistry();
    auto& grid = sim.GetGrid();
    const auto [ax, ay] = sim.player_starts[0];
    const auto [bx, by] = sim.player_starts[1];
    // Skirmish ground halfway between the first two towns, out of sight of
    // both, each fight on a patch of its own.
    int fx = (ax + bx) / 2, fy = (ay + by) / 2;
    if (!OpenPatch(grid, fx, fy)) verdict = "no open ground to fight on";
    auto patch = [&](int n, int& x, int& y) {
        x = fx;
        y = fy + n * 12 - 24;
        if (!OpenPatch(grid, x, y)) return false;
        // Nothing of Gaia's that fights -- a wolf -- near the skirmish: what
        // is measured is who the players' units take on.
        std::vector<entt::entity> wild;
        auto fighters = reg.view<ECS::Components::Combat, ECS::Components::Owner, ECS::Components::Transform>();
        for (auto e : fighters) {
            if (fighters.get<ECS::Components::Owner>(e).player_id != Core::PLAYER_GAIA) continue;
            const auto& at = fighters.get<ECS::Components::Transform>(e).position;
            const int dx = at.x.ToInt() - x, dy = at.y.ToInt() - y;
            if (dx * dx + dy * dy <= 20 * 20) wild.push_back(e);
        }
        for (auto e : wild) reg.destroy(e);
        return true;
    };

    if (ok()) {
        // Given three times over, the order is one change, told once.
        int told = 0;
        for (int i = 0; i < 3; ++i) {
            Order(sim, Core::DiplomacyCommand{2, static_cast<uint8_t>(Stance::Neutral), 1});
            const auto* log = ECS::Events::Read(reg);
            for (const auto& e : log->events) {
                told += e.tick == log->tick && e.kind == ECS::Events::Kind::Stance && e.player == 1 && e.from == 2;
            }
        }
        if (ECS::Diplomacy::StanceOf(reg, 1, 2) != Stance::Neutral) {
            verdict = "a stance did not change by order";
        } else if (ECS::Diplomacy::StanceOf(reg, 2, 1) != Stance::Enemy) {
            verdict = "changing one stance changed the other player's";
        } else if (told != 1) {
            verdict = "a stance set again as it was was told again";
        }
    }

    // A soldier leaves a neutral's villager alone, until its owner makes him
    // an enemy.
    if (ok()) {
        int x = 0, y = 0;
        if (!patch(0, x, y)) {
            verdict = "no ground for the first skirmish";
        } else {
            const entt::entity soldier = ECS::Units::Spawn(reg, militia_id, 1, At(x - 1, y));
            const entt::entity villager = ECS::Units::Spawn(reg, villager_id, 2, At(x + 1, y));
            const int full = Hp(reg, villager);
            Run(sim, 4);
            if (Hp(reg, villager) != full || !Idle(reg, soldier)) {
                verdict = "a soldier attacked a neutral's villager on sight";
            } else {
                Order(sim, Core::DiplomacyCommand{2, static_cast<uint8_t>(Stance::Enemy), 1});
                Run(sim, 4);
                if (Hp(reg, villager) >= full) verdict = "a soldier left an enemy's villager alone";
                // Made an ally mid-fight, the enemy is left alone at once --
                // though it fights on.
                Order(sim, Core::DiplomacyCommand{2, static_cast<uint8_t>(Stance::Ally), 1});
                Run(sim, 2);
                if (ok() && !Idle(reg, soldier)) verdict = "a soldier fought on against a new ally";
            }
        }
    }

    // Ordered at a neutral's, it attacks; ordered at an ally's, it does not.
    if (ok()) {
        int x = 0, y = 0;
        Order(sim, Core::DiplomacyCommand{2, static_cast<uint8_t>(Stance::Neutral), 1});
        Order(sim, Core::DiplomacyCommand{3, static_cast<uint8_t>(Stance::Ally), 1});
        if (!patch(1, x, y)) {
            verdict = "no ground for the second skirmish";
        } else {
            const entt::entity soldier = ECS::Units::Spawn(reg, militia_id, 1, At(x - 1, y));
            const entt::entity friend_ = ECS::Units::Spawn(reg, villager_id, 3, At(x + 1, y));
            const int full = Hp(reg, friend_);
            Order(sim, Core::AttackCommand{soldier, friend_, 1});
            Run(sim, 4);
            if (Hp(reg, friend_) != full || !Idle(reg, soldier)) {
                verdict = "a soldier attacked an ally's villager when ordered";
            } else {
                const entt::entity neutral = ECS::Units::Spawn(reg, villager_id, 2, At(x, y + 2));
                const int theirs = Hp(reg, neutral);
                Order(sim, Core::AttackCommand{soldier, neutral, 1});
                Run(sim, 4);
                if (Hp(reg, neutral) >= theirs) verdict = "a soldier ordered at a neutral's did not attack";
            }
        }
    }

    // Attacked by a neutral's soldier, it defends itself; by one whose owner
    // it regards as an ally, it does not.
    if (ok()) {
        int x = 0, y = 0;
        if (!patch(2, x, y)) {
            verdict = "no ground for the third skirmish";
        } else {
            const entt::entity ours = ECS::Units::Spawn(reg, militia_id, 1, At(x - 1, y));
            const entt::entity neutral = ECS::Units::Spawn(reg, militia_id, 2, At(x + 1, y));
            Run(sim, 3);
            if (!Fighting(reg, neutral, ours)) verdict = "an enemy's soldier did not attack on sight";
            else if (!Fighting(reg, ours, neutral)) verdict = "a soldier did not defend itself against a neutral's";
        }
    }
    if (ok()) {
        int x = 0, y = 0;
        if (!patch(3, x, y)) {
            verdict = "no ground for the fourth skirmish";
        } else {
            const entt::entity ours = ECS::Units::Spawn(reg, militia_id, 1, At(x - 1, y));
            const entt::entity theirs = ECS::Units::Spawn(reg, militia_id, 3, At(x + 1, y));
            const int full = Hp(reg, ours);
            Run(sim, 3);
            if (!Fighting(reg, theirs, ours) || Hp(reg, ours) >= full) {
                verdict = "a player regarding an ally as an enemy did not attack it";
            } else if (!Idle(reg, ours)) {
                verdict = "a soldier struck back at a player its owner regards as an ally";
            }
        }
    }

    // Cartography: with an ally each way, it sees what the ally sees; not
    // before, not the other way round without its own, and not once the
    // ally stops regarding it as one.
    if (ok()) {
        Order(sim, Core::DiplomacyCommand{2, static_cast<uint8_t>(Stance::Ally), 1});
        Order(sim, Core::DiplomacyCommand{1, static_cast<uint8_t>(Stance::Ally), 2});
        const int sx = bx, sy = by;
        const int ours_x = ax, ours_y = ay;
        // Ground the ally explored but cannot see: a corner of the map neither
        // has been near.
        int kx = -1, ky = -1;
        const int w = grid.GetWidth(), h = grid.GetHeight();
        const int corners[4][2] = {{6, 6}, {w - 7, 6}, {6, h - 7}, {w - 7, h - 7}};
        for (const auto& c : corners) {
            if (kx < 0 && !ECS::Vision::Explored(reg, 1, c[0], c[1]) &&
                !ECS::Vision::Explored(reg, 2, c[0], c[1])) {
                kx = c[0];
                ky = c[1];
            }
        }
        if (kx >= 0) ECS::Vision::Explore(reg, 2, kx, ky, 2);
        if (!ECS::Diplomacy::Allied(reg, 1, 2)) verdict = "two players allied each way are not allies";
        else if (kx < 0) verdict = "no corner of the map left unexplored";
        else if (ECS::Vision::Visible(reg, 1, sx, sy) || ECS::Vision::Explored(reg, 1, sx, sy)) {
            verdict = "an ally's town was seen before Cartography";
        }
        if (ok()) {
            ECS::Techs::Complete(reg, 1, kCartography);
            sim.Update(16);
            if (!ECS::Vision::Visible(reg, 1, sx, sy) || !ECS::Vision::Explored(reg, 1, sx, sy)) {
                verdict = "Cartography did not share an ally's sight";
            } else if (!ECS::Vision::Explored(reg, 1, kx, ky) || ECS::Vision::Visible(reg, 1, kx, ky)) {
                verdict = "Cartography did not share what an ally had explored";
            } else if (ECS::Vision::Visible(reg, 2, ours_x, ours_y)) {
                verdict = "an ally without Cartography saw through the researcher's eyes";
            }
        }
        if (ok()) {
            Order(sim, Core::DiplomacyCommand{1, static_cast<uint8_t>(Stance::Neutral), 2});
            sim.Update(16);
            if (ECS::Vision::Visible(reg, 1, sx, sy)) {
                verdict = "sight was shared with a player who no longer regards it as an ally";
            } else if (!ECS::Vision::Explored(reg, 1, sx, sy)) {
                verdict = "ground once seen through an ally's eyes was forgotten";
            }
        }
    }

    // Allied victory chosen in play: allies each way win together once the
    // third is gone -- but only when both have chosen it.
    if (ok()) {
        Order(sim, Core::DiplomacyCommand{1, static_cast<uint8_t>(Stance::Ally), 2});
        Wipe(sim, 3);
        Run(sim, 3);
        const auto* outcome = ECS::Victory::Read(reg);
        if (!outcome->Defeated(3)) {
            verdict = "the wiped-out player was not defeated";
        } else if (outcome->over) {
            verdict = "allies won together without choosing allied victory";
        } else {
            Order(sim, Core::AlliedVictoryCommand{true, 1});
            Run(sim, 2);
            if (outcome->over) verdict = "allies won together when only one chose allied victory";
            Order(sim, Core::AlliedVictoryCommand{true, 2});
            Run(sim, 2);
            if (ok() && (!outcome->over || !outcome->Won(1) || !outcome->Won(2))) {
                verdict = "allies who chose allied victory did not win together";
            }
        }
    }

    std::printf("diplomacy=%s\n", verdict);
    return ok();
}

} // namespace Verify
