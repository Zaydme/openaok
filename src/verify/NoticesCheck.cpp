#include "verify/Harness.h"

#include <cstdio>
#include <cstdlib>
#include <functional>
#include <string>

#include "core/Command.h"
#include "core/CommandQueue.h"
#include "core/ConfigManager.h"
#include "ecs/Animals.h"
#include "ecs/Buildings.h"
#include "ecs/Events.h"
#include "ecs/Simulation.h"
#include "ecs/Units.h"
#include "ecs/components/Owner.h"
#include "ecs/components/Resource.h"
#include "ecs/components/Transform.h"
#include "ecs/components/Worker.h"
#include "physics/Grid.h"

namespace Verify {

namespace {

constexpr int kTicksPerSecond = 1000 / 16;

// Runs the world up to `seconds`, until the log holds an event of `kind`
// that `match` takes.
bool Hears(ECS::Simulation& sim, ECS::Events::Kind kind, int seconds,
           const std::function<bool(const ECS::Events::Event&)>& match) {
    for (int t = 0; t < seconds * kTicksPerSecond; ++t) {
        sim.Update(16);
        const auto* log = ECS::Events::Read(sim.GetRegistry());
        if (log == nullptr) return false;
        for (const auto& e : log->events) {
            if (e.tick == log->tick && e.kind == kind && match(e)) return true;
        }
    }
    return false;
}

// Half the side of the open square a Wonder's site is put in: its five tiles
// and room round them for the builder.
constexpr int kWonderClearing = 5;

// The open square of ground nearest (x, y), `half` tiles either side of its
// middle, searched outward ring by ring.
bool OpenGround(const Physics::Grid& grid, int x, int y, int half, int& out_x, int& out_y) {
    for (int ring = 8; ring < 40; ++ring) {
        for (int dy = -ring; dy <= ring; ++dy) {
            for (int dx = -ring; dx <= ring; ++dx) {
                if (std::abs(dx) != ring && std::abs(dy) != ring) continue;
                bool clear = true;
                for (int oy = -half; oy <= half && clear; ++oy) {
                    for (int ox = -half; ox <= half && clear; ++ox) {
                        clear = grid.IsWalkable(x + dx + ox, y + dy + oy);
                    }
                }
                if (clear) {
                    out_x = x + dx;
                    out_y = y + dy;
                    return true;
                }
            }
        }
    }
    return false;
}

void Place(entt::registry& reg, entt::entity unit, const ECS::Components::Vector2D& at) {
    auto& transform = reg.get<ECS::Components::Transform>(unit);
    transform.position = at;
    transform.previous_position = at;
}

} // namespace

bool CheckNotices() {
    ECS::Simulation sim;
    sim.Init();
    auto& reg = sim.GetRegistry();
    auto& grid = sim.GetGrid();
    auto& cfg = Core::ConfigManager::Get();
    auto& queue = Core::CommandQueue::Get();
    const Core::PlayerId us = Core::PLAYER_LOCAL;
    std::string verdict = "ok";

    entt::entity villager = entt::null;
    auto workers = reg.view<ECS::Components::Worker, ECS::Components::Owner>();
    for (auto e : workers) {
        if (workers.get<ECS::Components::Owner>(e).player_id == us) {
            villager = e;
            break;
        }
    }
    const auto home = villager != entt::null
        ? reg.get<ECS::Components::Transform>(villager).position : ECS::Components::Vector2D{};

    // A sheep of Gaia's, found: the villager brought beside it claims it.
    entt::entity sheep = entt::null;
    auto animals = reg.view<ECS::Components::Owner, ECS::Components::Transform>();
    for (auto e : animals) {
        if (animals.get<ECS::Components::Owner>(e).player_id == Core::PLAYER_GAIA &&
            ECS::Animals::Herdable(reg, e)) {
            sheep = e;
            break;
        }
    }
    if (villager == entt::null || sheep == entt::null) {
        verdict = "no villager or no sheep of Gaia's";
    } else {
        const auto at = reg.get<ECS::Components::Transform>(sheep).position;
        Place(reg, villager, {at.x + Core::Fixed32(1), at.y});
        const bool claimed = Hears(sim, ECS::Events::Kind::Claimed, 3, [&](const auto& e) {
            return e.subject == sheep && e.player == us && e.id == Core::PLAYER_GAIA;
        });
        if (!claimed) verdict = "a sheep claimed was not told";
        Place(reg, villager, home);
    }

    // A farm worked to its last food.
    if (verdict == "ok") {
        const int farm_id = cfg.GetUnitIdByName("FARM");
        const int fx = home.x.ToInt() + 6;
        const int fy = home.y.ToInt() - 6;
        const entt::entity farm = ECS::Buildings::Place(reg, grid, farm_id, us, fx, fy, true);
        if (farm == entt::null) {
            verdict = "a farm could not be placed";
        } else {
            reg.get<ECS::Components::Resource>(farm).current_amount = 1;
            queue.PushCommand(Core::GatherCommand{villager, farm, us});
            const bool exhausted = Hears(sim, ECS::Events::Kind::Exhausted, 60, [&](const auto& e) {
                return e.subject == farm && e.player == us && e.id == farm_id;
            });
            if (!exhausted) verdict = "a farm run out was not told";
        }
    }

    // A Wonder's site, begun: the villager's first work on it.
    if (verdict == "ok") {
        const int wonder_id = cfg.GetUnitIdByName("WNDR");
        int wx = 0, wy = 0;
        const bool open = OpenGround(grid, home.x.ToInt(), home.y.ToInt(), kWonderClearing, wx, wy);
        const entt::entity site =
            open ? ECS::Buildings::Place(reg, grid, wonder_id, us, wx, wy, false) : entt::null;
        if (site == entt::null) {
            verdict = "a Wonder's site could not be placed";
        } else {
            queue.PushCommand(Core::RepairCommand{{villager}, site, us});
            const bool begun = Hears(sim, ECS::Events::Kind::Begun, 60, [&](const auto& e) {
                return e.subject == site && e.player == us && e.id == wonder_id;
            });
            if (!begun) verdict = "a Wonder begun was not told";
        }
    }

    // A relic picked up by a monk of ours.
    if (verdict == "ok") {
        const int relic_id = cfg.GetUnitIdByName("RELIC");
        const int monk_id = cfg.GetUnitIdByName("MONKX");
        const ECS::Components::Vector2D spot{home.x - Core::Fixed32(6), home.y + Core::Fixed32(6)};
        const entt::entity relic = ECS::Units::Spawn(reg, relic_id, Core::PLAYER_GAIA, spot);
        const entt::entity monk = ECS::Units::Spawn(reg, monk_id, us, {spot.x + Core::Fixed32(3), spot.y});
        if (relic == entt::null || monk == entt::null) {
            verdict = "no relic or monk to try";
        } else {
            queue.PushCommand(Core::RelicCommand{monk, relic, us});
            const bool picked = Hears(sim, ECS::Events::Kind::PickedUp, 15, [&](const auto& e) {
                return e.subject == monk && e.other == relic && e.player == us;
            });
            if (!picked) verdict = "a relic picked up was not told";
        }
    }

    std::printf("notices=%s\n", verdict.c_str());
    return verdict == "ok";
}

} // namespace Verify
