#include "verify/Harness.h"

#include <cstdio>

#include "core/ConfigManager.h"
#include "ecs/Simulation.h"
#include "ecs/Units.h"
#include "ecs/components/Sprite.h"

namespace Verify {

namespace {

constexpr int kTicksPerSecond = 1000 / 16;

} // namespace

// Graphics play as the dat's sequence says: a scout standing tosses its head
// and rests three seconds before it does again (the graphic's replay delay),
// and a packed trebuchet standing, in what is also its walking graphic, keeps
// its wheels still.
bool CheckSequence() {
    ECS::Simulation sim;
    sim.Init();
    auto& reg = sim.GetRegistry();
    auto& cfg = Core::ConfigManager::Get();
    sim.Update(16);
    const char* verdict = nullptr;
    const ECS::Components::Vector2D at{Core::Fixed32(40), Core::Fixed32(40)};
    const entt::entity scout = ECS::Units::Spawn(reg, cfg.GetUnitIdByName("SCOUT"), 1, at);
    const entt::entity packed = ECS::Units::Spawn(reg, cfg.GetUnitIdByName("PTREB"), 1,
                                                  {at.x + Core::Fixed32(4), at.y});
    // The longest stretch the scout rests on its first frame, and whether the
    // trebuchet's frame ever moves.
    int rest = 0, longest_rest = 0, plays = 0;
    int last_frame = 0;
    bool wheels_turned = false;
    for (int t = 0; t < 15 * kTicksPerSecond; ++t) {
        sim.Update(16);
        const int frame = reg.get<ECS::Components::Sprite>(scout).current_frame;
        if (frame == 0) {
            ++rest;
            if (rest > longest_rest) longest_rest = rest;
        } else {
            if (last_frame == 0) ++plays;
            rest = 0;
        }
        last_frame = frame;
        if (reg.get<ECS::Components::Sprite>(packed).current_frame != 0) wheels_turned = true;
    }
    if (plays == 0) {
        verdict = "a standing scout never moved";
    } else if (longest_rest < 5 * kTicksPerSecond / 2) {
        verdict = "a standing scout did not rest between plays";
    } else if (wheels_turned) {
        verdict = "a packed trebuchet standing turned its wheels";
    }
    std::printf("sequence=%s\n", verdict == nullptr ? "ok" : verdict);
    return verdict == nullptr;
}

} // namespace Verify
