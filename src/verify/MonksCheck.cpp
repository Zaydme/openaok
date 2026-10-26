#include "verify/Harness.h"

#include <cstdio>
#include <cstring>

#include "core/Command.h"
#include "core/CommandQueue.h"
#include "core/ConfigManager.h"
#include "core/Rules.h"
#include "ecs/Buildings.h"
#include "ecs/Events.h"
#include "ecs/Mortality.h"
#include "ecs/Players.h"
#include "ecs/Relics.h"
#include "ecs/Simulation.h"
#include "ecs/Units.h"
#include "ecs/components/Combat.h"
#include "ecs/components/Health.h"
#include "ecs/components/Identity.h"
#include "ecs/components/Monk.h"
#include "ecs/components/Owner.h"
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

Core::PlayerId OwnerOf(const entt::registry& reg, entt::entity e) {
    return reg.get<ECS::Components::Owner>(e).player_id;
}

int Population(const entt::registry& reg, Core::PlayerId player) {
    return ECS::Players::Resource(*ECS::Players::Find(reg, player),
                                  Core::Rules::Res::CurrentPopulation).ToInt();
}

} // namespace

// Monks by the dat's tasks: a conversion that takes at least its task's
// shortest time and moves the unit's place in the population, faith that must
// come back before the next, no buildings without Redemption, healing their
// own, and the news a conversion has begun.
bool CheckMonks() {
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
    const int monk_id = cfg.GetUnitIdByName("MONKX");
    const int militia_id = cfg.GetUnitIdByName("SPRMN");
    const int house_id = cfg.GetUnitIdByName("HOUS");

    int x = grid.GetWidth() / 2;
    int y = grid.GetHeight() / 2;
    if (monk_id < 0 || !Clearing(grid, x, y, 8)) {
        std::printf("monks=no monk or open ground\n");
        return false;
    }

    const entt::entity monk = ECS::Units::Spawn(reg, monk_id, kUs, At(x, y));
    if (!reg.all_of<ECS::Components::Monk>(monk)) verdict = "a monk has no faith by its tasks";

    // An enemy in range, told not to fight back.
    auto pacified = [&](entt::entity e) {
        reg.get<ECS::Components::Combat>(e).stance = ECS::Components::Stance::NoAttack;
        return e;
    };
    const entt::entity first = ok() ? pacified(ECS::Units::Spawn(reg, militia_id, kThem, At(x + 4, y)))
                                    : entt::null;
    if (ok()) {
        const int ours = Population(reg, kUs);
        const int theirs = Population(reg, kThem);
        queue.PushCommand(Core::AttackCommand{monk, first, kUs});
        bool heard = false;
        int took = -1;
        for (int t = 0; t < 12 * kTicksPerSecond && took < 0; ++t) {
            headless.Update(16);
            if (const ECS::Events::Log* log = ECS::Events::Read(reg)) {
                for (const auto& e : log->events) {
                    if (e.tick == log->tick && e.kind == ECS::Events::Kind::Converting &&
                        e.subject == first && e.player == kThem) {
                        heard = true;
                    }
                }
            }
            if (OwnerOf(reg, first) == kUs) took = t;
        }
        if (took < 0) {
            verdict = "a monk in range did not convert an enemy within ten seconds";
        } else if (took * 16 < 4000 - 32) {
            verdict = "a conversion took less than the convert task's shortest time";
        } else if (!heard) {
            verdict = "a conversion began without news of it for the alarm";
        } else if (Population(reg, kUs) != ours + 1 || Population(reg, kThem) != theirs - 1) {
            verdict = "a converted unit's place in the population did not change sides";
        } else if (reg.get<ECS::Components::Monk>(monk).faith.value != 0) {
            verdict = "a conversion did not spend the monk's faith";
        }
    }

    // Every conversion takes at least the task's shortest time and at most
    // the longest: five more monks, five more enemies, timed. One alone could
    // pass by luck -- a 28% roll failing four times running -- five cannot.
    if (ok()) {
        for (int i = 0; i < 5 && ok(); ++i) {
            const entt::entity priest = ECS::Units::Spawn(reg, monk_id, kUs, At(x - 6, y - 6 + i * 3));
            const entt::entity foe = pacified(
                ECS::Units::Spawn(reg, militia_id, kThem, At(x - 3, y - 6 + i * 3)));
            queue.PushCommand(Core::AttackCommand{priest, foe, kUs});
            int took = -1;
            for (int t = 0; t < 12 * kTicksPerSecond && took < 0; ++t) {
                headless.Update(16);
                if (OwnerOf(reg, foe) == kUs) took = t;
            }
            // In milliseconds, a tick's slack either way: a second is not a
            // whole number of 16 ms ticks.
            if (took < 0 || took * 16 < 4000 - 32) {
                verdict = "a conversion took less than the convert task's shortest time";
            } else if (took * 16 > 10000 + 32) {
                verdict = "a conversion dragged past the longest it may take";
            }
        }
    }

    // Without faith, no second conversion until it has come back.
    if (ok()) {
        const entt::entity second = pacified(ECS::Units::Spawn(reg, militia_id, kThem, At(x - 4, y)));
        queue.PushCommand(Core::AttackCommand{monk, second, kUs});
        run(15 * kTicksPerSecond);
        if (OwnerOf(reg, second) != kThem) {
            verdict = "a monk converted again before its faith came back";
        } else {
            run(60 * kTicksPerSecond);
            if (OwnerOf(reg, second) != kUs) {
                verdict = "a monk with its faith back did not finish the conversion";
            }
        }
    }

    // A building is not to be had without Redemption.
    if (ok()) {
        int hx = x;
        int hy = y + 6;
        Clearing(grid, hx, hy, 2);
        const entt::entity house = ECS::Buildings::Place(reg, grid, house_id, kThem, hx, hy, true);
        run(70 * kTicksPerSecond);   // faith back in full
        queue.PushCommand(Core::AttackCommand{monk, house, kUs});
        run(15 * kTicksPerSecond);
        if (house == entt::null || OwnerOf(reg, house) != kThem) {
            verdict = "a monk converted a building without Redemption";
        }
    }

    // Its own wounded, healed without being told.
    if (ok()) {
        queue.PushCommand(Core::StopCommand{monk, kUs});
        const entt::entity hurt = ECS::Units::Spawn(reg, militia_id, kUs,
            {reg.get<ECS::Components::Transform>(monk).position.x + Core::Fixed32(2),
             reg.get<ECS::Components::Transform>(monk).position.y});
        auto& hp = reg.get<ECS::Components::Health>(hurt);
        hp.current_hp = hp.max_hp - 20;
        run(20 * kTicksPerSecond);
        if (reg.get<ECS::Components::Health>(hurt).current_hp < hp.max_hp) {
            verdict = "a monk did not heal its own wounded nearby";
        }
    }

    // A relic: fetched by a monk, which becomes the dat's monk with a relic,
    // put in a monastery, where it pays half a gold a second; and dropped
    // where its carrier falls.
    if (ok()) {
        const int relic_id = cfg.GetUnitIdByName("RELIC");
        const int monastery_id = cfg.GetUnitIdByName("CRCH");
        int rx = x + 10;
        int ry = y;
        Clearing(grid, rx, ry, 4);
        const entt::entity relic = ECS::Units::Spawn(reg, relic_id, Core::PLAYER_GAIA, At(rx, ry));
        const entt::entity shrine = ECS::Buildings::Place(reg, grid, monastery_id, kUs, rx - 5, ry, true);
        const entt::entity fetcher = ECS::Units::Spawn(reg, monk_id, kUs, At(rx + 3, ry));
        queue.PushCommand(Core::RelicCommand{fetcher, relic, kUs});
        run(10 * kTicksPerSecond);
        const int bearer_unit = reg.get<ECS::Components::Identity>(fetcher).unit_id;
        if (relic == entt::null || shrine == entt::null) {
            verdict = "no relic or monastery to try";
        } else if (reg.all_of<ECS::Components::Transform>(relic) || bearer_unit == monk_id) {
            verdict = "a monk sent at a relic did not pick it up";
        } else {
            queue.PushCommand(Core::RelicCommand{fetcher, shrine, kUs});
            run(15 * kTicksPerSecond);
            const auto* me = ECS::Players::Find(reg, kUs);
            if (ECS::Relics::Held(reg, shrine) != 1 ||
                reg.get<ECS::Components::Identity>(fetcher).unit_id != monk_id) {
                verdict = "a monk carrying a relic did not put it in the monastery";
            } else if (ECS::Players::Resource(*me, 7).ToInt() != 1) {
                verdict = "a relic put away was not counted as captured";
            } else {
                const int gold = ECS::Players::Stock(*me, Core::ResourceType::Gold);
                run(20 * kTicksPerSecond);
                const int earned = ECS::Players::Stock(*me, Core::ResourceType::Gold) - gold;
                if (earned < 9 || earned > 11) verdict = "a relic did not pay half a gold a second";
            }
        }
        // A carrier that falls drops it.
        if (ok()) {
            const entt::entity loose = ECS::Units::Spawn(reg, relic_id, Core::PLAYER_GAIA, At(rx, ry + 3));
            const entt::entity carrier = ECS::Units::Spawn(reg, monk_id, kUs, At(rx + 1, ry + 3));
            queue.PushCommand(Core::RelicCommand{carrier, loose, kUs});
            run(10 * kTicksPerSecond);
            if (reg.all_of<ECS::Components::Transform>(loose)) {
                verdict = "a second monk did not pick up a relic";
            } else {
                ECS::Mortality::Kill(reg, grid, carrier);
                run(2);
                if (!reg.all_of<ECS::Components::Transform>(loose)) {
                    verdict = "a fallen carrier did not drop its relic";
                }
            }
        }
    }

    std::printf("monks=%s\n", verdict);
    return ok();
}

} // namespace Verify
