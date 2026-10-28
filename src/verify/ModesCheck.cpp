#include "verify/Harness.h"

#include <cstdio>
#include <cstring>
#include <vector>

#include "core/ConfigManager.h"
#include "core/GameSetup.h"
#include "core/Rules.h"
#include "ecs/Buildings.h"
#include "ecs/Combat.h"
#include "ecs/Mortality.h"
#include "ecs/Players.h"
#include "ecs/Population.h"
#include "ecs/Simulation.h"
#include "ecs/Techs.h"
#include "ecs/Victory.h"
#include "ecs/Vision.h"
#include "ecs/components/Building.h"
#include "ecs/components/Health.h"
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
            view.get<const ECS::Components::Identity>(e).unit_id == unit_id &&
            ECS::Combat::Alive(reg, e)) {
            ++n;
        }
    }
    return n;
}

int Owned(const entt::registry& reg, Core::PlayerId player) {
    int n = 0;
    auto view = reg.view<const ECS::Components::Identity, const ECS::Components::Owner>();
    for (auto e : view) {
        if (view.get<const ECS::Components::Owner>(e).player_id == player && ECS::Combat::Alive(reg, e)) ++n;
    }
    return n;
}

int Workers(const entt::registry& reg, Core::PlayerId player) {
    int n = 0;
    auto view = reg.view<const ECS::Components::Worker, const ECS::Components::Owner>();
    for (auto e : view) {
        if (view.get<const ECS::Components::Owner>(e).player_id == player) ++n;
    }
    return n;
}

entt::entity First(const entt::registry& reg, Core::PlayerId player, int unit_id) {
    auto view = reg.view<const ECS::Components::Identity, const ECS::Components::Owner>();
    for (auto e : view) {
        if (view.get<const ECS::Components::Owner>(e).player_id == player &&
            view.get<const ECS::Components::Identity>(e).unit_id == unit_id) {
            return e;
        }
    }
    return entt::null;
}

} // namespace

// The lobby's games and options taking effect. Regicide: every player starts
// with a King and a Castle besides its Town Center, villagers and scout, and
// is out -- everything it has destroyed -- when its King dies, the other side
// winning by it. Death Match: every stockpile at 20,000 (LANGUAGE.DLL 9752),
// the usual start otherwise. The population limit caps what houses support;
// the map revealed, explored or all in sight, from the start; the starting
// age; All Techs opening what a civilisation's tree shuts while keeping shut
// what every civilisation's tree shuts.
bool CheckModes() {
    const char* verdict = "ok";
    auto ok = [&]() { return std::strcmp(verdict, "ok") == 0; };
    auto& cfg = Core::ConfigManager::Get();
    const int king = cfg.GetUnitIdByName("KINGX");
    const int castle = cfg.GetUnitIdByName("CSTL");
    const int town_center = cfg.GetUnitIdByName("RTWC");
    const int scout = cfg.GetUnitIdByName("SCOUT");
    const int house = cfg.GetUnitIdByName("HOUS");

    // Regicide.
    {
        Core::GameSetup setup = Core::DefaultSetup();
        setup.regicide = true;
        ECS::Simulation sim;
        sim.Init(setup);
        auto& reg = sim.GetRegistry();
        for (Core::PlayerId p : {Core::PlayerId(1), Core::PlayerId(2)}) {
            if (!ok()) break;
            const auto* seat = ECS::Players::Find(reg, p);
            const int villagers =
                seat != nullptr ? ECS::Players::Resource(*seat, Core::Rules::Res::StartingVillagers).ToInt() : 0;
            if (Count(reg, p, king) != 1) verdict = "a Regicide player does not start with one King";
            else if (Count(reg, p, castle) != 1) verdict = "a Regicide player does not start with a Castle";
            else if (Count(reg, p, town_center) != 1) verdict = "a Regicide player has no Town Center";
            else if (Count(reg, p, scout) != 1) verdict = "a Regicide player has no scout";
            else if (Workers(reg, p) != villagers) verdict = "a Regicide player's villagers are not its civ's number";
        }
        if (ok()) {
            for (int t = 0; t < 3 * kTicksPerSecond; ++t) sim.Update(16);
            const auto* outcome = ECS::Victory::Read(reg);
            if (outcome == nullptr || outcome->Defeated(1) || outcome->Defeated(2)) {
                verdict = "a Regicide player with its King was defeated";
            }
        }
        // Player 2's King falls: player 2 is out, its things gone, player 1 wins.
        if (ok()) {
            ECS::Mortality::Kill(reg, sim.GetGrid(), First(reg, 2, king));
            for (int t = 0; t < 3 * kTicksPerSecond; ++t) sim.Update(16);
            const auto* outcome = ECS::Victory::Read(reg);
            if (outcome == nullptr || !outcome->Defeated(2)) verdict = "losing the King did not defeat its player";
            else if (Owned(reg, 2) != 0) verdict = "a player who lost its King kept its things";
            else if (!outcome->Won(1)) verdict = "the last King standing did not win";
        }
    }

    // A standard game with no Kings is not lost for the want of one.
    if (ok()) {
        ECS::Simulation sim;
        sim.Init(Core::DefaultSetup());
        auto& reg = sim.GetRegistry();
        if (Count(reg, 1, king) != 0 || Count(reg, 1, castle) != 0) verdict = "a standard game started with a King";
        for (int t = 0; t < 3 * kTicksPerSecond; ++t) sim.Update(16);
        const auto* outcome = ECS::Victory::Read(reg);
        if (ok() && (outcome == nullptr || outcome->Defeated(1))) verdict = "a standard game was lost for want of a King";
    }

    // Death Match: 20,000 of everything, the usual start otherwise.
    if (ok()) {
        Core::GameSetup setup = Core::DefaultSetup();
        setup.death_match = true;
        setup.resources = Core::StartingResources::DeathMatch;
        ECS::Simulation sim;
        sim.Init(setup);
        auto& reg = sim.GetRegistry();
        for (Core::PlayerId p : {Core::PlayerId(1), Core::PlayerId(2)}) {
            const auto* seat = ECS::Players::Find(reg, p);
            for (auto r : {Core::ResourceType::Food, Core::ResourceType::Wood,
                           Core::ResourceType::Gold, Core::ResourceType::Stone}) {
                if (seat == nullptr || ECS::Players::Stock(*seat, r) != 20000) {
                    verdict = "a Death Match stockpile is not 20,000";
                }
            }
            if (ok() && (Count(reg, p, town_center) != 1 || Count(reg, p, scout) != 1)) {
                verdict = "a Death Match start is not the usual Town Center and scout";
            }
        }
    }

    // The population limit, the map revealed, the starting age.
    if (ok()) {
        Core::GameSetup setup = Core::DefaultSetup();
        setup.population_limit = 25;
        setup.reveal = Core::Reveal::Explored;
        setup.starting_age = 2;
        ECS::Simulation sim;
        sim.Init(setup);
        auto& reg = sim.GetRegistry();
        auto& grid = sim.GetGrid();
        int placed = 0;
        for (int y = 2; y < grid.GetHeight() - 2 && placed < 10; y += 3) {
            for (int x = 2; x < grid.GetWidth() - 2 && placed < 10; x += 3) {
                if (!ECS::Buildings::CanPlace(grid, house, x, y)) continue;
                ECS::Buildings::Place(reg, grid, house, 1, x, y, true);
                ++placed;
            }
        }
        const int far = grid.GetWidth() - 3;
        if (ECS::Population::Capacity(reg, 1) != 25) verdict = "the population limit did not cap the houses";
        else if (!ECS::Vision::Explored(reg, 1, far, far) || ECS::Vision::Visible(reg, 1, far, far)) {
            verdict = "an explored map was not explored, or was in sight";
        } else if (ECS::Techs::Age(reg, 1) != 2) verdict = "the starting age was not the Castle Age";
    }
    if (ok()) {
        Core::GameSetup setup = Core::DefaultSetup();
        setup.reveal = Core::Reveal::AllVisible;
        ECS::Simulation sim;
        sim.Init(setup);
        sim.Update(16);
        const int far = sim.GetGrid().GetWidth() - 3;
        if (!ECS::Vision::Visible(sim.GetRegistry(), 1, far, far)) verdict = "an all-visible map was not in sight";
    }

    // All Techs: what the civ's own tree shuts is open; what every tree shuts
    // stays shut.
    if (ok()) {
        ECS::Simulation normal;
        normal.Init(Core::DefaultSetup());
        Core::GameSetup setup = Core::DefaultSetup();
        setup.all_techs = true;
        ECS::Simulation open;
        open.Init(setup);
        const auto* shut = ECS::Players::Find(normal.GetRegistry(), 1);
        const auto* all = ECS::Players::Find(open.GetRegistry(), 1);
        int opened = 0, newly_shut = 0;
        const auto& techs = Core::Rules::Get().techs;
        for (int id = 0; id < static_cast<int>(techs.size()); ++id) {
            if (!techs[id].valid || techs[id].civ >= 0) continue;
            const bool was = shut->techs[id].state == ECS::Players::TechState::Disabled;
            const bool now = all->techs[id].state == ECS::Players::TechState::Disabled;
            if (was && !now) ++opened;
            if (!was && now) ++newly_shut;
        }
        // Every playable civilisation's tree shuts the dat's dead bonuses.
        const int dead_bonus = [] {
            const auto& tables = Core::Rules::Get();
            for (int id = 0; id < static_cast<int>(tables.techs.size()) &&
                            id < static_cast<int>(tables.tech_text.size()); ++id) {
                if (tables.techs[id].valid && tables.tech_text[id].name == "C-Bonus, 1/2 Price Techs") {
                    return id;
                }
            }
            return -1;
        }();
        if (opened == 0) verdict = "All Techs opened nothing the civ's tree shuts";
        else if (newly_shut != 0) verdict = "All Techs shut something";
        else if (dead_bonus < 0) verdict = "the dat has no dead bonus to check";
        else if (all->techs[dead_bonus].state == ECS::Players::TechState::Done) {
            verdict = "All Techs gave a dead bonus every tree shuts";
        }
    }

    std::printf("modes=%s\n", verdict);
    return ok();
}

} // namespace Verify
