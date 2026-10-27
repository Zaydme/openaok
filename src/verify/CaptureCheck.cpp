#include "verify/Harness.h"

#include <cstdio>
#include <string>

#include "core/ConfigManager.h"
#include "ecs/Events.h"
#include "ecs/ScenarioWorld.h"
#include "ecs/Simulation.h"
#include "ecs/Vision.h"
#include "ecs/components/Owner.h"
#include "ecs/components/Transform.h"
#include "ecs/components/Worker.h"
#include "physics/Grid.h"

namespace Verify {

namespace {

constexpr Core::PlayerId kUs = Core::PLAYER_LOCAL;

// Open ground `half` tiles either side of a tile that no player has seen,
// searched from the map's corners inward.
bool Unseen(const ECS::Simulation& sim, int half, int& out_x, int& out_y) {
    const auto& reg = sim.GetRegistry();
    const auto& grid = sim.GetGrid();
    const int w = grid.GetWidth();
    const int h = grid.GetHeight();
    for (int inset = 8; inset < w / 2; inset += 2) {
        for (const auto& [cx, cy] : {std::pair{inset, inset}, std::pair{w - 1 - inset, inset},
                                     std::pair{inset, h - 1 - inset}, std::pair{w - 1 - inset, h - 1 - inset}}) {
            bool ok = true;
            for (int dy = -half; dy <= half && ok; ++dy) {
                for (int dx = -half; dx <= half && ok; ++dx) {
                    ok = grid.IsWalkable(cx + dx, cy + dy);
                    for (Core::PlayerId p = 1; p < Core::kMaxPlayers && ok; ++p) {
                        ok = !ECS::Vision::Explored(reg, p, cx + dx, cy + dy);
                    }
                }
            }
            if (ok) {
                out_x = cx;
                out_y = cy;
                return true;
            }
        }
    }
    return false;
}

Core::PlayerId OwnerOf(const entt::registry& reg, entt::entity e) {
    return reg.valid(e) ? reg.get<ECS::Components::Owner>(e).player_id : Core::PLAYER_GAIA;
}

ECS::Components::Vector2D At(int x, int y) { return {Core::Fixed32(x), Core::Fixed32(y)}; }

} // namespace

bool CheckCapture() {
    ECS::Simulation sim;
    sim.Init();
    auto& reg = sim.GetRegistry();
    auto& grid = sim.GetGrid();
    auto& cfg = Core::ConfigManager::Get();
    std::string verdict = "ok";
    sim.Update(16);

    // A village of Gaia's out of everyone's sight: its villager and its
    // house stay Gaia's until one of ours sees them, then are ours; a wolf
    // and a Relic Cart beside them stay Gaia's.
    {
        int vx = 0, vy = 0;
        entt::entity scout = entt::null;
        auto workers = reg.view<ECS::Components::Worker, ECS::Components::Owner>();
        for (auto e : workers) {
            if (workers.get<ECS::Components::Owner>(e).player_id == kUs) scout = e;
        }
        if (scout == entt::null || !Unseen(sim, 6, vx, vy)) {
            verdict = "no unit of ours or no unseen ground for a village";
        } else {
            const entt::entity villager = ECS::ScenarioWorld::Place(
                reg, grid, cfg.GetUnitIdByName("VMBAS"), Core::PLAYER_GAIA, At(vx, vy), {}, 0);
            const entt::entity house = ECS::ScenarioWorld::Place(
                reg, grid, cfg.GetUnitIdByName("HOUS"), Core::PLAYER_GAIA, At(vx + 2, vy), {}, 0);
            const entt::entity wolf = ECS::ScenarioWorld::Place(
                reg, grid, cfg.GetUnitIdByName("WOLFX"), Core::PLAYER_GAIA, At(vx - 3, vy), {}, 0);
            const entt::entity cart = ECS::ScenarioWorld::Place(
                reg, grid, cfg.GetUnitIdByName("ARTCT"), Core::PLAYER_GAIA, At(vx, vy + 2), {}, 0);
            for (int t = 0; t < 30; ++t) sim.Update(16);
            if (villager == entt::null || house == entt::null || wolf == entt::null || cart == entt::null) {
                verdict = "the village could not be placed";
            } else if (OwnerOf(reg, villager) != Core::PLAYER_GAIA || OwnerOf(reg, house) != Core::PLAYER_GAIA) {
                verdict = "Gaia's village was taken unseen";
            } else {
                auto& at = reg.get<ECS::Components::Transform>(scout);
                at.position = At(vx - 1, vy - 1);
                at.previous_position = at.position;
                bool told = false;
                for (int t = 0; t < 10; ++t) {
                    sim.Update(16);
                    const auto* log = ECS::Events::Read(reg);
                    for (const auto& e : log->events) {
                        told |= e.tick == log->tick && e.kind == ECS::Events::Kind::Converted &&
                                e.subject == villager && e.player == kUs && e.id == Core::PLAYER_GAIA;
                    }
                }
                if (OwnerOf(reg, villager) != kUs) verdict = "Gaia's villager seen was not taken";
                else if (OwnerOf(reg, house) != kUs) verdict = "Gaia's house seen was not taken";
                else if (!told) verdict = "Gaia's villager taken was not told";
                else if (reg.valid(wolf) && OwnerOf(reg, wolf) != Core::PLAYER_GAIA) verdict = "a wolf seen was taken";
                else if (OwnerOf(reg, cart) != Core::PLAYER_GAIA) verdict = "a Relic Cart seen was taken";
            }
        }
    }

    std::printf("capture=%s\n", verdict.c_str());
    return verdict == "ok";
}

} // namespace Verify
