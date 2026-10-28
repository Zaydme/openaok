#include "verify/Harness.h"
#include "core/FixedMath.h"

#include <cstdio>
#include <string>

#include "core/ConfigManager.h"
#include "core/Rules.h"
#include "ecs/ScenarioWorld.h"
#include "ecs/Simulation.h"
#include "ecs/Vision.h"
#include "ecs/components/Sprite.h"
#include "physics/Grid.h"

namespace Verify {

namespace {

constexpr Core::PlayerId kUs = Core::PLAYER_LOCAL;

// Open ground `half` tiles either side of a tile no player has explored,
// searched from the map's corners inward.
bool Unexplored(const ECS::Simulation& sim, int half, int& out_x, int& out_y) {
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

// The frame the renderer draws a file sprite with: the first half and one
// more of its angles stored, the rest their mirrors.
int DrawnFrame(const ECS::Components::Sprite& sprite) {
    const int n = std::max(1, sprite.angles);
    int angle = Core::FixedMath::Facet(sprite.heading, n);
    if (n > 1 && angle > n / 2) angle = n - angle;
    return angle * sprite.frames_per_angle + sprite.current_frame;
}

} // namespace

bool CheckFlag() {
    ECS::Simulation sim;
    sim.Init();
    auto& reg = sim.GetRegistry();
    auto& grid = sim.GetGrid();
    auto& cfg = Core::ConfigManager::Get();
    std::string verdict = "ok";
    sim.Update(16);

    // A scenario's flag of ours, turned as the editor left it, on ground no
    // one has seen: it sees round itself, and is drawn with a frame its
    // graphic has.
    int fx = 0, fy = 0;
    const int flag_id = cfg.GetUnitIdByName("FLAGA");
    const auto* rules = Core::Rules::Unit(flag_id);
    if (rules == nullptr || !Unexplored(sim, 2, fx, fy)) {
        verdict = "no flag in the data or no unexplored ground";
    } else {
        const entt::entity flag = ECS::ScenarioWorld::Place(
            reg, grid, flag_id, kUs, {Core::Fixed32(fx), Core::Fixed32(fy)}, Core::Fixed32(3), 0);
        sim.Update(16);
        const auto art = cfg.GetGraphic(rules->standing_graphic);
        const auto* sprite = flag != entt::null ? reg.try_get<ECS::Components::Sprite>(flag) : nullptr;
        if (sprite == nullptr) verdict = "a flag could not be placed";
        else if (!ECS::Vision::Visible(reg, kUs, fx, fy)) verdict = "a flag of ours does not show its ground";
        else if (!ECS::Vision::Shows(reg, kUs, flag)) verdict = "a flag of ours is not shown";
        else if (DrawnFrame(*sprite) >= art.frame_count * std::max(1, art.angle_count)) {
            verdict = "a flag is drawn with a frame its graphic does not have";
        }
    }

    std::printf("flag=%s\n", verdict.c_str());
    return verdict == "ok";
}

} // namespace Verify
