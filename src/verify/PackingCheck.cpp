#include "verify/Harness.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "core/Command.h"
#include "core/ConfigManager.h"
#include "core/FixedMath.h"
#include "core/GameSetup.h"
#include "core/Rules.h"
#include "ecs/Buildings.h"
#include "ecs/Combat.h"
#include "ecs/Orders.h"
#include "ecs/Packing.h"
#include "ecs/Players.h"
#include "ecs/ScenarioWorld.h"
#include "ecs/Simulation.h"
#include "ecs/Units.h"
#include "ecs/components/Animation.h"
#include "ecs/components/Building.h"
#include "ecs/components/Combat.h"
#include "ecs/components/Health.h"
#include "ecs/components/Identity.h"
#include "ecs/components/Mortal.h"
#include "ecs/components/Owner.h"
#include "ecs/components/Projectile.h"
#include "ecs/components/Sprite.h"
#include "ecs/components/Transform.h"

namespace Verify {

namespace {

using Core::Fixed32;
using ECS::Components::Vector2D;
constexpr int kTicksPerSecond = 1000 / 16;

bool OpenRow(ECS::Simulation& sim, int half_x, int half_y, int& out_x, int& out_y) {
    auto& grid = sim.GetGrid();
    const int cx = grid.GetWidth() / 2, cy = grid.GetHeight() / 2;
    for (int r = 0; r < grid.GetWidth() / 2; ++r) {
        for (int dy = -r; dy <= r; ++dy) {
            for (int dx = -r; dx <= r; ++dx) {
                if (std::abs(dx) != r && std::abs(dy) != r) continue;
                bool open = true;
                for (int oy = -half_y; oy <= half_y && open; ++oy) {
                    for (int ox = -half_x; ox <= half_x && open; ++ox) {
                        open = grid.IsWalkable(cx + dx + ox, cy + dy + oy);
                    }
                }
                if (open) {
                    out_x = cx + dx;
                    out_y = cy + dy;
                    return true;
                }
            }
        }
    }
    return false;
}

int UnitIdOf(const entt::registry& reg, entt::entity e) {
    return reg.get<ECS::Components::Identity>(e).unit_id;
}

Fixed32 Apart(Vector2D a, Vector2D b) { return Core::FixedMath::Distance(a.x - b.x, a.y - b.y); }

} // namespace

// Trebuchets packing and unpacking, from the dat's transform units: a packed
// trebuchet (PTREB) told to unpack stands still while it sets up -- a move
// order changes nothing -- and becomes the Trebuchet (TREBU) after
// Packing::kPackMs, armed, its wounds in proportion; set up it cannot move;
// packed again it can. A stop order part way leaves it as it was. Sent at an
// enemy's Barracks from beyond range, a packed one closes to within the set-up
// trebuchet's range, sets up there and opens fire -- its Unpack and Attack
// task. Both draw their icons from the units' sheet, and a scenario places
// both as units.
bool CheckPacking() {
    const char* verdict = "ok";
    auto ok = [&]() { return std::strcmp(verdict, "ok") == 0; };
    auto& cfg = Core::ConfigManager::Get();
    const int packed = cfg.GetUnitIdByName("PTREB");
    const int set_up = cfg.GetUnitIdByName("TREBU");
    const int barracks = cfg.GetUnitIdByName("BRKS");

    Core::GameSetup setup = Core::DefaultSetup();
    setup.starting_age = 3;
    ECS::Simulation sim;
    sim.Init(setup);
    auto& reg = sim.GetRegistry();
    auto& grid = sim.GetGrid();
    {
        std::vector<entt::entity> wild;
        auto armed = reg.view<ECS::Components::Combat, ECS::Components::Owner>();
        for (auto e : armed) {
            if (armed.get<ECS::Components::Owner>(e).player_id == Core::PLAYER_GAIA) wild.push_back(e);
        }
        for (auto e : wild) reg.destroy(e);
    }
    int ax = 0, ay = 0;
    if (!OpenRow(sim, 16, 3, ax, ay)) verdict = "no open ground";

    const Vector2D home{Fixed32(ax - 12), Fixed32(ay)};
    const entt::entity treb = ok() ? ECS::Units::Spawn(reg, packed, 1, home) : entt::null;
    if (ok() && treb == entt::null) verdict = "no packed trebuchet";

    // The dat makes both trebuchets buildings by type; their icons are on the
    // units' sheet all the same, and a gate's, which also turns into another
    // unit, on the buildings'.
    if (ok() && (cfg.UsesBuildingIcons(packed) || cfg.UsesBuildingIcons(set_up))) {
        verdict = "a trebuchet's icon is taken from the buildings' sheet";
    } else if (ok() && (!cfg.UsesBuildingIcons(barracks) || !cfg.UsesBuildingIcons(cfg.GetUnitIdByName("GTAA2")))) {
        verdict = "a building's icon is not taken from the buildings' sheet";
    }

    // Placed as a scenario places it -- from a file or the editor -- each is
    // the unit it is: drawn as one, packing, the set-up one armed.
    for (const int placed : {packed, set_up}) {
        if (!ok()) break;
        const Vector2D at{home.x, home.y + Fixed32(4)};
        const entt::entity e = ECS::ScenarioWorld::Place(reg, grid, placed, 1, at, Fixed32(0), 0);
        sim.Update(16);
        const auto* sprite = e != entt::null ? reg.try_get<ECS::Components::Sprite>(e) : nullptr;
        if (e == entt::null || reg.all_of<ECS::Components::Building>(e)) {
            verdict = "a trebuchet placed in a scenario is made a building";
        } else if (sprite == nullptr || sprite->slp_id < 0 || !reg.all_of<ECS::Components::Animation>(e)) {
            verdict = "a trebuchet placed in a scenario is not drawn as a unit";
        } else if (!ECS::Packing::Packs(reg, e)) {
            verdict = "a trebuchet placed in a scenario does not pack";
        } else if (placed == set_up && !reg.all_of<ECS::Components::Combat>(e)) {
            verdict = "a trebuchet placed set up in a scenario is not armed";
        }
        if (e != entt::null) reg.destroy(e);
    }

    // Set up: still, for the dat-less packing time, then a trebuchet.
    if (ok()) {
        auto& h = reg.get<ECS::Components::Health>(treb);
        h.current_hp = h.max_hp / 2;
        ECS::Orders::Execute(reg, grid, Core::PackCommand{{treb}, false, 1});
        ECS::Orders::Execute(reg, grid, Core::MoveCommand{treb, home.x, home.y + Fixed32(6), 1});
        int done_at = -1;
        for (int t = 0; t < 10 * kTicksPerSecond && ok(); ++t) {
            sim.Update(16);
            if (UnitIdOf(reg, treb) == set_up && done_at < 0) done_at = t;
            if (done_at < 0 && Apart(reg.get<ECS::Components::Transform>(treb).position, home) >
                                   Fixed32(1) / Fixed32(10)) {
                verdict = "a trebuchet setting up moved";
            }
        }
        const int want = ECS::Packing::kPackMs / 16;
        if (ok() && done_at < 0) verdict = "a packed trebuchet never set up";
        else if (ok() && (done_at < want - 2 || done_at > want + 2)) verdict = "setting up did not take the packing time";
        else if (ok() && !reg.all_of<ECS::Components::Combat>(treb)) verdict = "a trebuchet set up is not armed";
        else if (ok()) {
            const auto& hp = reg.get<ECS::Components::Health>(treb);
            if (hp.current_hp * 2 != hp.max_hp) verdict = "setting up did not keep the wounds in proportion";
        }
    }
    // Set up, it cannot move.
    if (ok()) {
        ECS::Orders::Execute(reg, grid, Core::MoveCommand{treb, home.x, home.y + Fixed32(6), 1});
        for (int t = 0; t < 3 * kTicksPerSecond; ++t) sim.Update(16);
        if (Apart(reg.get<ECS::Components::Transform>(treb).position, home) > Fixed32(1) / Fixed32(10)) {
            verdict = "a trebuchet set up moved";
        }
    }
    // A stop part way through packing leaves it set up.
    if (ok()) {
        ECS::Orders::Execute(reg, grid, Core::PackCommand{{treb}, true, 1});
        for (int t = 0; t < kTicksPerSecond; ++t) sim.Update(16);
        ECS::Orders::Execute(reg, grid, Core::StopCommand{treb, 1});
        for (int t = 0; t < 6 * kTicksPerSecond; ++t) sim.Update(16);
        if (UnitIdOf(reg, treb) != set_up || !reg.all_of<ECS::Components::Combat>(treb)) {
            verdict = "a trebuchet stopped while packing did not stay set up";
        }
    }
    // Packed, it moves again.
    if (ok()) {
        ECS::Orders::Execute(reg, grid, Core::PackCommand{{treb}, true, 1});
        for (int t = 0; t < 5 * kTicksPerSecond; ++t) sim.Update(16);
        if (UnitIdOf(reg, treb) != packed) verdict = "a trebuchet did not pack";
        else if (reg.all_of<ECS::Components::Combat>(treb)) verdict = "a packed trebuchet is armed";
        if (ok()) {
            ECS::Orders::Execute(reg, grid, Core::MoveCommand{treb, home.x, home.y + Fixed32(2), 1});
            for (int t = 0; t < 6 * kTicksPerSecond; ++t) sim.Update(16);
            if (Apart(reg.get<ECS::Components::Transform>(treb).position, home) < Fixed32(1)) {
                verdict = "a packed trebuchet did not move";
            }
        }
    }
    // Sent at an enemy's Barracks out of range: it closes in, sets up within
    // range and shoots, and its stone bursts where it lands in the stone's
    // dying graphic, which carries the sound of the blow.
    if (ok()) {
        const entt::entity target =
            ECS::Buildings::Place(reg, grid, barracks, 2, ax + 14, ay, true);
        const auto* rules = ECS::Players::UnitOf(reg, 1, set_up);
        const Fixed32 range = rules != nullptr ? rules->max_range : Fixed32(0);
        if (ECS::Combat::Gap(reg, treb, target) <= range) verdict = "the Barracks is not out of range";
        const int hp0 = reg.get<ECS::Components::Health>(target).current_hp;
        // Its hit points, nothing once it is gone.
        auto hp_of = [&](entt::entity e) {
            const auto* hp = reg.valid(e) ? reg.try_get<ECS::Components::Health>(e) : nullptr;
            return hp != nullptr ? hp->current_hp : 0;
        };
        ECS::Orders::Execute(reg, grid, Core::AttackCommand{treb, target, 1});
        bool loosed = false;
        bool burst = false;
        const auto* stone = rules != nullptr ? ECS::Players::UnitOf(reg, 1, rules->projectile) : nullptr;
        const int burst_graphic = stone != nullptr ? stone->dying_graphic : -1;
        Fixed32 gap_set_up{};
        for (int t = 0; t < 90 * kTicksPerSecond && ok(); ++t) {
            const int before = UnitIdOf(reg, treb);
            sim.Update(16);
            if (before == packed && ECS::Packing::Busy(reg, treb) && gap_set_up.value == 0) {
                gap_set_up = ECS::Combat::Gap(reg, treb, target);
            }
            auto missiles = reg.view<ECS::Components::Projectile>();
            for (auto m : missiles) {
                if (missiles.get<ECS::Components::Projectile>(m).shooter == treb) loosed = true;
            }
            auto playing = reg.view<ECS::Components::Animation, ECS::Components::Dying>();
            for (auto p : playing) {
                if (playing.get<ECS::Components::Animation>(p).graphic[0] == burst_graphic) burst = true;
            }
            if ((burst && hp_of(target) < hp0) || hp_of(target) <= 0) break;
        }
        if (ok() && gap_set_up.value == 0) verdict = "a packed trebuchet sent at a building never set up";
        else if (ok() && gap_set_up > range) verdict = "a trebuchet set up out of range";
        else if (ok() && !loosed) verdict = "a trebuchet sent at a building never shot";
        else if (ok() && hp_of(target) >= hp0) {
            verdict = "a trebuchet's shot did no harm";
        } else if (ok() && (burst_graphic < 0 || !burst)) {
            verdict = "a trebuchet's stone did not burst where it landed";
        }
    }

    std::printf("packing=%s\n", verdict);
    return ok();
}

} // namespace Verify
