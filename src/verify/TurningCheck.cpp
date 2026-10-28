#include "verify/Harness.h"

#include <cstdio>
#include <cstdlib>

#include "core/Command.h"
#include "core/ConfigManager.h"
#include "core/FixedMath.h"
#include "ecs/Orders.h"
#include "ecs/Simulation.h"
#include "ecs/Units.h"
#include "ecs/components/Combat.h"
#include "ecs/components/Projectile.h"
#include "ecs/components/Sprite.h"

namespace Verify {

namespace {

constexpr int kTicksPerSecond = 1000 / 16;

struct Turned {
    int first_shot_ms = -1;   // when its first missile was loosed
    int most_angles = 0;      // the most angles it turned in one tick
    int start_facet = 0;
    int shot_facet = 0;       // the angle it faced when it shot
    int target_facet = 0;
};

// `name`, facing South-East, sent at an enemy standing straight behind it:
// how it turned and when it first shot.
Turned TurnAndShoot(const char* name) {
    ECS::Simulation sim;
    sim.Init();
    auto& reg = sim.GetRegistry();
    auto& grid = sim.GetGrid();
    auto& cfg = Core::ConfigManager::Get();
    sim.Update(16);
    Turned out;
    const ECS::Components::Vector2D at{Core::Fixed32(40), Core::Fixed32(40)};
    const entt::entity shooter = ECS::Units::Spawn(reg, cfg.GetUnitIdByName(name), 1, at);
    // Behind it: South-East is the world's +x, so behind is -x.
    const entt::entity target =
        ECS::Units::Spawn(reg, cfg.GetUnitIdByName("SPRMN"), 2, {at.x - Core::Fixed32(5), at.y});
    reg.get<ECS::Components::Combat>(target).stance = ECS::Components::Stance::NoAttack;
    auto& sprite = reg.get<ECS::Components::Sprite>(shooter);
    const int n = sprite.angles;
    out.start_facet = Core::FixedMath::Facet(sprite.heading, n);
    out.target_facet = Core::FixedMath::Facet(Core::FixedMath::WorldVectorToHeading(Core::Fixed32(-1), Core::Fixed32(0)), n);
    ECS::Orders::Execute(reg, grid, Core::AttackCommand{shooter, target, 1});
    int facet = out.start_facet;
    for (int t = 0; t < 10 * kTicksPerSecond && out.first_shot_ms < 0; ++t) {
        sim.Update(16);
        const int now = Core::FixedMath::Facet(reg.get<ECS::Components::Sprite>(shooter).heading, n);
        int turned = std::abs(now - facet);
        if (turned > n / 2) turned = n - turned;
        if (turned > out.most_angles) out.most_angles = turned;
        facet = now;
        for (auto m : reg.view<ECS::Components::Projectile>()) {
            if (reg.get<ECS::Components::Projectile>(m).shooter == shooter && out.first_shot_ms < 0) {
                out.first_shot_ms = (t + 1) * 16;
                out.shot_facet = now;
            }
        }
    }
    return out;
}

} // namespace

// Turning before acting, at the dat's rotation speed: a war galley sent at an
// enemy straight behind it turns a sixteenth of a turn each tenth of a second
// and shoots only once it faces it -- eight angles round, at least seven
// tenths of a second -- a trebuchet an eighth each half second, two seconds
// for half a turn; an archer turns at once.
bool CheckTurning() {
    const char* verdict = nullptr;
    const Turned galley = TurnAndShoot("GALLY");
    const Turned treb = TurnAndShoot("TREBU");
    const Turned archer = TurnAndShoot("ARCHR");
    if (galley.first_shot_ms < 0 || treb.first_shot_ms < 0 || archer.first_shot_ms < 0) {
        verdict = "a ship, a trebuchet or an archer never shot at an enemy behind it";
    } else if (galley.most_angles > 1 || treb.most_angles > 1) {
        verdict = "a ship or a trebuchet turned more than an angle at a time";
    } else if (galley.first_shot_ms < 700) {
        verdict = "a war galley shot before it could have turned half round";
    } else if (treb.first_shot_ms < 1500) {
        verdict = "a trebuchet shot before it could have turned half round";
    } else if (galley.shot_facet != galley.target_facet || treb.shot_facet != treb.target_facet) {
        verdict = "a ship or a trebuchet shot before it faced its target";
    } else if (archer.most_angles < 2) {
        verdict = "an archer did not turn at once";
    }
    std::printf("turning=%s\n", verdict == nullptr ? "ok" : verdict);
    std::printf("turning-figures galley=%dms treb=%dms archer=%dms\n", galley.first_shot_ms,
                treb.first_shot_ms, archer.first_shot_ms);
    return verdict == nullptr;
}

} // namespace Verify
