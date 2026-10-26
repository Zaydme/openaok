#include "verify/Harness.h"

#include <cstdio>
#include <cstring>
#include <vector>

#include "core/Command.h"
#include "core/CommandQueue.h"
#include "core/ConfigManager.h"
#include "core/FixedMath.h"
#include "ecs/Combat.h"
#include "ecs/Simulation.h"
#include "ecs/Units.h"
#include "ecs/components/Combat.h"
#include "ecs/components/Duty.h"
#include "ecs/components/Health.h"
#include "ecs/components/Transform.h"

namespace Verify {

namespace {

constexpr Core::PlayerId kUs = 1;
constexpr Core::PlayerId kThem = 2;
constexpr int kTicksPerSecond = 1000 / 16;

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

Core::Fixed32 Distance(const ECS::Components::Vector2D& a, const ECS::Components::Vector2D& b) {
    return Core::FixedMath::Distance(a.x - b.x, a.y - b.y);
}

const ECS::Components::Vector2D& Where(const entt::registry& reg, entt::entity e) {
    return reg.get<ECS::Components::Transform>(e).position;
}

} // namespace

// Standing orders: a patrol walking its beat both ways and fighting what it
// meets, then walking on; a follower keeping by a moving unit; a guard turning
// on whoever attacks its charge; and any other order ending a duty.
bool CheckDuties() {
    ECS::Simulation headless;
    headless.Init();
    auto& reg = headless.GetRegistry();
    auto& grid = headless.GetGrid();
    auto& cfg = Core::ConfigManager::Get();
    auto& queue = Core::CommandQueue::Get();
    const char* verdict = "ok";
    auto ok = [&]() { return std::strcmp(verdict, "ok") == 0; };
    auto run = [&](int ticks) {
        for (int t = 0; t < ticks; ++t) headless.Update(16);
    };
    const int militia_id = cfg.GetUnitIdByName("SPRMN");
    const int scout_id = cfg.GetUnitIdByName("SCOUT");

    int x = grid.GetWidth() / 2;
    int y = grid.GetHeight() / 2;
    if (!Clearing(grid, x, y, 9)) {
        std::printf("duties=no open ground\n");
        return false;
    }

    // A patrol: from where it stands to eight tiles off, and back, again and
    // again.
    const ECS::Components::Vector2D a = At(x - 4, y);
    const ECS::Components::Vector2D b = At(x + 4, y);
    const entt::entity walker = ECS::Units::Spawn(reg, militia_id, kUs, a);
    queue.PushCommand(Core::PatrolCommand{{walker}, b.x, b.y, kUs});
    int ends = 0;
    bool near_b = false;
    for (int t = 0; t < 60 * kTicksPerSecond && ends < 4; ++t) {
        headless.Update(16);
        const auto& p = Where(reg, walker);
        if (!near_b && Distance(p, b) < Core::Fixed32(1)) { near_b = true; ++ends; }
        if (near_b && Distance(p, a) < Core::Fixed32(1)) { near_b = false; ++ends; }
    }
    if (ends < 4) verdict = "a patrol did not walk its beat there and back twice";

    // An enemy on the beat: the patrol takes it on and, once it is dead,
    // walks on.
    if (ok()) {
        const entt::entity foe = ECS::Units::Spawn(reg, militia_id, kThem, At(x, y));
        auto& foe_combat = reg.get<ECS::Components::Combat>(foe);
        foe_combat.stance = ECS::Components::Stance::NoAttack;
        reg.get<ECS::Components::Health>(foe).current_hp = 5;
        bool killed = false;
        int t = 0;
        for (; t < 60 * kTicksPerSecond; ++t) {
            headless.Update(16);
            if (!ECS::Combat::Alive(reg, foe)) { killed = true; break; }
        }
        if (!killed) {
            verdict = "a patrol passed an enemy on its beat";
        } else {
            bool reached = false;
            for (int u = 0; u < 30 * kTicksPerSecond && !reached; ++u) {
                headless.Update(16);
                const auto& p = Where(reg, walker);
                reached = Distance(p, a) < Core::Fixed32(1) || Distance(p, b) < Core::Fixed32(1);
            }
            if (!reached || !reg.all_of<ECS::Components::Duty>(walker)) {
                verdict = "a patrol did not walk on after its fight";
            }
        }
    }

    // A new order ends it.
    if (ok()) {
        queue.PushCommand(Core::StopCommand{walker, kUs});
        run(2);
        if (reg.all_of<ECS::Components::Duty>(walker)) verdict = "stop did not end a patrol";
    }

    // A follower keeps by a unit that rides off.
    entt::entity leader = entt::null;
    if (ok()) {
        int fx = x;
        int fy = y + 10;
        Clearing(grid, fx, fy, 7);
        leader = ECS::Units::Spawn(reg, scout_id, kUs, At(fx - 5, fy));
        reg.get<ECS::Components::Combat>(leader).stance = ECS::Components::Stance::NoAttack;
        const entt::entity follower = ECS::Units::Spawn(reg, militia_id, kUs, At(fx - 6, fy + 1));
        queue.PushCommand(Core::FollowCommand{{follower}, leader, kUs});
        queue.PushCommand(Core::MoveCommand{leader, At(fx + 5, fy).x, At(fx + 5, fy).y, kUs});
        run(20 * kTicksPerSecond);
        if (Distance(Where(reg, leader), At(fx + 5, fy)) > Core::Fixed32(1)) {
            verdict = "the unit being followed did not get where it was sent";
        } else if (Distance(Where(reg, follower), Where(reg, leader)) > Core::Fixed32(3)) {
            verdict = "a follower did not keep by the unit it followed";
        }
    }

    // A guard turns on whoever attacks its charge.
    if (ok()) {
        const auto& at = Where(reg, leader);
        const entt::entity guard = ECS::Units::Spawn(
            reg, militia_id, kUs, {at.x - Core::Fixed32(1), at.y});
        reg.get<ECS::Components::Combat>(guard).stance = ECS::Components::Stance::StandGround;
        queue.PushCommand(Core::GuardCommand{{guard}, leader, kUs});
        run(kTicksPerSecond);
        const entt::entity raider = ECS::Units::Spawn(
            reg, militia_id, kThem, {at.x + Core::Fixed32(3), at.y + Core::Fixed32(1)});
        ECS::Combat::Engage(reg, raider, leader, true);
        bool answered = false;
        for (int t = 0; t < 10 * kTicksPerSecond && !answered; ++t) {
            headless.Update(16);
            const auto& combat = reg.get<ECS::Components::Combat>(guard);
            answered = combat.target == raider;
        }
        if (!answered) verdict = "a guard let its charge be attacked";
    }

    std::printf("duties=%s\n", verdict);
    return ok();
}

} // namespace Verify
