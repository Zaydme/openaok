#include "verify/Harness.h"

#include <cstdio>
#include <cstring>

#include "core/ConfigManager.h"
#include "core/FixedMath.h"
#include "ecs/Animals.h"
#include "ecs/Combat.h"
#include "ecs/Simulation.h"
#include "ecs/Units.h"
#include "ecs/components/Combat.h"
#include "ecs/components/Grazer.h"
#include "ecs/components/Health.h"
#include "ecs/components/Identity.h"
#include "ecs/components/Owner.h"
#include "ecs/components/Transform.h"

namespace Verify {

namespace {

constexpr Core::PlayerId kUs = 1;
constexpr Core::PlayerId kThem = 2;
constexpr int kTicksPerSecond = 1000 / 16;

// The first open tile from (x, y) outward with a clear square round it.
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

// A point inside the tile, off its centre -- within half a tile of it, since
// tiles are centred on whole numbers.
ECS::Components::Vector2D At(int x, int y) {
    return {Core::Fixed32(x) + Core::Fixed32(1) / Core::Fixed32(4),
            Core::Fixed32(y) - Core::Fixed32(1) / Core::Fixed32(4)};
}

Core::Fixed32 Apart(const entt::registry& reg, entt::entity a, entt::entity b) {
    const auto& p = reg.get<ECS::Components::Transform>(a).position;
    const auto& q = reg.get<ECS::Components::Transform>(b).position;
    return Core::FixedMath::Distance(p.x - q.x, p.y - q.y);
}

} // namespace

// Animals by their task lists: a Gaia sheep joins the first player to come
// near and changes hands when another comes while its owner is away; struck
// prey runs; the wild graze about where they were put without straying.
bool CheckAnimals() {
    ECS::Simulation headless;
    headless.Init();
    auto& reg = headless.GetRegistry();
    auto& grid = headless.GetGrid();
    auto& cfg = Core::ConfigManager::Get();
    const char* verdict = "ok";
    auto ok = [&]() { return std::strcmp(verdict, "ok") == 0; };
    auto run = [&](int ticks) {
        for (int t = 0; t < ticks; ++t) headless.Update(16);
    };

    const int sheep_id = cfg.GetUnitIdByName("SHEEPG");
    const int deer_id = cfg.GetUnitIdByName("DEERX");
    const int militia_id = cfg.GetUnitIdByName("SPRMN");
    int x = grid.GetWidth() / 2;
    int y = grid.GetHeight() / 2;
    if (!Clearing(grid, x, y, 6)) {
        std::printf("animals=no open ground\n");
        return false;
    }

    // A Gaia sheep, and a soldier of ours walking up to it.
    const entt::entity sheep = ECS::Units::Spawn(reg, sheep_id, Core::PLAYER_GAIA, At(x, y));
    if (!ECS::Animals::Herdable(reg, sheep)) verdict = "a sheep is not herdable by its tasks";
    run(kTicksPerSecond);
    if (ok() && reg.get<ECS::Components::Owner>(sheep).player_id != Core::PLAYER_GAIA) {
        verdict = "a sheep with nobody near changed hands";
    }
    const entt::entity ours = ok() ? ECS::Units::Spawn(reg, militia_id, kUs, At(x + 1, y))
                                   : entt::null;
    if (ok()) {
        reg.get<ECS::Components::Combat>(ours).stance = ECS::Components::Stance::NoAttack;
        run(kTicksPerSecond);
        if (reg.get<ECS::Components::Owner>(sheep).player_id != kUs) {
            verdict = "a gaia sheep did not join the player who came near";
        }
    }
    // Theirs comes while ours is there -- nearer to it than ours, even: it
    // stays ours. Ours leaves: it goes.
    if (ok()) {
        reg.get<ECS::Components::Transform>(ours).position =
            {At(x, y).x + Core::Fixed32(3) / Core::Fixed32(2), At(x, y).y};
        const entt::entity theirs = ECS::Units::Spawn(
            reg, militia_id, kThem, {At(x, y).x, At(x, y).y + Core::Fixed32(1) / Core::Fixed32(2)});
        reg.get<ECS::Components::Combat>(theirs).stance = ECS::Components::Stance::NoAttack;
        run(kTicksPerSecond);
        if (reg.get<ECS::Components::Owner>(sheep).player_id != kUs) {
            verdict = "a sheep left its owner while the owner's unit stood by it";
        } else {
            reg.get<ECS::Components::Transform>(ours).position = At(x + 30, y);
            run(kTicksPerSecond);
            if (reg.get<ECS::Components::Owner>(sheep).player_id != kThem) {
                verdict = "a sheep its owner left did not go to the player beside it";
            }
        }
    }

    // A deer struck runs from whoever struck it.
    if (ok()) {
        int dx = x + 12;
        int dy = y;
        Clearing(grid, dx, dy, 6);
        const entt::entity deer = ECS::Units::Spawn(reg, deer_id, Core::PLAYER_GAIA, At(dx, dy));
        const entt::entity hunter = ECS::Units::Spawn(reg, militia_id, kUs, At(dx - 1, dy));
        reg.get<ECS::Components::Combat>(hunter).stance = ECS::Components::Stance::NoAttack;
        if (!ECS::Animals::Prey(reg, deer)) {
            verdict = "a deer is not prey by its tasks";
        } else {
            const Core::Fixed32 before = Apart(reg, deer, hunter);
            ECS::Combat::Hurt(reg, grid, deer, Core::Fixed32(1), hunter);
            run(4 * kTicksPerSecond);
            if (!ECS::Combat::Alive(reg, deer) || Apart(reg, deer, hunter) < before + Core::Fixed32(2)) {
                verdict = "a struck deer did not run from its attacker";
            }
        }
    }

    // The wild graze: over several minutes they move, and stay within a couple
    // of tiles of where they were put -- a random walk that long would carry
    // one five or six tiles off.
    if (ok()) {
        int gx = x - 12;
        int gy = y;
        Clearing(grid, gx, gy, 6);
        const ECS::Components::Vector2D home = At(gx, gy);
        const entt::entity grazer = ECS::Units::Spawn(reg, deer_id, Core::PLAYER_GAIA, home);
        Core::Fixed32 furthest(0);
        bool moved = false;
        for (int t = 0; t < 400 * kTicksPerSecond; ++t) {
            headless.Update(16);
            const auto& p = reg.get<ECS::Components::Transform>(grazer).position;
            const Core::Fixed32 d = Core::FixedMath::Distance(p.x - home.x, p.y - home.y);
            if (d.value > 0) moved = true;
            if (d > furthest) furthest = d;
        }
        if (!moved) verdict = "a wild deer never grazed";
        else if (furthest > Core::Fixed32(3)) verdict = "a grazing deer wandered off";
    }

    std::printf("animals=%s\n", verdict);
    return ok();
}

} // namespace Verify
