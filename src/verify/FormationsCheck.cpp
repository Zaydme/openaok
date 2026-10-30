#include "verify/Harness.h"

#include <cstdio>
#include <vector>

#include "core/Command.h"
#include "core/ConfigManager.h"
#include "ecs/Mortality.h"
#include "ecs/Orders.h"
#include "ecs/ScenarioWorld.h"
#include "ecs/Simulation.h"
#include "ecs/components/Formation.h"
#include "ecs/components/Path.h"
#include "ecs/components/Selectable.h"
#include "ecs/components/Transform.h"
#include "rendering/UIRenderer.h"

namespace Verify {

namespace {

using Kind = Rendering::UIRenderer::CommandKind;
constexpr Core::PlayerId kUs = 1;
constexpr int kTicksPerSecond = 1000 / 16;
// The formation's buttons: Disband Formation's icon and its slot, the last.
constexpr int kDisbandIcon = 39;
constexpr int kDisbandSlot = 15;
// The lit Box Formation's icon.
constexpr int kBoxLit = 66;

bool Clearing(const Physics::Grid& grid, int& x, int& y, int half) {
    for (int radius = 0; radius < 60; ++radius) {
        for (int dy = -radius; dy <= radius; ++dy) {
            for (int dx = -radius; dx <= radius; ++dx) {
                bool clear = true;
                for (int oy = -half; oy <= half && clear; ++oy) {
                    for (int ox = -half; ox <= half && clear; ++ox) {
                        if (!grid.IsWalkable(x + dx + ox, y + dy + oy)) clear = false;
                    }
                }
                if (clear) {
                    x += dx;
                    y += dy;
                    return true;
                }
            }
        }
    }
    return false;
}

ECS::Components::Vector2D At(int x, int y) {
    return {Core::Fixed32(x) + Core::Fixed32(1) / Core::Fixed32(2),
            Core::Fixed32(y) + Core::Fixed32(1) / Core::Fixed32(2)};
}

const ECS::Components::InFormation* In(const entt::registry& reg, entt::entity e) {
    return reg.try_get<ECS::Components::InFormation>(e);
}

void Select(entt::registry& reg, const std::vector<entt::entity>& picked) {
    auto all = reg.view<ECS::Components::Selectable>();
    for (auto e : all) {
        all.get<ECS::Components::Selectable>(e).is_selected =
            std::find(picked.begin(), picked.end(), e) != picked.end();
    }
}

// The panel's button of `kind` for the selection, if offered.
const Rendering::UIRenderer::Command* Offered(ECS::Simulation& sim, Kind kind,
                                              Rendering::UIRenderer::Command* cmds, int n) {
    for (int i = 0; i < n; ++i) {
        if (cmds[i].kind == kind && (kind != Kind::Formation || cmds[i].choice == static_cast<int>(Core::Formation::Box))) {
            return &cmds[i];
        }
    }
    (void)sim;
    return nullptr;
}

} // namespace

// Locked formations: three spearmen given Box keep it -- one of them sent
// takes all three, at the slowest one's pace, to their places in a box; the
// panel lights Box and offers Disband Formation in its last slot; two of them
// given Line leave the third in no formation; Disband breaks theirs up; and a
// member killed leaves a formation of two as none.
bool CheckFormations() {
    const char* verdict = nullptr;
    ECS::Simulation sim;
    sim.Init(Core::DefaultSetup());
    auto& reg = sim.GetRegistry();
    auto& grid = sim.GetGrid();
    auto& cfg = Core::ConfigManager::Get();
    sim.Update(16);
    int x = grid.GetWidth() / 2;
    int y = grid.GetHeight() / 2;
    if (!Clearing(grid, x, y, 8)) verdict = "no field";
    std::vector<entt::entity> spears;
    const int spearman = cfg.GetUnitIdByName("SPRMN");
    for (int i = 0; i < 3 && verdict == nullptr; ++i) {
        spears.push_back(ECS::ScenarioWorld::Place(reg, grid, spearman, kUs, At(x - 3 + i, y), {}, 0));
        if (spears.back() == entt::null) verdict = "no spearman placed";
    }
    auto run = [&](int seconds) {
        for (int t = 0; t < seconds * kTicksPerSecond; ++t) sim.Update(16);
    };
    auto order = [&](const Core::Command& c) { ECS::Orders::Execute(reg, grid, c); };

    if (verdict == nullptr) {
        order(Core::FormCommand{spears, Core::Formation::Box, kUs});
        const auto* a = In(reg, spears[0]);
        const auto* b = In(reg, spears[1]);
        const auto* c = In(reg, spears[2]);
        if (a == nullptr || b == nullptr || c == nullptr) verdict = "the spearmen were not locked in a formation";
        else if (a->group != b->group || b->group != c->group) verdict = "the spearmen were locked in different formations";
        else if (a->formation != Core::Formation::Box) verdict = "the spearmen were not locked in Box";
    }
    // The panel: Box lit, Disband Formation last.
    if (verdict == nullptr) {
        run(2);
        Select(reg, spears);
        Rendering::UIRenderer::Command cmds[Rendering::UIRenderer::kCommandSlots];
        const int n = Rendering::UIRenderer::BuildCommands(sim, 1024, 768, cmds, Rendering::UIRenderer::kCommandSlots);
        const auto* box = Offered(sim, Kind::Formation, cmds, n);
        const auto* disband = Offered(sim, Kind::Disband, cmds, n);
        if (box == nullptr || box->icon_frame != kBoxLit) verdict = "the panel did not light the formation the spearmen are in";
        else if (disband == nullptr || disband->slot != kDisbandSlot || disband->icon_frame != kDisbandIcon) {
            verdict = "the panel did not offer Disband Formation in its last slot";
        }
    }
    // One sent takes the formation along.
    if (verdict == nullptr) {
        order(Core::MoveCommand{spears[1], Core::Fixed32(x + 6), Core::Fixed32(y + 6), kUs});
        for (entt::entity e : spears) {
            const auto* path = reg.try_get<ECS::Components::Path>(e);
            if (path == nullptr || !path->has_goal || path->waypoints.empty()) {
                verdict = "a spearman of the formation stayed behind when one of them was sent";
                break;
            }
        }
        if (verdict == nullptr) {
            const auto& goal0 = reg.get<ECS::Components::Path>(spears[0]).waypoints.back();
            const auto& goal2 = reg.get<ECS::Components::Path>(spears[2]).waypoints.back();
            if (goal0.x == goal2.x && goal0.y == goal2.y) verdict = "the formation's members were sent to one spot";
            else if (reg.get<ECS::Components::Path>(spears[0]).speed_cap.value <= 0) {
                verdict = "the formation did not keep to its slowest member's pace";
            }
        }
    }
    // Two given another formation leave the third in none.
    if (verdict == nullptr) {
        run(6);
        order(Core::FormCommand{{spears[0], spears[1]}, Core::Formation::Line, kUs});
        const auto* a = In(reg, spears[0]);
        if (a == nullptr || a->formation != Core::Formation::Line || In(reg, spears[1]) == nullptr) {
            verdict = "two spearmen given Line were not locked in it";
        } else if (In(reg, spears[2]) != nullptr) {
            verdict = "the spearman left on his own was kept in a formation";
        }
    }
    if (verdict == nullptr) {
        order(Core::DisbandCommand{{spears[1]}, kUs});
        if (In(reg, spears[0]) != nullptr || In(reg, spears[1]) != nullptr) verdict = "Disband left a spearman in his formation";
    }
    // A member killed.
    if (verdict == nullptr) {
        order(Core::FormCommand{{spears[0], spears[2]}, Core::Formation::Flank, kUs});
        ECS::Mortality::Kill(reg, grid, spears[2]);
        if (In(reg, spears[0]) != nullptr) verdict = "the last of a formation was kept in it after the other died";
    }
    std::printf("formations=%s\n", verdict == nullptr ? "ok" : verdict);
    return verdict == nullptr;
}

} // namespace Verify
