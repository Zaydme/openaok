#include "verify/Harness.h"

#include <cstdio>

#include "core/ConfigManager.h"
#include "ecs/Mortality.h"
#include "ecs/Simulation.h"
#include "ecs/Units.h"
#include "ecs/components/Mortal.h"
#include "ecs/components/Owner.h"
#include "rendering/UIRenderer.h"

namespace Verify {

namespace {

namespace C = ECS::Components;
constexpr int kTicksPerSecond = 1000 / 16;

} // namespace

// A soldier struck down leaves the minimap as it falls: its dot shows while it
// lives, and neither it while it falls nor the body it leaves has one.
bool CheckMinimapDead() {
    ECS::Simulation sim;
    sim.Init();
    auto& reg = sim.GetRegistry();
    auto& grid = sim.GetGrid();
    const char* verdict = nullptr;
    const int spear = Core::ConfigManager::Get().GetUnitIdByName("SPRMN");
    const entt::entity soldier = ECS::Units::Spawn(reg, spear, 1, {Core::Fixed32(20), Core::Fixed32(20)});
    bool dying = false, corpse = false;
    if (!Rendering::UIRenderer::OnMinimap(reg, soldier)) {
        verdict = "a living soldier had no dot on the minimap";
    } else {
        ECS::Mortality::Kill(reg, grid, soldier);
        for (int t = 0; t < 30 * kTicksPerSecond && verdict == nullptr; ++t) {
            if (reg.valid(soldier)) {
                if (reg.all_of<C::Dying>(soldier)) dying = true;
                if (Rendering::UIRenderer::OnMinimap(reg, soldier)) verdict = "a falling soldier kept its dot on the minimap";
            }
            // The body it leaves, its owner's still.
            for (auto body : reg.view<C::Corpse, C::Owner>()) {
                corpse = true;
                if (Rendering::UIRenderer::OnMinimap(reg, body)) verdict = "a soldier's body had a dot on the minimap";
            }
            sim.Update(16);
        }
        if (verdict == nullptr && (!dying || !corpse)) verdict = "the soldier never fell and lay dead";
    }
    std::printf("minimapdead=%s dying=%d corpse=%d\n", verdict == nullptr ? "ok" : verdict, dying ? 1 : 0, corpse ? 1 : 0);
    return verdict == nullptr;
}

} // namespace Verify
