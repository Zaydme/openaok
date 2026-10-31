#include "verify/Harness.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "core/Command.h"
#include "core/CommandQueue.h"
#include "core/ConfigManager.h"
#include "core/FixedMath.h"
#include "core/Rules.h"
#include "ecs/BuildingGeometry.h"
#include "ecs/Buildings.h"
#include "ecs/Combat.h"
#include "ecs/Garrisons.h"
#include "ecs/Missiles.h"
#include "ecs/Mortality.h"
#include "ecs/Players.h"
#include "ecs/Simulation.h"
#include "ecs/Units.h"
#include "ecs/components/Combat.h"
#include "ecs/components/Garrison.h"
#include "ecs/components/Health.h"
#include "ecs/components/Identity.h"
#include "ecs/components/Owner.h"
#include "ecs/components/Projectile.h"
#include "ecs/components/Resource.h"
#include "ecs/components/Transform.h"
#include "ecs/components/Worker.h"
#include "rendering/UIRenderer.h"

namespace Verify {

namespace {

constexpr Core::PlayerId kUs = 1;
constexpr Core::PlayerId kThem = 2;
constexpr int kTicksPerSecond = 1000 / 16;

entt::entity TownCenterOf(const entt::registry& reg, Core::PlayerId player, int tc_id) {
    auto view = reg.view<const ECS::Components::Identity, const ECS::Components::Owner>();
    for (auto e : view) {
        if (view.get<const ECS::Components::Identity>(e).unit_id == tc_id &&
            view.get<const ECS::Components::Owner>(e).player_id == player) {
            return e;
        }
    }
    return entt::null;
}

std::vector<entt::entity> VillagersOf(const entt::registry& reg, Core::PlayerId player) {
    std::vector<entt::entity> out;
    auto view = reg.view<const ECS::Components::Worker, const ECS::Components::Owner,
                         const ECS::Components::Transform>();
    for (auto e : view) {
        if (view.get<const ECS::Components::Owner>(e).player_id == player) out.push_back(e);
    }
    return out;
}

int ArrowsFrom(const entt::registry& reg, entt::entity shooter, int missile_id) {
    int n = 0;
    auto view = reg.view<const ECS::Components::Projectile>();
    for (auto e : view) {
        const auto& p = view.get<const ECS::Components::Projectile>(e);
        if (p.shooter == shooter && (missile_id < 0 || p.unit_id == missile_id)) ++n;
    }
    return n;
}

bool Inside(const entt::registry& reg, entt::entity unit) {
    return reg.all_of<ECS::Components::Garrisoned>(unit) &&
           !reg.all_of<ECS::Components::Transform>(unit);
}

} // namespace

// Sheltering in buildings, from the dat's capacities and types: going in and
// leaving the world, refusals by kind and by room, a Town Center that shoots
// only with a garrison and an arrow per villager in it, healing inside,
// turning out at the door, the town bell there and back, and a garrison
// surviving its building.
bool CheckGarrison() {
    ECS::Simulation headless;
    headless.Init();
    auto& reg = headless.GetRegistry();
    auto& grid = headless.GetGrid();
    auto& cfg = Core::ConfigManager::Get();
    auto& queue = Core::CommandQueue::Get();
    const char* verdict = "ok";
    auto ok = [&]() { return std::strcmp(verdict, "ok") == 0; };
    auto run = [&](int ticks) {
        for (int t = 0; t < ticks; ++t) headless.Update(16);
    };

    const int tc_id = cfg.GetUnitIdByName("RTWC");
    const int villager_id = cfg.GetUnitIdByName("VMBAS");
    const int scout_id = cfg.GetUnitIdByName("SCOUT");
    const int militia_id = cfg.GetUnitIdByName("SPRMN");
    const entt::entity tc = TownCenterOf(reg, kUs, tc_id);
    const auto* tc_rules = ECS::Players::UnitOf(reg, kUs, tc_id);
    if (tc == entt::null || tc_rules == nullptr) {
        std::printf("garrison=no town center\n");
        return false;
    }
    const auto ground = ECS::Buildings::FootprintBounds(
        reg.get<ECS::Components::Transform>(tc).position, tc_id);
    const ECS::Components::Vector2D middle{(ground.min_x + ground.max_x) / Core::Fixed32(2),
                                           (ground.min_y + ground.max_y) / Core::Fixed32(2)};

    // The info panel tells how many are inside our own Town Center only.
    const entt::entity theirs = TownCenterOf(reg, kThem, tc_id);
    if (!Rendering::UIRenderer::ShowsGarrison(reg, tc)) {
        verdict = "the info panel did not tell what is inside our own Town Center";
    } else if (theirs == entt::null || Rendering::UIRenderer::ShowsGarrison(reg, theirs)) {
        verdict = "the info panel told what is inside an enemy's Town Center";
    }

    // Nobody of ours but the Town Center may hurt the target: the starting
    // scout would ride out and do it.
    {
        auto fighters = reg.view<ECS::Components::Combat, const ECS::Components::Owner>();
        for (auto e : fighters) {
            if (fighters.get<const ECS::Components::Owner>(e).player_id == kUs && e != tc) {
                fighters.get<ECS::Components::Combat>(e).stance = ECS::Components::Stance::NoAttack;
            }
        }
    }

    // An enemy standing in range, told never to fight, as a target.
    const entt::entity target = ECS::Units::Spawn(
        reg, militia_id, kThem, {ground.max_x + Core::Fixed32(3), middle.y});
    reg.get<ECS::Components::Combat>(target).stance = ECS::Components::Stance::NoAttack;
    const int target_hp = reg.get<ECS::Components::Health>(target).current_hp;

    // Empty, a Town Center does not shoot.
    run(4 * kTicksPerSecond);
    if (ECS::Missiles::VolleyOf(reg, tc).Total() != 0 ||
        reg.get<ECS::Components::Health>(target).current_hp != target_hp ||
        ArrowsFrom(reg, tc, -1) != 0) {
        verdict = "an empty town center shot";
    }

    // A villager sent in walks there and leaves the world.
    std::vector<entt::entity> villagers = VillagersOf(reg, kUs);
    if (ok() && villagers.empty()) verdict = "no villagers to garrison";
    const entt::entity first = ok() ? villagers.front() : entt::null;
    const auto* us = ECS::Players::Find(reg, kUs);
    const int population = ECS::Players::Resource(*us, Core::Rules::Res::CurrentPopulation).ToInt();
    if (ok()) {
        queue.PushCommand(Core::GarrisonCommand{first, tc, kUs});
        run(20 * kTicksPerSecond);
        if (!Inside(reg, first) || ECS::Garrisons::Count(reg, tc) != 1) {
            verdict = "a villager sent into its town center did not go in";
        } else if (ECS::Players::Resource(*us, Core::Rules::Res::CurrentPopulation).ToInt() !=
                   population) {
            verdict = "a garrisoned villager left the population";
        }
    }

    // Cavalry is not taken: the Town Center's garrison type has no cavalry.
    if (ok()) {
        const entt::entity scout = ECS::Units::Spawn(
            reg, scout_id, kUs, {ground.min_x - Core::Fixed32(2), middle.y});
        queue.PushCommand(Core::GarrisonCommand{scout, tc, kUs});
        run(5 * kTicksPerSecond);
        if (!reg.all_of<ECS::Components::Transform>(scout) ||
            reg.any_of<ECS::Components::Boarding, ECS::Components::Garrisoned>(scout)) {
            verdict = "a town center took in cavalry";
        }
    }


    // How many arrows a garrison of villagers adds, worked from the player's
    // own Town Center: each villager's 2.5 pierce a second over its arrow's
    // pierce over its reload -- the Town Center arrow's 5 every 2 seconds, so
    // a villager each, even for the Teutons, whose Town Center's own attack
    // is 7 -- added to the dat's one missile, which a Town Center does not
    // loose itself, up to the dat's eleven: fifteen villagers loose ten.
    const auto arrows_for = [&](int villagers) {
        const auto* rules = ECS::Players::UnitOf(reg, kUs, tc_id);
        const auto* arrow = ECS::Players::UnitOf(reg, kUs, rules->secondary_projectile);
        int pierce = 0;
        for (int a = 0; a < arrow->attack_count; ++a) {
            if (arrow->attacks[a].cls == 3) pierce += arrow->attacks[a].amount;
        }
        const Core::Fixed32 own = Core::Fixed32(pierce) / rules->reload;
        const Core::Fixed32 lent = Core::Fixed32(villagers) * Core::Fixed32(5) / Core::Fixed32(2) / own;
        return std::min((rules->missiles + lent).ToInt(), rules->max_missiles) - 1;
    };
    if (ok() && (ECS::Missiles::VolleyOf(reg, tc).extra != arrows_for(1) || arrows_for(1) != 1)) {
        verdict = "one villager inside did not lend one arrow";
    }

    // Healing inside, at the building's rate.
    if (ok()) {
        auto& hp = reg.get<ECS::Components::Health>(first);
        hp.current_hp = hp.max_hp - 10;
        const int before = hp.current_hp;
        run(30 * kTicksPerSecond);
        if (reg.get<ECS::Components::Health>(first).current_hp <= before) {
            verdict = "a garrisoned villager did not heal";
        }
    }

    // Room: a crowd sent in fills it to its capacity and no further, and each
    // villager inside is one more arrow, up to the dat's most.
    std::vector<entt::entity> crowd;
    if (ok()) {
        // Each on open ground: the map may have a bush or a tree where the
        // crowd would stand, and a villager put down on one never walks off.
        auto open_near = [&](ECS::Components::Vector2D at) {
            const int tx = Physics::TileOf(at.x), ty = Physics::TileOf(at.y);
            for (int r = 0; r < 6; ++r) {
                for (int dy = -r; dy <= r; ++dy) {
                    for (int dx = -r; dx <= r; ++dx) {
                        if (std::max(std::abs(dx), std::abs(dy)) != r) continue;
                        if (grid.IsWalkable(tx + dx, ty + dy)) {
                            return ECS::Components::Vector2D{at.x + Core::Fixed32(dx), at.y + Core::Fixed32(dy)};
                        }
                    }
                }
            }
            return at;
        };
        for (int i = 0; i < tc_rules->garrison + 4; ++i) {
            const Core::Fixed32 along = Core::Fixed32(i % 6) * Core::Fixed32(6) / Core::Fixed32(10);
            const Core::Fixed32 out = Core::Fixed32(i / 6) * Core::Fixed32(6) / Core::Fixed32(10);
            crowd.push_back(ECS::Units::Spawn(
                reg, villager_id, kUs,
                open_near({ground.min_x - Core::Fixed32(2) - out, ground.min_y + along})));
        }
        for (entt::entity v : crowd) queue.PushCommand(Core::GarrisonCommand{v, tc, kUs});
        run(30 * kTicksPerSecond);
        const int inside = ECS::Garrisons::Count(reg, tc);
        if (inside != tc_rules->garrison) {
            verdict = "a crowd sent into a town center did not fill it exactly";
        } else if (ECS::Missiles::VolleyOf(reg, tc).Total() != arrows_for(inside) ||
                   arrows_for(inside) != 10) {
            verdict = "a full town center loosed the wrong number of arrows";
        }
    }

    // Full, it shoots: arrows of the garrison's own missile, a volley of them,
    // and an enemy in range is hurt -- a fresh one, since the first fell while
    // the crowd went in.
    const int arrow_id = tc_rules->secondary_projectile;
    if (ok()) {
        const entt::entity fresh = ECS::Units::Spawn(
            reg, militia_id, kThem, {middle.x, ground.max_y + Core::Fixed32(3)});
        reg.get<ECS::Components::Combat>(fresh).stance = ECS::Components::Stance::NoAttack;
        const int fresh_hp = reg.get<ECS::Components::Health>(fresh).current_hp;
        int most = 0;
        for (int t = 0; t < 6 * kTicksPerSecond; ++t) {
            headless.Update(16);
            most = std::max(most, ArrowsFrom(reg, tc, arrow_id));
        }
        if (most == 0) {
            verdict = "a garrisoned town center loosed no arrows";
        } else if (most < arrows_for(ECS::Garrisons::Count(reg, tc))) {
            verdict = "a garrisoned town center loosed less than its volley";
        } else if (reg.valid(fresh) && ECS::Combat::Alive(reg, fresh) &&
                   reg.get<ECS::Components::Health>(fresh).current_hp >= fresh_hp) {
            verdict = "a garrisoned town center's arrows hurt nobody";
        }
    }

    // Turned out: everyone back in the world, at the door, not in the walls.
    if (ok()) {
        queue.PushCommand(Core::UngarrisonCommand{tc, kUs});
        run(1);
        if (ECS::Garrisons::Count(reg, tc) != 0) {
            verdict = "a town center turned out still held units";
        }
        auto view = reg.view<const ECS::Components::Worker, const ECS::Components::Transform>();
        for (auto v : view) {
            if (reg.any_of<ECS::Components::Garrisoned>(v)) {
                verdict = "a unit turned out is still marked as inside";
                break;
            }
            if (ground.DistanceFrom(view.get<const ECS::Components::Transform>(v).position).value <= 0) {
                verdict = "a unit turned out stands inside the building";
                break;
            }
        }
    }

    // The town bell: villagers at work nearby take cover, and go back to the
    // same work when it rings again.
    if (ok()) {
        // Everyone at the nearest tree.
        entt::entity tree = entt::null;
        Core::Fixed32 nearest{};
        auto trees = reg.view<const ECS::Components::Resource, const ECS::Components::Transform>();
        for (auto r : trees) {
            if (trees.get<const ECS::Components::Resource>(r).type != Core::ResourceType::Wood) {
                continue;
            }
            const auto& p = trees.get<const ECS::Components::Transform>(r).position;
            const Core::Fixed32 d = Core::FixedMath::Distance(p.x - middle.x, p.y - middle.y);
            if (tree == entt::null || d < nearest) {
                tree = r;
                nearest = d;
            }
        }
        // The crowd goes, or the bell would rightly fill the Town Center with
        // them -- it takes the villagers nearest a shelter first.
        for (entt::entity v : crowd) ECS::Mortality::Kill(reg, grid, v);
        std::vector<entt::entity> workers;
        for (entt::entity v : VillagersOf(reg, kUs)) {
            if (workers.size() >= 4) break;
            workers.push_back(v);
            queue.PushCommand(Core::GatherCommand{v, tree, kUs});
        }
        run(5 * kTicksPerSecond);
        queue.PushCommand(Core::TownBellCommand{tc, kUs});
        run(40 * kTicksPerSecond);
        for (entt::entity v : workers) {
            if (!Inside(reg, v) || !reg.get<ECS::Components::Garrisoned>(v).bell) {
                verdict = "a villager at work did not answer the town bell";
                break;
            }
        }
        if (ok()) {
            queue.PushCommand(Core::TownBellCommand{tc, kUs});
            run(2);
            for (entt::entity v : workers) {
                const auto* worker = reg.try_get<ECS::Components::Worker>(v);
                if (!reg.all_of<ECS::Components::Transform>(v) || worker == nullptr ||
                    worker->state == Core::UnitState::Idle || worker->target_resource != tree) {
                    verdict = "the bell rung again did not send villagers back to their work";
                    break;
                }
            }
        }
    }

    // A garrison outlives its building.
    if (ok()) {
        queue.PushCommand(Core::GarrisonCommand{first, tc, kUs});
        run(20 * kTicksPerSecond);
        if (!Inside(reg, first)) {
            verdict = "a villager did not go back in";
        } else {
            ECS::Buildings::Destroy(reg, grid, tc);
            if (!reg.all_of<ECS::Components::Transform>(first) ||
                reg.any_of<ECS::Components::Garrisoned>(first)) {
                verdict = "a garrison was lost with its building";
            }
        }
    }


    // A Watch Tower's arrows: an archer lends 4 pierce every 2 seconds over
    // the tower arrow's 5 every 2, so one inside adds no whole arrow and two
    // add one; swordsmen beside them add nothing.
    if (ok()) {
        ECS::Simulation fresh;
        fresh.Init();
        auto& freg = fresh.GetRegistry();
        auto& fgrid = fresh.GetGrid();
        const int tower_id = cfg.GetUnitIdByName("WCTW");
        entt::entity tower = entt::null;
        for (int y = 30; y < 90 && tower == entt::null; y += 3) {
            for (int x = 30; x < 90 && tower == entt::null; x += 3) {
                if (ECS::Buildings::CanPlace(fgrid, tower_id, x, y)) {
                    tower = ECS::Buildings::Place(freg, fgrid, tower_id, kUs, x, y, true);
                }
            }
        }
        const ECS::Components::Vector2D near{Core::Fixed32(20), Core::Fixed32(20)};
        auto put_in = [&](const char* unit) {
            ECS::Garrisons::PutInside(freg, ECS::Units::Spawn(freg, cfg.GetUnitIdByName(unit), kUs, near), tower);
        };
        if (tower == entt::null) {
            verdict = "no watch tower to garrison";
        } else {
            const int alone = ECS::Missiles::VolleyOf(freg, tower).Total();
            put_in("ARCHR");
            const int one = ECS::Missiles::VolleyOf(freg, tower).Total();
            put_in("ARCHR");
            const int two = ECS::Missiles::VolleyOf(freg, tower).Total();
            put_in("SPRMN");
            put_in("SPRMN");
            const int swords = ECS::Missiles::VolleyOf(freg, tower).Total();
            if (alone != 1) {
                verdict = "an empty watch tower did not loose its one arrow";
            } else if (one != 1) {
                verdict = "one archer in a watch tower added a whole arrow";
            } else if (two != 2) {
                verdict = "two archers in a watch tower did not add an arrow";
            } else if (swords != 2) {
                verdict = "swordsmen in a watch tower added arrows";
            }
        }
    }

    // A King (class 59) goes in where villagers do: a Regicide player hides the
    // King in a building. In a world of its own, the one above having filled
    // and fought.
    if (ok()) {
        ECS::Simulation fresh;
        fresh.Init();
        auto& freg = fresh.GetRegistry();
        const entt::entity home = TownCenterOf(freg, kUs, tc_id);
        const int king_id = cfg.GetUnitIdByName("KINGX");
        if (home == entt::null) {
            verdict = "no town center for the King";
        } else {
            const auto walls = ECS::Buildings::FootprintBounds(
                freg.get<ECS::Components::Transform>(home).position, tc_id);
            const entt::entity king = ECS::Units::Spawn(
                freg, king_id, kUs, {walls.min_x - Core::Fixed32(2), walls.min_y});
            if (!ECS::Garrisons::Accepts(freg, home, king)) {
                verdict = "a town center would not take in a King";
            } else {
                queue.PushCommand(Core::GarrisonCommand{king, home, kUs});
                for (int t = 0; t < 8 * kTicksPerSecond; ++t) fresh.Update(16);
                if (!freg.all_of<ECS::Components::Garrisoned>(king)) {
                    verdict = "a King sent into its town center did not go in";
                }
            }
        }
    }

    std::printf("garrison=%s\n", verdict);
    return ok();
}

} // namespace Verify
