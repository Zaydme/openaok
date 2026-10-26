#include "verify/Harness.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "core/Command.h"
#include "core/CommandQueue.h"
#include "core/ConfigManager.h"
#include "core/GameSetup.h"
#include "core/Rules.h"
#include "ecs/Buildings.h"
#include "ecs/Combat.h"
#include "ecs/Garrisons.h"
#include "ecs/Mortality.h"
#include "ecs/Navigation.h"
#include "ecs/Simulation.h"
#include "ecs/Units.h"
#include "ecs/Victory.h"
#include "ecs/components/Combat.h"
#include "ecs/components/Garrison.h"
#include "ecs/components/Identity.h"
#include "ecs/components/Mortal.h"
#include "ecs/components/Owner.h"
#include "ecs/components/Transform.h"
#include "pathfinding/Pathfinder.h"
#include "verify/Shore.h"

namespace Verify {

namespace {

using Core::Fixed32;
using ECS::Components::Vector2D;

constexpr Core::PlayerId kAttacker = 1;
constexpr Core::PlayerId kDefender = 2;

entt::entity FirstOf(const entt::registry& reg, int unit_id, Core::PlayerId player) {
    auto all = reg.view<const ECS::Components::Identity, const ECS::Components::Owner>();
    for (auto e : all) {
        if (all.get<const ECS::Components::Identity>(e).unit_id == unit_id &&
            all.get<const ECS::Components::Owner>(e).player_id == player) {
            return e;
        }
    }
    return entt::null;
}

// A land unit set down beside `tc`, where a trained one would step out.
entt::entity Muster(ECS::Simulation& sim, int unit_id, entt::entity tc) {
    Vector2D at{};
    if (!ECS::Buildings::Doorstep(sim.GetRegistry(), sim.GetGrid(), tc, at)) return entt::null;
    return ECS::Units::Spawn(sim.GetRegistry(), unit_id, kAttacker, at);
}

bool Aboard(const entt::registry& reg, entt::entity unit, entt::entity ship) {
    const auto* inside = reg.try_get<ECS::Components::Garrisoned>(unit);
    return inside != nullptr && inside->building == ship && !reg.all_of<ECS::Components::Transform>(unit);
}

} // namespace

// A landing on Islands that ends in conquest. A transport trained at the
// first player's Dock takes on a war party under the dat's garrison rules --
// every land unit's garrison task names the transport's class; ships have
// none; five fit, as its capacity says -- sails it round to the other island,
// stays on the sea the whole way, and puts it ashore on that island's land.
// The party burns the town and kills its people, and the game ends in
// conquest for the first player although the second still has a fishing ship
// and a transport afloat, which conquest does not count. Finally a transport
// sunk with a soldier aboard takes him down with it.
bool CheckLanding() {
    const char* verdict = "ok";
    auto ok = [&]() { return std::strcmp(verdict, "ok") == 0; };
    auto& cfg = Core::ConfigManager::Get();
    const int dock_id = cfg.GetUnitIdByName("DOCK");
    const int transport_id = cfg.GetUnitIdByName("XPORT");
    const int galley_id = cfg.GetUnitIdByName("SGALY");
    const int fishing_id = cfg.GetUnitIdByName("FSHSP");
    const int ram_id = cfg.GetUnitIdByName("BTRAM");
    const int knight_id = cfg.GetUnitIdByName("KNGHT");
    const int villager_id = cfg.GetUnitIdByName("VMBAS");
    const int tc_id = cfg.GetUnitIdByName("RTWC");

    Core::GameSetup setup = Core::DefaultSetup();
    setup.map_script = "Islands";
    setup.starting_age = 2;
    ECS::Simulation sim;
    sim.Init(setup);
    auto& reg = sim.GetRegistry();
    auto& grid = sim.GetGrid();
    const Physics::Layer sea = Shore::Sea(sim);
    const auto [ax, ay] = sim.player_starts[0];
    const auto [dx, dy] = sim.player_starts[1];
    const entt::entity home = FirstOf(reg, tc_id, kAttacker);
    const entt::entity target = FirstOf(reg, tc_id, kDefender);
    const int their_island = Pathfinding::RegionAt(grid, dx + 3, dy + 3);
    if (home == entt::null || target == entt::null || their_island < 0) {
        std::printf("landing=Islands has no two towns to try\n");
        return false;
    }

    // A transport, trained at a Dock on the first island.
    const entt::entity dock = Shore::RaiseNear(sim, kAttacker, dock_id, ax, ay, 40);
    entt::entity ship = entt::null;
    if (dock == entt::null) verdict = "no shore on the first island takes a dock";
    if (ok()) {
        Core::CommandQueue::Get().PushCommand(Core::TrainCommand{dock, transport_id, kAttacker});
        const auto* rules = Core::Rules::Unit(transport_id);
        for (int t = 0; t < ((rules ? rules->train_time : 60) + 5) * Shore::kTicksPerSecond &&
                        ship == entt::null; ++t) {
            sim.Update(16);
            ship = FirstOf(reg, transport_id, kAttacker);
        }
        if (ship == entt::null) verdict = "the dock never launched the transport";
    }

    // The war party: two rams for the town, three knights for its people.
    std::vector<entt::entity> party;
    if (ok()) {
        for (int unit : {ram_id, ram_id, knight_id, knight_id, knight_id}) {
            const entt::entity e = Muster(sim, unit, home);
            if (e == entt::null) verdict = "no room by the Town Center for the war party";
            party.push_back(e);
        }
    }
    // Who may board: a land unit may, a ship may not.
    if (ok()) {
        const entt::entity galley = ECS::Units::Spawn(
            reg, galley_id, kAttacker, reg.get<ECS::Components::Transform>(ship).position);
        if (ECS::Garrisons::Accepts(reg, ship, galley)) verdict = "a galley may board a transport";
        if (!ECS::Garrisons::Accepts(reg, ship, party.front())) verdict = "a ram may not board a transport";
        reg.destroy(galley);
    }

    // The transport stands off out at sea first, so that it has to come in
    // to the shore for the party rather than happening to lie against it.
    if (ok()) {
        const Vector2D at = reg.get<ECS::Components::Transform>(ship).position;
        Vector2D out{};
        bool found = false;
        for (int r = 6; r <= 30 && !found; ++r) {
            for (int i = 0; i < 8 * r && !found; ++i) {
                // Round the ring of tiles r out, side by side.
                const int side = i / (2 * r), step = i % (2 * r) - r;
                const int ox0 = side == 0 ? step : side == 1 ? r : side == 2 ? -step : -r;
                const int oy0 = side == 0 ? -r : side == 1 ? step : side == 2 ? r : -step;
                const int x = Physics::TileOf(at.x) + ox0;
                const int y = Physics::TileOf(at.y) + oy0;
                bool open = true;
                for (int oy = -2; oy <= 2 && open; ++oy) {
                    for (int ox = -2; ox <= 2 && open; ++ox) {
                        open = grid.Passable(sea, x + ox, y + oy) && !grid.IsWalkable(x + ox, y + oy);
                    }
                }
                if (open && Pathfinding::RegionAt(grid, x, y, sea) ==
                                ECS::Navigation::RegionOf(reg, grid, ship)) {
                    out = Shore::At(x, y);
                    found = true;
                }
            }
        }
        if (!found) {
            verdict = "no open sea off the first island";
        } else {
            Core::CommandQueue::Get().PushCommand(Core::MoveCommand{ship, out.x, out.y, kAttacker});
            for (int t = 0; t < 60 * Shore::kTicksPerSecond; ++t) sim.Update(16);
            const Vector2D now = reg.get<ECS::Components::Transform>(ship).position;
            if (Shore::Apart(now, out) > Fixed32(1)) verdict = "the transport never stood off";
        }
    }

    // Aboard: each walks down to the shore, and the ship comes in for them.
    if (ok()) {
        for (entt::entity e : party) {
            Core::CommandQueue::Get().PushCommand(Core::GarrisonCommand{e, ship, kAttacker});
        }
        bool all_aboard = false;
        for (int t = 0; t < 240 * Shore::kTicksPerSecond && !all_aboard; ++t) {
            sim.Update(16);
            all_aboard = true;
            for (entt::entity e : party) all_aboard = all_aboard && Aboard(reg, e, ship);
        }
        if (!all_aboard) {
            verdict = "the war party never got aboard";
        } else if (ECS::Garrisons::Count(reg, ship) != 5) {
            verdict = "the transport does not hold the party";
        } else {
            // Full: the dat's capacity is five.
            const entt::entity extra = Muster(sim, villager_id, home);
            if (extra != entt::null && ECS::Garrisons::Accepts(reg, ship, extra)) {
                verdict = "a full transport took a sixth";
            }
            if (extra != entt::null) reg.destroy(extra);
        }
    }

    // The other side's boats, afloat on the open sea, which conquest leaves.
    if (ok()) {
        Vector2D open{};
        bool found = false;
        for (int r = 6; r < grid.GetWidth() && !found; ++r) {
            const int x = (ax + dx) / 2, y = (ay + dy) / 2 + r % 20 - 10;
            if (grid.Passable(sea, x, y) && !grid.IsWalkable(x, y) &&
                grid.Passable(sea, x + 1, y + 1) && !grid.IsWalkable(x + 1, y + 1)) {
                open = Shore::At(x, y);
                found = true;
            }
        }
        if (!found) {
            verdict = "no open sea between the islands";
        } else {
            ECS::Units::Spawn(reg, fishing_id, kDefender, open);
            ECS::Units::Spawn(reg, transport_id, kDefender, {open.x + Fixed32(1), open.y + Fixed32(1)});
        }
    }

    // Across: unloaded at the other island, on its land.
    if (ok()) {
        Core::CommandQueue::Get().PushCommand(
            Core::UnloadCommand{ship, Fixed32(dx), Fixed32(dy), kAttacker});
        bool ashore = false;
        for (int t = 0; t < 300 * Shore::kTicksPerSecond && ok() && !ashore; ++t) {
            sim.Update(16);
            if (!reg.valid(ship) || !reg.all_of<ECS::Components::Transform>(ship)) {
                verdict = "the transport was lost on the way";
                break;
            }
            const Vector2D at = reg.get<ECS::Components::Transform>(ship).position;
            if (!grid.Passable(sea, Physics::TileOf(at.x), Physics::TileOf(at.y))) {
                verdict = "the transport sailed onto the land";
            }
            ashore = ECS::Garrisons::Count(reg, ship) == 0;
        }
        if (ok() && !ashore) verdict = "the transport never put its load ashore";
        for (entt::entity e : party) {
            if (!ok()) break;
            if (!reg.valid(e) || !reg.all_of<ECS::Components::Transform>(e)) {
                verdict = "a soldier was left aboard";
                break;
            }
            const Vector2D at = reg.get<ECS::Components::Transform>(e).position;
            if (Pathfinding::RegionAt(grid, Physics::TileOf(at.x), Physics::TileOf(at.y)) != their_island) {
                verdict = "the party was put ashore off the other island";
            }
        }
    }

    // The assault: the rams at the Town Center; the knights find their own.
    if (ok()) {
        for (entt::entity e : party) {
            if (reg.get<ECS::Components::Identity>(e).unit_id == ram_id) {
                Core::CommandQueue::Get().PushCommand(Core::AttackCommand{e, target, kAttacker});
            }
        }
        // Every few seconds, whoever of the party stands idle goes after the
        // nearest of the defenders' things still standing -- the boats aside.
        const auto* outcome = ECS::Victory::Read(reg);
        for (int t = 1; t < 400 * Shore::kTicksPerSecond && outcome != nullptr && !outcome->over; ++t) {
            sim.Update(16);
            if (t % (5 * Shore::kTicksPerSecond) != 0) continue;
            for (entt::entity e : party) {
                if (!ECS::Combat::Alive(reg, e)) continue;
                const auto* fight = reg.try_get<ECS::Components::Combat>(e);
                if (fight == nullptr || fight->state != ECS::Components::CombatState::Idle) continue;
                const Vector2D from = reg.get<ECS::Components::Transform>(e).position;
                entt::entity nearest = entt::null;
                Fixed32 best(1 << 20);
                auto theirs = reg.view<ECS::Components::Owner, ECS::Components::Transform,
                                       ECS::Components::Identity>();
                for (auto x : theirs) {
                    if (theirs.get<ECS::Components::Owner>(x).player_id != kDefender) continue;
                    const int unit = theirs.get<ECS::Components::Identity>(x).unit_id;
                    if (unit == fishing_id || unit == transport_id) continue;
                    if (!ECS::Combat::Alive(reg, x)) continue;
                    const Fixed32 d = Shore::Apart(theirs.get<ECS::Components::Transform>(x).position, from);
                    if (d < best) { best = d; nearest = x; }
                }
                if (nearest != entt::null) {
                    Core::CommandQueue::Get().PushCommand(Core::AttackCommand{e, nearest, kAttacker});
                }
            }
        }
        if (outcome == nullptr || !outcome->over) {
            verdict = "the landing never ended the game";
        } else if (!outcome->Defeated(kDefender) || !outcome->Won(kAttacker)) {
            verdict = "the landing did not end in conquest for the attacker";
        } else if (FirstOf(reg, fishing_id, kDefender) == entt::null ||
                   FirstOf(reg, transport_id, kDefender) == entt::null) {
            verdict = "the defender's boats were destroyed, not exempted";
        }
    }

    // A transport that sinks takes those aboard with it.
    if (ok()) {
        entt::entity knight = entt::null;
        for (entt::entity e : party) {
            if (ECS::Combat::Alive(reg, e) && reg.get<ECS::Components::Identity>(e).unit_id == knight_id) {
                knight = e;
            }
        }
        if (knight == entt::null) {
            verdict = "no knight came through the assault";
        } else {
            Core::CommandQueue::Get().PushCommand(Core::GarrisonCommand{knight, ship, kAttacker});
            for (int t = 0; t < 120 * Shore::kTicksPerSecond && !Aboard(reg, knight, ship); ++t) {
                sim.Update(16);
            }
            if (!Aboard(reg, knight, ship)) {
                verdict = "a knight on the shore could not board the transport waiting there";
            } else {
                ECS::Mortality::Kill(reg, grid, ship);
                for (int t = 0; t < 10 * Shore::kTicksPerSecond; ++t) sim.Update(16);
                if (reg.valid(knight) && !reg.all_of<ECS::Components::Dying>(knight)) {
                    verdict = "a soldier aboard outlived his sunken transport";
                }
            }
        }
    }

    std::printf("landing=%s\n", verdict);
    return ok();
}

} // namespace Verify
