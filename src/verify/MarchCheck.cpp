#include "verify/Harness.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <utility>
#include <vector>

#include "core/Command.h"
#include "core/CommandQueue.h"
#include "core/ConfigManager.h"
#include "core/GameSetup.h"
#include "ecs/Navigation.h"
#include "ecs/Simulation.h"
#include "ecs/Units.h"
#include "ecs/components/Combat.h"
#include "ecs/components/Health.h"
#include "ecs/components/Transform.h"
#include "pathfinding/Pathfinder.h"

namespace Verify {

namespace {

constexpr int kTicksPerSecond = 1000 / 16;
constexpr int kGroup = 40;     // eight abreast, five deep, a tile apart
constexpr int kAttackers = 20;

// A group as it stands: how wide across its line of march (the standard
// deviation of where its units stand across it, in tiles), and how many pairs
// stand over each other, nearer than one body's width together.
struct Spread {
    double width = 0;
    int stacked = 0;
};

Spread Measure(const entt::registry& reg, const std::vector<entt::entity>& units, double dx, double dy) {
    Spread s;
    const double len = std::sqrt(dx * dx + dy * dy);
    const double ax = -dy / len, ay = dx / len;
    std::vector<std::pair<double, double>> at;
    for (auto e : units) {
        if (!reg.valid(e) || !reg.all_of<ECS::Components::Transform>(e)) continue;
        const auto& p = reg.get<ECS::Components::Transform>(e).position;
        at.emplace_back(p.x.ToFloat(), p.y.ToFloat());
    }
    if (at.size() < 2) return s;
    double mean = 0;
    for (const auto& a : at) mean += a.first * ax + a.second * ay;
    mean /= static_cast<double>(at.size());
    double var = 0;
    for (const auto& a : at) {
        const double o = a.first * ax + a.second * ay - mean;
        var += o * o;
    }
    s.width = std::sqrt(var / static_cast<double>(at.size()));
    const double body = ECS::Navigation::RadiusOf(reg, units.front()).ToFloat();
    for (size_t i = 0; i < at.size(); ++i) {
        for (size_t j = i + 1; j < at.size(); ++j) {
            if (std::hypot(at[i].first - at[j].first, at[i].second - at[j].second) < body) ++s.stacked;
        }
    }
    return s;
}

std::vector<entt::entity> Block(entt::registry& reg, int unit_id, int x0, int y0) {
    std::vector<entt::entity> units;
    for (int i = 0; i < kGroup; ++i) {
        units.push_back(ECS::Units::Spawn(reg, unit_id, 1, {Core::Fixed32(x0 + i % 8), Core::Fixed32(y0 + i / 8)}));
    }
    return units;
}

// A group marched to (tx, ty) for `seconds`: its narrowest and the most pairs
// stacked at any second.
void March(ECS::Simulation& sim, const std::vector<entt::entity>& units, int x0, int y0, int tx, int ty,
           int seconds, double& narrowest, int& stacked) {
    Core::CommandQueue::Get().PushCommand(Core::FormationMoveCommand{units, Core::Fixed32(tx), Core::Fixed32(ty),
                                                                       Core::Formation::Line, 1});
    narrowest = 1e9;
    stacked = 0;
    for (int s = 0; s < seconds; ++s) {
        for (int t = 0; t < kTicksPerSecond; ++t) sim.Update(16);
        const Spread m = Measure(sim.GetRegistry(), units, tx - x0, ty - y0);
        narrowest = std::min(narrowest, m.width);
        stacked = std::max(stacked, m.stacked);
    }
}

} // namespace

// Soldiers keep their room. Forty spearmen sent across open ground keep their
// width, none standing over another; sent past the Town Center, the houses
// and the forest's edge, they squeeze through but do not stack; and twenty of
// them sent at one enemy stand round it rather than on one another.
bool CheckMarch() {
    const char* verdict = "ok";
    const int spear = Core::ConfigManager::Get().GetUnitIdByName("SPRMN");
    const Core::GameSetup setup = Core::DefaultSetup();

    // Open ground west of the town on the default map.
    ECS::Simulation open;
    open.Init(setup);
    auto& reg = open.GetRegistry();
    const int x0 = 66, y0 = 63, tx = x0 + 24, ty = y0 + 2;
    const auto group = Block(reg, spear, x0, y0);
    double open_narrowest = 0;
    int open_stacked = 0;
    March(open, group, x0, y0, tx, ty, 30, open_narrowest, open_stacked);

    // Twenty of them at one enemy beside them that does not fight back.
    const std::vector<entt::entity> attackers(group.begin(), group.begin() + kAttackers);
    const auto& lead = reg.get<ECS::Components::Transform>(attackers.front()).position;
    const int region = ECS::Navigation::RegionOf(reg, open.GetGrid(), attackers.front());
    int fx = lead.x.ToInt(), fy = lead.y.ToInt();
    for (int r = 4; r < 12 && fx == lead.x.ToInt(); ++r) {
        for (int d = -r; d <= r && fx == lead.x.ToInt(); ++d) {
            if (Pathfinding::RegionAt(open.GetGrid(), lead.x.ToInt() + d, lead.y.ToInt() - r, Physics::kLand) == region) {
                fx = lead.x.ToInt() + d;
                fy = lead.y.ToInt() - r;
            }
        }
    }
    const entt::entity foe = ECS::Units::Spawn(reg, spear, 2, {Core::Fixed32(fx), Core::Fixed32(fy)});
    reg.get<ECS::Components::Combat>(foe).stance = ECS::Components::Stance::NoAttack;
    reg.get<ECS::Components::Health>(foe).max_hp = reg.get<ECS::Components::Health>(foe).current_hp = 100000;
    for (auto e : attackers) Core::CommandQueue::Get().PushCommand(Core::AttackCommand{e, foe, 1});
    for (int t = 0; t < 20 * kTicksPerSecond; ++t) open.Update(16);
    const int round_stacked = Measure(reg, attackers, 1, 0).stacked;

    // Through the town.
    ECS::Simulation town;
    town.Init(setup);
    const auto& home = town.player_starts[0];
    const int ax = home.first - 6, ay = home.second - 2;
    const auto crowd = Block(town.GetRegistry(), spear, ax, ay);
    double town_narrowest = 0;
    int town_stacked = 0;
    March(town, crowd, ax, ay, home.first + 22, home.second + 8, 40, town_narrowest, town_stacked);

    if (open_stacked > 1) verdict = "soldiers marching over open ground stood over one another";
    else if (open_narrowest < 1.0) verdict = "soldiers marching over open ground closed into a file";
    else if (town_stacked > 2) verdict = "soldiers marching through the town stood over one another";
    else if (round_stacked > 20) verdict = "soldiers at one enemy stood over one another";
    std::printf("march=%s open_width=%.2f open_stacked=%d town_width=%.2f town_stacked=%d round_stacked=%d\n", verdict,
                open_narrowest, open_stacked, town_narrowest, town_stacked, round_stacked);
    return std::strcmp(verdict, "ok") == 0;
}

} // namespace Verify
