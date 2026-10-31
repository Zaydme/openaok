#include "verify/Harness.h"

#include <cstdio>

#include "core/Costs.h"
#include "ecs/Simulation.h"
#include "verify/Stress.h"

namespace Verify {

namespace {

constexpr int kTicksPerSecond = 1000 / 16;
constexpr int kSeconds = 60;

} // namespace

bool CheckStress() {
    ECS::Simulation sim;
    sim.Init(Stress::Setup());
    const Stress::Forces forces = Stress::Populate(sim);
    const bool was_on = Core::Costs::Enabled();
    Core::Costs::Enable(true);
    Core::Costs::Reset();
    for (int t = 0; t < kSeconds * kTicksPerSecond; ++t) sim.Update(16);
    const Core::Costs::Totals totals = Core::Costs::Read();
    Core::Costs::Enable(was_on);
    Core::Costs::Reset();

    int64_t sum = 0;
    for (int64_t us : totals.us) sum += us;
    const int64_t ticks = totals.ticks > 0 ? totals.ticks : 1;
    std::printf("stress=ok army=%d skirmish=%d fleet=%d entities=%zu tick_us=%lld worst_us=%lld routes=%lld expansions=%lld",
                forces.army, forces.skirmish, forces.fleet,
                static_cast<size_t>(sim.GetRegistry().storage<entt::entity>().size()), static_cast<long long>(sum / ticks),
                static_cast<long long>(totals.worst_tick), static_cast<long long>(totals.routes),
                static_cast<long long>(totals.expansions));
    for (size_t i = 0; i < Core::Costs::kParts; ++i) {
        std::printf(" %s=%lld/%lld", Core::Costs::Name(static_cast<Core::Costs::Part>(i)),
                    static_cast<long long>(totals.us[i] / ticks), static_cast<long long>(totals.worst[i]));
    }
    std::printf("\n");
    return true;
}

} // namespace Verify
