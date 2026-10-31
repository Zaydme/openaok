#include "verify/AIGame.h"

#include <algorithm>
#include <cstdio>

#include "ai/Brain.h"
#include "ai/Census.h"
#include "ecs/Events.h"
#include "ecs/components/Building.h"
#include "ecs/components/Owner.h"
#include "ecs/components/Worker.h"
#include "verify/Harness.h"

namespace Verify::AIGame {

bool Minute(ECS::Simulation& sim, ECS::Simulation* twin, int minute, Progress& progress) {
    auto& reg = sim.GetRegistry();
    for (int t = 0; t < kTicksPerMinute; ++t) {
        sim.Update(16);
        if (twin != nullptr) twin->Update(16);
        const ECS::Events::Log* log = ECS::Events::Read(reg);
        if (log == nullptr) continue;
        for (const auto& e : log->events) {
            if (e.tick != log->tick) continue;
            // Soldiers trained: born, and not villagers.
            if (e.kind == ECS::Events::Kind::Born && e.player >= 1 && e.player < Core::kMaxPlayers &&
                reg.valid(e.subject) && !reg.all_of<ECS::Components::Worker>(e.subject)) {
                ++progress.trained[e.player];
            }
            if (e.kind != ECS::Events::Kind::Struck) continue;
            // An army at the enemy's town: one player's unit striking the
            // other's building. Scouts brushing past each other in the field
            // do not count -- that happens unordered.
            if (e.player != Core::PLAYER_GAIA && reg.valid(e.other) && reg.valid(e.subject) &&
                reg.all_of<ECS::Components::Building>(e.subject)) {
                const auto* owner = reg.try_get<ECS::Components::Owner>(e.other);
                if (owner != nullptr && owner->player_id != Core::PLAYER_GAIA && owner->player_id != e.player) {
                    progress.struck_enemy = true;
                }
            }
        }
    }
    for (Core::PlayerId p = 1; p < Core::kMaxPlayers; ++p) {
        const AI::Census c = AI::Take(reg, p);
        progress.peak_villagers[p] = std::max(progress.peak_villagers[p], static_cast<int>(c.villagers.size()));
        if (c.age >= 1 && progress.feudal_minute[p] < 0) progress.feudal_minute[p] = minute;
    }
    return twin == nullptr || Fingerprint(reg) == Fingerprint(twin->GetRegistry());
}

void Trace(const entt::registry& reg, int minute, Core::PlayerId p) {
    const AI::Census c = AI::Take(reg, p);
    std::printf("AI min %2d p%d age %d vil %2d (idle %d, f%d w%d g%d s%d b%d) army %d (warships %zu) "
                "fishing %zu transports %zu pop %d/%d stock f%d w%d g%d s%d bld %zu\n",
                minute, p, c.age, static_cast<int>(c.villagers.size()),
                static_cast<int>(c.idle_villagers.size()), c.gatherers[0], c.gatherers[1],
                c.gatherers[2], c.gatherers[3], c.builders,
                static_cast<int>(c.military.size()), c.boats.size(), c.fishing_ships.size(),
                c.transports.size(), c.population, c.capacity,
                c.stock[0], c.stock[1], c.stock[2], c.stock[3], c.buildings.size());
    const AI::Brains* brains = reg.ctx().find<AI::Brains>();
    if (brains == nullptr) return;
    for (const AI::Brain& b : brains->brains) {
        if (b.player != p) continue;
        const auto& g = b.mind.goals;
        const auto sn = [&](AI::Lang::SN s) { return b.mind.sn[static_cast<size_t>(s)]; };
        size_t attacking = 0;
        for (const AI::Group& g : b.attack_groups) attacking += g.units.size();
        std::printf("    goals need%d rush%d age-adv%d conflict%d parity%d attack%d | share f%d w%d g%d s%d "
                    "builders %d%% escrow f%d w%d g%d s%d (%d%%) attack group %zu\n",
                    g[5], g[6], g[14], g[16], g[3], g[4],
                    sn(AI::Lang::SN::FoodGathererPercentage), sn(AI::Lang::SN::WoodGathererPercentage),
                    sn(AI::Lang::SN::GoldGathererPercentage), sn(AI::Lang::SN::StoneGathererPercentage),
                    sn(AI::Lang::SN::PercentCivilianBuilders), b.mind.escrow[0], b.mind.escrow[1],
                    b.mind.escrow[2], b.mind.escrow[3], b.mind.escrow_percent[0], attacking);
        std::printf("    buildings:");
        for (const auto& site : c.buildings) std::printf(" %d%s", site.unit_id, site.complete ? "" : "*");
        std::printf("\n");
    }
}

} // namespace Verify::AIGame
