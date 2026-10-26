#include "verify/Harness.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "core/ConfigManager.h"
#include "core/GameSetup.h"
#include "core/Rules.h"
#include "ecs/Combat.h"
#include "ecs/Orders.h"
#include "ecs/Players.h"
#include "ecs/Production.h"
#include "ecs/Simulation.h"
#include "ecs/Techs.h"
#include "ecs/Units.h"
#include "ecs/components/Combat.h"
#include "ecs/components/Health.h"
#include "ecs/components/Mortal.h"
#include "ecs/components/Projectile.h"
#include "ecs/components/Transform.h"
#include "verify/Shore.h"

namespace Verify {

namespace {

using Core::Fixed32;
using ECS::Components::Vector2D;
namespace R = Core::Rules;

// The middle of a stretch of open sea `half` tiles each way, with no land in
// it, nearest the map's middle; false if the map has none.
bool OpenSea(const ECS::Simulation& sim, int half, Vector2D& out) {
    const auto& grid = const_cast<ECS::Simulation&>(sim).GetGrid();
    const Physics::Layer sea = Shore::Sea(sim);
    const int cx = grid.GetWidth() / 2, cy = grid.GetHeight() / 2;
    for (int r = 0; r < grid.GetWidth() / 2; ++r) {
        for (int dy = -r; dy <= r; ++dy) {
            for (int dx = -r; dx <= r; ++dx) {
                if (std::abs(dx) != r && std::abs(dy) != r) continue;
                bool open = true;
                for (int oy = -half; oy <= half && open; ++oy) {
                    for (int ox = -half; ox <= half && open; ++ox) {
                        const int x = cx + dx + ox, y = cy + dy + oy;
                        open = grid.Passable(sea, x, y) && !grid.IsWalkable(x, y);
                    }
                }
                if (open) {
                    out = Shore::At(cx + dx, cy + dy);
                    return true;
                }
            }
        }
    }
    return false;
}

Vector2D Offset(Vector2D at, int dx, int dy) {
    return {at.x + Fixed32(dx), at.y + Fixed32(dy)};
}

bool Gone(const entt::registry& reg, entt::entity e) {
    return !reg.valid(e) || reg.all_of<ECS::Components::Dying>(e);
}

int Hp(const entt::registry& reg, entt::entity e) {
    return reg.valid(e) && reg.all_of<ECS::Components::Health>(e)
        ? reg.get<ECS::Components::Health>(e).current_hp : 0;
}

void Run(ECS::Simulation& sim, int seconds) {
    for (int t = 0; t < seconds * Shore::kTicksPerSecond; ++t) sim.Update(16);
}

// Removes every unit near `at` so a fight there is only the ships set down.
void ClearAround(ECS::Simulation& sim, Vector2D at, int radius) {
    auto& reg = sim.GetRegistry();
    std::vector<entt::entity> near;
    auto units = reg.view<ECS::Components::Transform, ECS::Components::Health>();
    for (auto e : units) {
        if (Shore::Apart(units.get<ECS::Components::Transform>(e).position, at) < Fixed32(radius)) {
            near.push_back(e);
        }
    }
    for (auto e : near) reg.destroy(e);
}

} // namespace

// The warships and the Dock's technologies, from the dat. In the Castle Age a
// Dock trains the galley, fire ship, demolition ship and transport the ages
// enabled; a galley seeks out an enemy ship and sinks it with arrows that
// fly; a fire ship burns a galley down before the galley's arrows tell; a
// demolition ship detonates -- spent in its one blow -- hurting every enemy
// ship in its blast and none of its own side's; and Careening, researched at
// the Dock, gives ships the dat's pierce armour and transports their room.
bool CheckNavy() {
    const char* verdict = "ok";
    auto ok = [&]() { return std::strcmp(verdict, "ok") == 0; };
    auto& cfg = Core::ConfigManager::Get();
    const int dock_id = cfg.GetUnitIdByName("DOCK");
    const int galley_id = cfg.GetUnitIdByName("SGALY");
    const int fire_id = cfg.GetUnitIdByName("FRGAL");
    const int demo_id = cfg.GetUnitIdByName("RMSHP");
    const int transport_id = cfg.GetUnitIdByName("XPORT");
    const int fishing_id = cfg.GetUnitIdByName("FSHSP");

    Core::GameSetup setup = Core::DefaultSetup();
    setup.map_script = "Islands";
    setup.starting_age = 2;
    ECS::Simulation sim;
    sim.Init(setup);
    auto& reg = sim.GetRegistry();
    auto& grid = sim.GetGrid();
    const Physics::Layer sea = Shore::Sea(sim);

    // The Dock trains what the ages enabled.
    const auto [sx, sy] = sim.player_starts[0];
    const entt::entity dock = Shore::RaiseNear(sim, 1, dock_id, sx, sy, 40);
    if (dock == entt::null) verdict = "no shore takes a dock";
    if (ok()) {
        for (int unit : {galley_id, fire_id, demo_id, transport_id, fishing_id}) {
            auto* me = ECS::Players::Find(reg, 1);
            for (auto& r : me->resources) r = r + Fixed32(1000);
            if (ECS::Production::Enqueue(reg, dock, unit, 1) != ECS::Production::Refusal::None) {
                verdict = "the Castle Age dock refused a ship the ages enable";
            }
        }
        ECS::Production::Abandon(reg, dock);
    }

    // Open sea for the fights, cleared of anything that swims there.
    Vector2D sea_at{};
    if (ok() && !OpenSea(sim, 5, sea_at)) verdict = "Islands has no open sea";

    // A galley against an enemy fishing ship: it goes after it unbidden and
    // sinks it with arrows in flight.
    if (ok()) {
        ClearAround(sim, sea_at, 12);
        const entt::entity galley = ECS::Units::Spawn(reg, galley_id, 1, sea_at);
        const entt::entity prey = ECS::Units::Spawn(reg, fishing_id, 2, Offset(sea_at, 4, 0));
        bool loosed = false;
        bool aground = false;
        for (int t = 0; t < 60 * Shore::kTicksPerSecond && !Gone(reg, prey); ++t) {
            sim.Update(16);
            if (!reg.view<ECS::Components::Projectile>().empty()) loosed = true;
            const auto& p = reg.get<ECS::Components::Transform>(galley).position;
            if (!grid.Passable(sea, Physics::TileOf(p.x), Physics::TileOf(p.y))) aground = true;
        }
        if (!loosed) verdict = "the galley loosed no arrows";
        else if (!Gone(reg, prey)) verdict = "the galley never sank the fishing ship";
        else if (aground) verdict = "the galley left the sea";
        Run(sim, 3);
        if (reg.valid(galley)) reg.destroy(galley);
    }

    // A fire ship against a galley: the galley burns first.
    if (ok()) {
        ClearAround(sim, sea_at, 12);
        const entt::entity fire = ECS::Units::Spawn(reg, fire_id, 1, sea_at);
        const entt::entity galley = ECS::Units::Spawn(reg, galley_id, 2, Offset(sea_at, 2, 0));
        for (int t = 0; t < 60 * Shore::kTicksPerSecond && !Gone(reg, galley) && !Gone(reg, fire); ++t) {
            sim.Update(16);
        }
        if (!Gone(reg, galley)) verdict = "the fire ship did not burn the galley down";
        else if (Gone(reg, fire)) verdict = "the galley outlasted the fire ship";
        Run(sim, 3);
        if (reg.valid(fire)) reg.destroy(fire);
    }

    // A demolition ship among two enemy galleys and one of its own: it
    // detonates, both enemies are hurt by the blast, its own side's is not.
    if (ok()) {
        ClearAround(sim, sea_at, 12);
        const entt::entity demo = ECS::Units::Spawn(reg, demo_id, 1, Offset(sea_at, -3, 0));
        const entt::entity a = ECS::Units::Spawn(reg, galley_id, 2, sea_at);
        const entt::entity b = ECS::Units::Spawn(reg, galley_id, 2, Offset(sea_at, 0, 1));
        const entt::entity own = ECS::Units::Spawn(reg, galley_id, 1, Offset(sea_at, 1, 0));
        // The galleys all hold their fire, so only the blast can hurt anyone.
        for (entt::entity hull : {a, b, own}) {
            if (auto* combat = reg.try_get<ECS::Components::Combat>(hull)) {
                combat->stance = ECS::Components::Stance::NoAttack;
            }
        }
        const int a0 = Hp(reg, a), b0 = Hp(reg, b), own0 = Hp(reg, own);
        for (int t = 0; t < 30 * Shore::kTicksPerSecond && !Gone(reg, demo); ++t) sim.Update(16);
        const R::UnitRules* rules = R::Unit(demo_id);
        const R::UnitRules* hull = R::Unit(galley_id);
        const int blow = rules != nullptr && hull != nullptr
            ? ECS::Combat::Damage(rules->attacks, rules->attack_count, *hull) : 0;
        if (!Gone(reg, demo)) {
            verdict = "the demolition ship never detonated";
        } else if (a0 - Hp(reg, a) < blow || b0 - Hp(reg, b) < blow) {
            verdict = "the blast did not reach both enemy galleys";
        } else if (Hp(reg, own) != own0) {
            verdict = "the blast hurt its own side's galley";
        }
        Run(sim, 3);
    }

    // Careening at the Dock: the dat's pierce armour for ships, and room in
    // transports.
    if (ok()) {
        constexpr int kCareening = 374;
        constexpr int kPierce = 3;
        auto* me = ECS::Players::Find(reg, 1);
        for (auto& r : me->resources) r = r + Fixed32(2000);
        const auto* before = ECS::Players::UnitOf(reg, 1, galley_id);
        const int armour = R::ClassValue(before->armours, before->armour_count, kPierce, 0);
        const int room = ECS::Players::UnitOf(reg, 1, transport_id)->garrison;
        const R::TechRules* tech = R::Tech(kCareening);
        if (tech == nullptr || tech->location != dock_id) {
            verdict = "Careening is not the Dock's";
        } else if (ECS::Production::EnqueueTech(reg, dock, kCareening, 1) !=
                   ECS::Production::Refusal::None) {
            verdict = "the dock refused Careening";
        } else {
            Run(sim, tech->time + 2);
            const auto* after = ECS::Players::UnitOf(reg, 1, galley_id);
            if (!ECS::Techs::Researched(reg, 1, kCareening)) {
                verdict = "Careening never finished";
            } else if (R::ClassValue(after->armours, after->armour_count, kPierce, 0) != armour + 1) {
                verdict = "Careening did not add a ship's pierce armour";
            } else if (ECS::Players::UnitOf(reg, 1, transport_id)->garrison != room + 5) {
                verdict = "Careening did not make room in transports";
            }
        }
    }

    // The galley line: War Galley, researched at the Dock, upgrades the
    // player's galley slot in place -- the War Galley's hit points, for a
    // galley already afloat too.
    if (ok()) {
        constexpr int kWarGalley = 34;
        auto* me = ECS::Players::Find(reg, 1);
        for (auto& r : me->resources) r = r + Fixed32(2000);
        const entt::entity afloat = ECS::Units::Spawn(reg, galley_id, 1, sea_at);
        const R::TechRules* tech = R::Tech(kWarGalley);
        const R::UnitRules* upgraded = R::Unit(cfg.GetUnitIdByName("GALLY"));
        if (tech == nullptr || upgraded == nullptr || tech->location != dock_id) {
            verdict = "War Galley is not the Dock's";
        } else if (ECS::Production::EnqueueTech(reg, dock, kWarGalley, 1) !=
                   ECS::Production::Refusal::None) {
            verdict = "the Castle Age dock refused War Galley";
        } else {
            Run(sim, tech->time + 2);
            if (ECS::Players::UnitOf(reg, 1, galley_id)->hp != upgraded->hp) {
                verdict = "War Galley did not upgrade the galley";
            } else if (!reg.valid(afloat) ||
                       reg.get<ECS::Components::Health>(afloat).max_hp != upgraded->hp) {
                verdict = "a galley afloat was not upgraded to a War Galley";
            }
        }
        if (reg.valid(afloat)) reg.destroy(afloat);
    }

    // A cannon galleon bombards an enemy Dock from range with its shot.
    if (ok()) {
        const int cannon_id = cfg.GetUnitIdByName("CANGA");
        const R::UnitRules* cannon = R::Unit(cannon_id);
        const auto [ex, ey] = sim.player_starts[1];
        const entt::entity target = Shore::RaiseNear(sim, 2, dock_id, ex, ey, 40);
        if (cannon == nullptr || target == entt::null) {
            verdict = "no cannon galleon, or no shore for the enemy's dock";
        } else {
            // Out on the water beyond its range of the dock, so it has to close
            // in on its own sea to fire.
            const auto& at = reg.get<ECS::Components::Transform>(target).position;
            Vector2D post{};
            bool found = false;
            for (int r = 18; r <= 26 && !found; ++r) {
                for (int b = 0; b < 8 && !found; ++b) {
                    static constexpr int kDx[8] = {1, 1, 0, -1, -1, -1, 0, 1};
                    static constexpr int kDy[8] = {0, 1, 1, 1, 0, -1, -1, -1};
                    const int x = Physics::TileOf(at.x) + kDx[b] * r;
                    const int y = Physics::TileOf(at.y) + kDy[b] * r;
                    if (grid.Passable(sea, x, y) && !grid.IsWalkable(x, y)) {
                        post = Shore::At(x, y);
                        found = true;
                    }
                }
            }
            if (!found) {
                verdict = "no water in range of the enemy dock";
            } else {
                const entt::entity ship = ECS::Units::Spawn(reg, cannon_id, 1, post);
                // Ordered at the dock: left to itself it would pick the
                // enemy's people on the shore first, by combat level.
                ECS::Orders::Execute(reg, grid, Core::AttackCommand{ship, target, 1});
                const int before = Hp(reg, target);
                bool shot = false;
                for (int t = 0; t < 60 * Shore::kTicksPerSecond && !shot; ++t) {
                    sim.Update(16);
                    auto missiles = reg.view<ECS::Components::Projectile>();
                    for (auto m : missiles) {
                        if (missiles.get<ECS::Components::Projectile>(m).unit_id == cannon->projectile) shot = true;
                    }
                }
                Run(sim, 12);   // a cannonball is slow: 1.95 tiles a second
                if (!shot) verdict = "the cannon galleon never fired its shot";
                else if (Hp(reg, target) >= before) verdict = "the cannon galleon's shot did no harm";
                if (reg.valid(ship)) reg.destroy(ship);
            }
        }
    }

    std::printf("navy=%s\n", verdict);
    return ok();
}

} // namespace Verify
