#include "verify/Harness.h"

#include <cstdio>
#include <cstring>

#include "core/Command.h"
#include "core/ConfigManager.h"
#include "core/GameSetup.h"
#include "ecs/Buildings.h"
#include "ecs/Cheats.h"
#include "ecs/Combat.h"
#include "ecs/Orders.h"
#include "ecs/Players.h"
#include "ecs/Simulation.h"
#include "ecs/Victory.h"
#include "ecs/Vision.h"
#include "ecs/components/Building.h"
#include "ecs/components/Identity.h"
#include "ecs/components/Owner.h"
#include "ecs/components/Transform.h"
#include "ecs/components/Worker.h"

namespace Verify {

namespace {

constexpr int kTicksPerSecond = 1000 / 16;

int Count(const entt::registry& reg, Core::PlayerId player, int unit_id) {
    int n = 0;
    auto view = reg.view<const ECS::Components::Identity, const ECS::Components::Owner>();
    for (auto e : view) {
        if (view.get<const ECS::Components::Owner>(e).player_id == player &&
            view.get<const ECS::Components::Identity>(e).unit_id == unit_id && ECS::Combat::Alive(reg, e)) {
            ++n;
        }
    }
    return n;
}

int Food(const entt::registry& reg, Core::PlayerId p) {
    return ECS::Players::Stock(*ECS::Players::Find(reg, p), Core::ResourceType::Food);
}

void Cheat(ECS::Simulation& sim, Core::PlayerId player, const char* typed) {
    ECS::Orders::Execute(sim.GetRegistry(), sim.GetGrid(),
                         Core::CheatCommand{ECS::Cheats::Recognise(typed), player});
}

} // namespace

// The cheat codes: each typed line recognised as its code, however
// it is capitalised and spaced, and nothing else taken for one; none of them
// doing anything unless the lobby allowed cheats; and each doing what it
// does -- the stockpiles, the map explored and all in sight, a building up at
// once under "aegis", the Cobra car and the Saboteur, a player torpedoed, the
// game won, the enemies destroyed, oneself destroyed.
bool CheckCheats() {
    const char* verdict = "ok";
    auto ok = [&]() { return std::strcmp(verdict, "ok") == 0; };
    namespace C = ECS::Cheats;

    struct Typed { const char* text; int code; };
    const Typed lines[] = {
        {"cheese steak jimmy's", C::kFood}, {"LUMBERJACK", C::kWood}, {"  robin hood ", C::kGold},
        {"rock on", C::kStone}, {"marco", C::kMarco}, {"polo", C::kPolo}, {"aegis", C::kAegis},
        {"natural wonders", C::kNaturalWonders}, {"i r winner", C::kWinner},
        {"black death", C::kBlackDeath}, {"wimpywimpywimpy", C::kWimpy},
        {"how do you turn this on", C::kCobra}, {"to smithereens", C::kSmithereens},
        {"torpedo3", C::kTorpedo + 3}, {"robinhood", C::kNone}, {"torpedo9", C::kNone},
        {"hello", C::kNone}, {"i love the monkey head", C::kNone},
    };
    for (const Typed& line : lines) {
        if (C::Recognise(line.text) != line.code) {
            verdict = "a chat line was taken for the wrong cheat";
            break;
        }
    }
    auto& cfg = Core::ConfigManager::Get();
    const int cobra = cfg.GetUnitIdByName("COBRA");
    const int saboteur = cfg.GetUnitIdByName("HDSQD");
    const int house = cfg.GetUnitIdByName("HOUS");

    // Without the lobby's leave, nothing.
    if (ok()) {
        ECS::Simulation sim;
        sim.Init(Core::DefaultSetup());
        const int before = Food(sim.GetRegistry(), 1);
        Cheat(sim, 1, "cheese steak jimmy's");
        if (Food(sim.GetRegistry(), 1) != before) verdict = "a cheat worked in a game that does not allow them";
    }

    Core::GameSetup setup = Core::DefaultSetup();
    setup.cheats = true;
    setup.players[3] = {true, 1, 0};
    if (ok()) {
        ECS::Simulation sim;
        sim.Init(setup);
        auto& reg = sim.GetRegistry();
        auto& grid = sim.GetGrid();
        const int before = Food(reg, 1);
        Cheat(sim, 1, "cheese steak jimmy's");
        const int far = grid.GetWidth() - 3;
        if (Food(reg, 1) != before + 1000) verdict = "cheese steak jimmy's did not give 1,000 food";
        if (ok()) {
            Cheat(sim, 1, "marco");
            if (!ECS::Vision::Explored(reg, 1, far, far) || ECS::Vision::Visible(reg, 1, far, far)) {
                verdict = "marco did not explore the map";
            }
            Cheat(sim, 1, "marco");
            if (ok() && ECS::Vision::Explored(reg, 1, far, far)) verdict = "marco again did not hide the map";
        }
        if (ok()) {
            Cheat(sim, 1, "polo");
            sim.Update(16);
            if (!ECS::Vision::Visible(reg, 1, far, far)) verdict = "polo did not lift the fog";
            if (ok() && ECS::Vision::Visible(reg, 2, far, far)) verdict = "polo lifted another player's fog";
        }
        // Aegis: a House up at the first blow.
        if (ok()) {
            Cheat(sim, 1, "aegis");
            entt::entity builder = entt::null;
            auto workers = reg.view<ECS::Components::Worker, ECS::Components::Owner,
                                    ECS::Components::Transform>();
            for (auto w : workers) {
                if (workers.get<ECS::Components::Owner>(w).player_id == 1) builder = w;
            }
            const auto at = reg.get<ECS::Components::Transform>(builder).position;
            int hx = 0, hy = 0;
            bool found = false;
            for (int r = 3; r < 12 && !found; ++r) {
                for (int d = -r; d <= r && !found; ++d) {
                    const int x = Physics::TileOf(at.x) + d, y = Physics::TileOf(at.y) + r;
                    if (ECS::Buildings::CanPlace(grid, house, x, y)) { hx = x; hy = y; found = true; }
                }
            }
            ECS::Orders::Execute(reg, grid, Core::BuildCommand{builder, house, hx, hy, 1});
            bool up = false;
            int swinging = 0;   // ticks the builder spent at work before it stood
            for (int t = 0; t < 30 * kTicksPerSecond && !up; ++t) {
                sim.Update(16);
                if (reg.get<ECS::Components::Worker>(builder).working) ++swinging;
                auto houses = reg.view<ECS::Components::Building, ECS::Components::Identity,
                                       ECS::Components::Owner>();
                for (auto h : houses) {
                    if (houses.get<ECS::Components::Identity>(h).unit_id == house &&
                        houses.get<ECS::Components::Owner>(h).player_id == 1 &&
                        houses.get<ECS::Components::Building>(h).state ==
                            ECS::Components::BuildingState::Complete) {
                        up = true;
                    }
                }
            }
            // A villager's walk there takes a few seconds; its building, under
            // aegis, a blow.
            if (!up || swinging > 2) verdict = "aegis did not raise a House at once";
            Cheat(sim, 1, "aegis");
        }
        if (ok()) {
            Cheat(sim, 1, "how do you turn this on");
            Cheat(sim, 1, "to smithereens");
            if (Count(reg, 1, cobra) != 1) verdict = "how do you turn this on gave no Cobra car";
            else if (Count(reg, 1, saboteur) != 1) verdict = "to smithereens gave no Saboteur";
        }
        if (ok()) {
            Cheat(sim, 1, "torpedo3");
            for (int t = 0; t < 2 * kTicksPerSecond; ++t) sim.Update(16);
            const auto* outcome = ECS::Victory::Read(reg);
            if (outcome == nullptr || !outcome->Defeated(3)) verdict = "torpedo3 did not destroy player 3";
            else if (outcome->Defeated(2)) verdict = "torpedo3 destroyed player 2";
        }
        if (ok()) {
            Cheat(sim, 1, "i r winner");
            const auto* outcome = ECS::Victory::Read(reg);
            if (outcome == nullptr || !outcome->Won(1) || !outcome->Defeated(2)) {
                verdict = "i r winner did not win the game";
            }
        }
    }
    if (ok()) {
        ECS::Simulation sim;
        sim.Init(setup);
        Cheat(sim, 1, "black death");
        for (int t = 0; t < 2 * kTicksPerSecond; ++t) sim.Update(16);
        const auto* outcome = ECS::Victory::Read(sim.GetRegistry());
        if (outcome == nullptr || !outcome->Defeated(2) || !outcome->Defeated(3) || outcome->Defeated(1)) {
            verdict = "black death did not destroy every enemy";
        }
    }
    if (ok()) {
        ECS::Simulation sim;
        sim.Init(setup);
        Cheat(sim, 2, "wimpywimpywimpy");
        const auto* outcome = ECS::Victory::Read(sim.GetRegistry());
        if (outcome == nullptr || !outcome->Defeated(2) || outcome->Defeated(1)) {
            verdict = "wimpywimpywimpy did not destroy its typist";
        }
    }

    std::printf("cheats=%s\n", verdict);
    return ok();
}

} // namespace Verify
