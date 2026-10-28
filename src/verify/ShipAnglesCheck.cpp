#include "verify/Harness.h"

#include <cstdio>

#include "core/ConfigManager.h"
#include "core/FixedMath.h"
#include "ecs/Combat.h"
#include "ecs/Simulation.h"
#include "ecs/Units.h"
#include "ecs/components/Sprite.h"
#include "ecs/components/SpritePieces.h"

namespace Verify {

namespace {

constexpr int kTicksPerSecond = 1000 / 16;

// A world vector a sixteenth of a turn at a time round from South (the
// world's (1, 1)), the way the facings go: cos and sin of k * 22.5 degrees,
// in thousandths, turned an eighth onto the world's axes.
ECS::Components::Vector2D Toward(int k) {
    static const int kCos[16] = {1000, 924, 707, 383, 0, -383, -707, -924,
                                 -1000, -924, -707, -383, 0, 383, 707, 924};
    const Core::Fixed32 c = Core::Fixed32(kCos[k % 16]) / Core::Fixed32(1000);
    const Core::Fixed32 s = Core::Fixed32(kCos[(k + 12) % 16]) / Core::Fixed32(1000);
    // u along (1, 1), v along (-1, 1): x = u - v, y = u + v, scaled by 4.
    return {(c - s) * Core::Fixed32(4), (c + s) * Core::Fixed32(4)};
}

} // namespace

// A ship's graphic is drawn for sixteen angles and shows the nearest of them:
// a war galley turned toward each sixteenth of a turn shows that one, the
// eight between the eight ways among them, while a swordsman, drawn for
// eight, shows the nearest of its eight.
bool CheckShipAngles() {
    ECS::Simulation sim;
    sim.Init();
    auto& reg = sim.GetRegistry();
    auto& cfg = Core::ConfigManager::Get();
    sim.Update(16);
    const char* verdict = nullptr;
    const ECS::Components::Vector2D at{Core::Fixed32(40), Core::Fixed32(40)};
    const entt::entity galley = ECS::Units::Spawn(reg, cfg.GetUnitIdByName("GALLY"), 1, at);
    const entt::entity swordsman = ECS::Units::Spawn(reg, cfg.GetUnitIdByName("SPRMN"), 1, at);
    for (int t = 0; t < kTicksPerSecond; ++t) sim.Update(16);
    const auto* pieces = reg.try_get<ECS::Components::SpritePieces>(galley);
    if (pieces == nullptr || pieces->count == 0 || pieces->piece[0].angles != 16) {
        verdict = "a war galley's graphic is not drawn for sixteen angles";
    }
    for (int k = 0; k < 16 && verdict == nullptr; ++k) {
        const ECS::Components::Vector2D way = Toward(k);
        const entt::entity mark = ECS::Units::Spawn(reg, cfg.GetUnitIdByName("SPRMN"), 2,
                                                    {at.x + way.x, at.y + way.y});
        ECS::Combat::Face(reg, galley, mark);
        ECS::Combat::Face(reg, swordsman, mark);
        const int ship = Core::FixedMath::Facet(reg.get<ECS::Components::Sprite>(galley).heading, 16);
        const int man = Core::FixedMath::Facet(reg.get<ECS::Components::Sprite>(swordsman).heading,
                                               reg.get<ECS::Components::Sprite>(swordsman).angles);
        if (ship != k) {
            verdict = "a war galley does not show the sixteenth of a turn it faces";
        } else if (reg.get<ECS::Components::Sprite>(swordsman).angles != 8) {
            verdict = "a swordsman's graphic is not drawn for eight angles";
        } else if (k % 2 == 0 && man != k / 2) {
            verdict = "a swordsman does not show the eighth of a turn it faces";
        }
        reg.destroy(mark);
    }
    std::printf("shipangles=%s\n", verdict == nullptr ? "ok" : verdict);
    return verdict == nullptr;
}

} // namespace Verify
