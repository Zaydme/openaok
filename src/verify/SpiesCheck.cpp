#include "verify/Harness.h"

#include <algorithm>
#include <cstdio>
#include <string>

#include "core/ConfigManager.h"
#include "core/GameSetup.h"
#include "core/Rules.h"
#include "ecs/Diplomacy.h"
#include "ecs/Events.h"
#include "ecs/Flares.h"
#include "ecs/Players.h"
#include "ecs/Simulation.h"
#include "ecs/Spies.h"
#include "ecs/Techs.h"
#include "ecs/Vision.h"
#include "ecs/components/Identity.h"
#include "ecs/components/Owner.h"
#include "ecs/components/Transform.h"
#include "ecs/components/Worker.h"

namespace Verify {

namespace {

namespace R = Core::Rules;
constexpr Core::PlayerId kUs = Core::PLAYER_LOCAL;
constexpr int kTicksPerSecond = 1000 / 16;

int SpiesTech() {
    for (int id = 0; id < static_cast<int>(R::Get().techs.size()); ++id) {
        if (ECS::Techs::Spies(id)) return id;
    }
    return -1;
}

int Gold(const entt::registry& reg) {
    return ECS::Players::Resource(*ECS::Players::Find(reg, kUs), R::Res::Gold).ToInt();
}

// The villagers of everyone in the game we are not allied with.
int EnemyVillagers(const entt::registry& reg) {
    int n = 0;
    for (Core::PlayerId p = 1; p < Core::kMaxPlayers; ++p) {
        const auto* seat = ECS::Players::Find(reg, p);
        if (p == kUs || seat == nullptr || ECS::Diplomacy::Allied(reg, kUs, p)) continue;
        n += ECS::Players::Resource(*seat, R::Res::CivilianPopulation).ToInt();
    }
    return n;
}

entt::entity Owned(entt::registry& reg, Core::PlayerId player, int unit_id) {
    auto view = reg.view<ECS::Components::Identity, ECS::Components::Owner>();
    for (auto e : view) {
        if (view.get<ECS::Components::Identity>(e).unit_id == unit_id &&
            view.get<ECS::Components::Owner>(e).player_id == player) {
            return e;
        }
    }
    return entt::null;
}

} // namespace

bool CheckSpies() {
    auto& cfg = Core::ConfigManager::Get();
    std::string verdict = "ok";
    const int spies = SpiesTech();
    if (spies < 0) verdict = "no Spies in the data";

    // A random map game: Spies' price is 200 gold an enemy villager, and once
    // researched we see what an enemy villager sees.
    if (verdict == "ok") {
        ECS::Simulation sim;
        sim.Init();
        auto& reg = sim.GetRegistry();
        sim.Update(16);
        const auto* me = ECS::Players::Find(reg, kUs);
        const R::TechRules* tech = R::Tech(spies);
        const int villagers = std::clamp(EnemyVillagers(reg), 1, 150);
        const auto& status = me->techs[static_cast<size_t>(spies)];
        int gold = -1;
        for (int i = 0; i < status.cost_count; ++i) {
            if (status.costs[i].resource == R::Res::Gold) gold = status.costs[i].amount;
        }
        // An enemy villager, and the tile it stands on.
        int vx = -1, vy = -1;
        auto workers = reg.view<ECS::Components::Worker, ECS::Components::Owner, ECS::Components::Transform>();
        for (auto e : workers) {
            const Core::PlayerId p = workers.get<ECS::Components::Owner>(e).player_id;
            if (p == kUs || ECS::Diplomacy::Allied(reg, kUs, p)) continue;
            const auto& at = workers.get<ECS::Components::Transform>(e).position;
            vx = at.x.ToInt();
            vy = at.y.ToInt();
            break;
        }
        if (status.state == ECS::Players::TechState::Disabled) {
            verdict = "Spies is not offered in a random map game";
        } else if (gold != tech->costs[0].amount * villagers) {
            verdict = "Spies does not cost its price for each enemy villager";
        } else if (vx < 0) {
            verdict = "no enemy villager to spy on";
        } else if (ECS::Vision::Visible(reg, kUs, vx, vy)) {
            verdict = "an enemy villager was seen before Spies";
        } else {
            ECS::Techs::Complete(reg, kUs, spies);
            sim.Update(16);
            if (!ECS::Vision::Visible(reg, kUs, vx, vy)) verdict = "Spies does not show what an enemy sees";
        }
    }

    // Death Match: no Spies.
    if (verdict == "ok") {
        Core::GameSetup setup = Core::DefaultSetup();
        setup.resources = Core::StartingResources::DeathMatch;
        ECS::Simulation sim;
        sim.Init(setup);
        const auto* me = ECS::Players::Find(sim.GetRegistry(), kUs);
        if (me->techs[static_cast<size_t>(spies)].state != ECS::Players::TechState::Disabled) {
            verdict = "Spies is offered in Death Match";
        }
    }

    // Regicide: no Spies, but Treason at a Castle -- 400 gold each time, and
    // every enemy King marked for us, seen round, for ten seconds.
    if (verdict == "ok") {
        Core::GameSetup setup = Core::DefaultSetup();
        setup.regicide = true;
        ECS::Simulation sim;
        sim.Init(setup);
        auto& reg = sim.GetRegistry();
        sim.Update(16);
        auto* me = ECS::Players::Find(reg, kUs);
        const entt::entity castle = Owned(reg, kUs, cfg.GetUnitIdByName(setup.regicide_castle));
        const int king_id = cfg.GetUnitIdByName(setup.regicide_king);
        entt::entity enemy_king = entt::null;
        for (Core::PlayerId p = 1; p < Core::kMaxPlayers && enemy_king == entt::null; ++p) {
            if (p != kUs && ECS::Players::Find(reg, p) != nullptr &&
                ECS::Diplomacy::StanceOf(reg, kUs, p) != ECS::Diplomacy::Stance::Ally) {
                enemy_king = Owned(reg, p, king_id);
            }
        }
        auto treasons = [&](int seconds) {
            int n = 0;
            for (int t = 0; t < seconds * kTicksPerSecond; ++t) {
                sim.Update(16);
                const auto* log = ECS::Events::Read(reg);
                for (const auto& e : log->events) {
                    n += e.tick == log->tick && e.kind == ECS::Events::Kind::Treason && e.player == kUs;
                }
            }
            return n;
        };
        if (me->techs[static_cast<size_t>(spies)].state != ECS::Players::TechState::Disabled) {
            verdict = "Spies is offered in Regicide";
        } else if (castle == entt::null || enemy_king == entt::null) {
            verdict = "no Castle of ours or no enemy King";
        } else {
            ECS::Players::SetResource(*me, R::Res::Gold, Core::Fixed32(399));
            ECS::Spies::Treason(reg, castle, kUs);
            const int refused = treasons(1);
            ECS::Players::SetResource(*me, R::Res::Gold, Core::Fixed32(1000));
            const auto& king_at = reg.get<ECS::Components::Transform>(enemy_king).position;
            const int kx = king_at.x.ToInt(), ky = king_at.y.ToInt();
            const bool seen_before = ECS::Vision::Visible(reg, kUs, kx, ky);
            const bool done = ECS::Spies::Treason(reg, castle, kUs);
            sim.Update(16);
            const auto* flares = ECS::Flares::Read(reg);
            const auto* events = ECS::Events::Read(reg);
            bool marked = false;
            for (const auto& f : flares->flares) {
                marked |= f.Seen(kUs) && f.from == kUs && f.lifetime_ms == 10000 &&
                          ECS::Flares::Showing(*flares, f, events->tick) &&
                          f.at.x.ToInt() == kx && f.at.y.ToInt() == ky;
            }
            const bool seen = ECS::Vision::Visible(reg, kUs, kx, ky);
            const int gold_after = Gold(reg);
            for (int t = 0; t < 11 * kTicksPerSecond; ++t) sim.Update(16);
            bool still = false;
            for (const auto& f : flares->flares) still |= ECS::Flares::Showing(*flares, f, events->tick);
            if (refused != 0) verdict = "Treason was done without its gold";
            else if (!done || gold_after != 600) verdict = "Treason did not take its 400 gold";
            else if (!marked) verdict = "Treason did not mark an enemy King for us";
            else if (seen_before || !seen) verdict = "Treason did not show us an enemy King's ground";
            else if (still) verdict = "Treason's mark outlasted ten seconds";
        }
    }

    std::printf("spies=%s\n", verdict.c_str());
    return verdict == "ok";
}

} // namespace Verify
