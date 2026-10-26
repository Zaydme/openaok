#include "verify/Harness.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "core/Command.h"
#include "core/ConfigManager.h"
#include "core/GameSetup.h"
#include "core/Rules.h"
#include "ecs/Buildings.h"
#include "ecs/Orders.h"
#include "ecs/Players.h"
#include "ecs/Simulation.h"
#include "ecs/Units.h"
#include "ecs/components/Animation.h"
#include "ecs/components/Building.h"
#include "ecs/components/Combat.h"
#include "ecs/components/Owner.h"
#include "ecs/components/Health.h"
#include "ecs/components/Transform.h"
#include "ecs/components/Worker.h"
#include "ecs/components/Path.h"

namespace Verify {

namespace {

using Core::Fixed32;
constexpr int kTicksPerSecond = 1000 / 16;

// The open ground nearest the map's middle, `half` tiles each way.
bool OpenGround(ECS::Simulation& sim, int half, int& out_x, int& out_y) {
    auto& grid = sim.GetGrid();
    const int cx = grid.GetWidth() / 2, cy = grid.GetHeight() / 2;
    for (int r = 0; r < grid.GetWidth() / 2; ++r) {
        for (int dy = -r; dy <= r; ++dy) {
            for (int dx = -r; dx <= r; ++dx) {
                if (std::abs(dx) != r && std::abs(dy) != r) continue;
                bool open = true;
                for (int oy = -half; oy <= half && open; ++oy) {
                    for (int ox = -half; ox <= half && open; ++ox) {
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

Fixed32 Res(const entt::registry& reg, Core::PlayerId p, int dat_resource) {
    const auto* player = ECS::Players::Find(reg, p);
    return player != nullptr ? ECS::Players::Resource(*player, dat_resource) : Fixed32(0);
}

int Hp(const entt::registry& reg, entt::entity e) {
    return reg.get<ECS::Components::Health>(e).current_hp;
}

// Runs until `target` is mended to full or `limit` seconds pass, timing the
// mending from the first hit point gained. Seconds, or -1 if never full.
double TimeToMend(ECS::Simulation& sim, entt::entity target, int limit, bool& job_shown,
                  const std::vector<entt::entity>& crew) {
    auto& reg = sim.GetRegistry();
    const int start = Hp(reg, target);
    int first = -1;
    for (int t = 0; t < limit * kTicksPerSecond; ++t) {
        sim.Update(16);
        if (first < 0 && Hp(reg, target) > start) first = t;
        for (entt::entity v : crew) {
            const auto* anim = reg.try_get<ECS::Components::Animation>(v);
            const auto* w = reg.try_get<ECS::Components::Worker>(v);
            if (anim != nullptr && w != nullptr && w->working &&
                anim->job == ECS::Components::VillagerJob::Repair) {
                job_shown = true;
            }
        }
        const auto& h = reg.get<ECS::Components::Health>(target);
        if (h.current_hp >= h.max_hp) {
            return first < 0 ? 0.0 : static_cast<double>(t - first + 1) / kTicksPerSecond;
        }
    }
    return -1.0;
}

bool Near(double value, double want, double slack) {
    return value >= want - slack && value <= want + slack;
}

} // namespace

// Repairs: a damaged Barracks mended by one villager at the repairer's 12.5
// hit points a second, wearing the repairer's graphics, and charged half the
// Barracks' wood over its hit points for each one; a crew of three at twice
// that (each after the first adds half); a trebuchet, which the dat makes a
// building-type object, at a quarter of the pace and charged its wood and
// gold; the repair stopping when the owner runs out of wood; and no repair of
// an enemy's building, nor of a ram, which the repairer's tasks do not name.
bool CheckRepair() {
    const char* verdict = "ok";
    auto ok = [&]() { return std::strcmp(verdict, "ok") == 0; };
    auto& cfg = Core::ConfigManager::Get();
    const int barracks = cfg.GetUnitIdByName("BRKS");
    const int villager = cfg.GetUnitIdByName("VMBAS");
    const int trebuchet = cfg.GetUnitIdByName("PTREB");
    const int ram = cfg.GetUnitIdByName("BTRAM");
    const auto* brks = Core::Rules::Unit(barracks);
    const auto* treb = Core::Rules::Unit(trebuchet);

    Core::GameSetup setup = Core::DefaultSetup();
    ECS::Simulation sim;
    sim.Init(setup);
    auto& reg = sim.GetRegistry();
    auto& grid = sim.GetGrid();
    // No wolves: nothing here may be hurt but by the scenario.
    {
        std::vector<entt::entity> wild;
        auto armed = reg.view<ECS::Components::Combat, ECS::Components::Owner>();
        for (auto e : armed) {
            if (armed.get<ECS::Components::Owner>(e).player_id == Core::PLAYER_GAIA) wild.push_back(e);
        }
        for (auto e : wild) reg.destroy(e);
    }
    int ax = 0, ay = 0;
    if (!OpenGround(sim, 12, ax, ay)) verdict = "no open ground";
    auto* me = ECS::Players::Find(reg, 1);
    if (ok() && me != nullptr) {
        ECS::Players::AddStock(*me, Core::ResourceType::Wood, 2000);
        ECS::Players::AddStock(*me, Core::ResourceType::Gold, 2000);
    }
    const ECS::Components::Vector2D near{Fixed32(ax), Fixed32(ay + 4)};

    // One villager, half a Barracks.
    if (ok()) {
        const entt::entity b = ECS::Buildings::Place(reg, grid, barracks, 1, ax - 5, ay - 5, true);
        auto& h = reg.get<ECS::Components::Health>(b);
        h.current_hp = h.max_hp / 2;
        const int lost = h.max_hp - h.current_hp;
        const entt::entity v = ECS::Units::Spawn(reg, villager, 1, near);
        const Fixed32 wood = Res(reg, 1, Core::Rules::Res::Wood);
        ECS::Orders::Execute(reg, grid, Core::RepairCommand{{v}, b, 1});
        bool job = false;
        const double took = TimeToMend(sim, b, 200, job, {v});
        const double want = lost / 12.5;
        const double paid = (wood - Res(reg, 1, Core::Rules::Res::Wood)).ToFloat();
        const double owed = lost * brks->costs[0].amount / (2.0 * brks->hp);
        if (took < 0) verdict = "a villager did not mend a damaged Barracks";
        else if (!Near(took, want, 1.0)) verdict = "a villager did not mend at the repairer's rate";
        else if (!job) verdict = "a villager repairing did not become the repairer";
        else if (!Near(paid, owed, 1.0)) verdict = "a repair was not charged half the cost a hit point";
        else if (reg.get<ECS::Components::Worker>(v).state != Core::UnitState::Idle) {
            verdict = "a repairer did not stop when the Barracks was whole";
        }
    }

    // Three villagers: the first at the full rate, each after it at half.
    if (ok()) {
        const entt::entity b = ECS::Buildings::Place(reg, grid, barracks, 1, ax + 5, ay - 5, true);
        auto& h = reg.get<ECS::Components::Health>(b);
        h.current_hp = h.max_hp / 2;
        const int lost = h.max_hp - h.current_hp;
        std::vector<entt::entity> crew;
        for (int i = 0; i < 3; ++i) {
            crew.push_back(ECS::Units::Spawn(reg, villager, 1,
                                             {Fixed32(ax + i), Fixed32(ay + 4)}));
        }
        ECS::Orders::Execute(reg, grid, Core::RepairCommand{crew, b, 1});
        bool job = false;
        const double took = TimeToMend(sim, b, 200, job, crew);
        if (took < 0) verdict = "a crew did not mend a damaged Barracks";
        else if (!Near(took, lost / (12.5 * 2.0), 1.5)) verdict = "a crew did not mend at the crew rate";
    }

    // A trebuchet: a quarter of the pace, charged its wood and gold.
    if (ok()) {
        const entt::entity t = ECS::Units::Spawn(reg, trebuchet, 1, {Fixed32(ax), Fixed32(ay + 8)});
        auto& h = reg.get<ECS::Components::Health>(t);
        h.current_hp = h.max_hp / 2;
        const int lost = h.max_hp - h.current_hp;
        const entt::entity v = ECS::Units::Spawn(reg, villager, 1, near);
        const Fixed32 gold = Res(reg, 1, Core::Rules::Res::Gold);
        ECS::Orders::Execute(reg, grid, Core::RepairCommand{{v}, t, 1});
        bool job = false;
        const double took = TimeToMend(sim, t, 200, job, {v});
        const double paid = (gold - Res(reg, 1, Core::Rules::Res::Gold)).ToFloat();
        const double owed = lost * treb->costs[1].amount / (2.0 * treb->hp);
        if (took < 0) verdict = "a villager did not mend a trebuchet";
        else if (!Near(took, lost / (12.5 / 4.0), 1.5)) verdict = "a trebuchet was not mended at a quarter pace";
        else if (!Near(paid, owed, 1.0)) verdict = "a trebuchet's repair was not charged its gold";
    }

    // The owner out of wood: the repair stops short.
    if (ok()) {
        const entt::entity b = ECS::Buildings::Place(reg, grid, barracks, 1, ax - 5, ay + 10, true);
        const int max_hp = reg.get<ECS::Components::Health>(b).max_hp;
        reg.get<ECS::Components::Health>(b).current_hp = max_hp / 2;
        const entt::entity v = ECS::Units::Spawn(reg, villager, 1, near);
        ECS::Players::SetResource(*ECS::Players::Find(reg, 1), Core::Rules::Res::Wood, Fixed32(5));
        ECS::Orders::Execute(reg, grid, Core::RepairCommand{{v}, b, 1});
        bool job = false;
        TimeToMend(sim, b, 60, job, {v});
        const int mended = Hp(reg, b) - max_hp / 2;
        const int affordable = static_cast<int>(5.0 * 2.0 * brks->hp / brks->costs[0].amount);
        if (Hp(reg, b) >= max_hp) verdict = "a repair went on with no wood to pay for it";
        else if (mended < affordable - 1 || mended > affordable + 1) verdict = "a repair stopped before the wood ran out";
        else if (reg.get<ECS::Components::Worker>(v).state != Core::UnitState::Idle) {
            verdict = "a repairer out of wood did not stop";
        }
        ECS::Players::AddStock(*ECS::Players::Find(reg, 1), Core::ResourceType::Wood, 2000);
    }

    // Refusals: an enemy's Barracks, and a ram.
    if (ok()) {
        const entt::entity theirs = ECS::Buildings::Place(reg, grid, barracks, 2, ax + 5, ay + 10, true);
        reg.get<ECS::Components::Health>(theirs).current_hp = 100;
        const entt::entity battering = ECS::Units::Spawn(reg, ram, 1, {Fixed32(ax + 3), Fixed32(ay + 8)});
        reg.get<ECS::Components::Health>(battering).current_hp = 50;
        const entt::entity v = ECS::Units::Spawn(reg, villager, 1, near);
        ECS::Orders::Execute(reg, grid, Core::RepairCommand{{v}, theirs, 1});
        if (reg.get<ECS::Components::Worker>(v).state == Core::UnitState::Repairing) {
            verdict = "a villager was set to repair an enemy's building";
        }
        ECS::Orders::Execute(reg, grid, Core::RepairCommand{{v}, battering, 1});
        if (ok() && reg.get<ECS::Components::Worker>(v).state == Core::UnitState::Repairing) {
            verdict = "a villager was set to repair a ram, which the dat's tasks do not name";
        }
    }

    std::printf("repair=%s\n", verdict);
    return ok();
}

} // namespace Verify
