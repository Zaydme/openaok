#include "verify/Harness.h"

#include <cstdio>
#include <cstdlib>
#include <functional>
#include <string>
#include <vector>

#include "core/Command.h"
#include "core/CommandQueue.h"
#include "core/ConfigManager.h"
#include "ecs/Animals.h"
#include "ecs/Buildings.h"
#include "ecs/Events.h"
#include "ecs/Players.h"
#include "ecs/Production.h"
#include "ecs/Simulation.h"
#include "ecs/Techs.h"
#include "ecs/Vision.h"
#include "ecs/Units.h"
#include "ecs/components/Building.h"
#include "ecs/components/Identity.h"
#include "ecs/components/Owner.h"
#include "ecs/components/ProductionQueue.h"
#include "ecs/components/Resource.h"
#include "ecs/components/Transform.h"
#include "ecs/components/Worker.h"
#include "physics/Grid.h"

namespace Verify {

namespace {

constexpr int kTicksPerSecond = 1000 / 16;

// How many events of `kind` of `player`'s the log holds over `seconds`.
int Count(ECS::Simulation& sim, ECS::Events::Kind kind, Core::PlayerId player, int seconds) {
    int n = 0;
    for (int t = 0; t < seconds * kTicksPerSecond; ++t) {
        sim.Update(16);
        const auto* log = ECS::Events::Read(sim.GetRegistry());
        if (log == nullptr) return n;
        for (const auto& e : log->events) {
            if (e.tick == log->tick && e.kind == kind && e.player == player) ++n;
        }
    }
    return n;
}

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

    // A Wonder's site, begun: the villager's first work on it is told, and
    // from then every player sees its ground.
    if (verdict == "ok") {
        const int wonder_id = cfg.GetUnitIdByName("WNDR");
        int wx = 0, wy = 0;
        const bool open = OpenGround(grid, home.x.ToInt(), home.y.ToInt(), kWonderClearing, wx, wy);
        const entt::entity site =
            open ? ECS::Buildings::Place(reg, grid, wonder_id, us, wx, wy, false) : entt::null;
        if (site == entt::null) {
            verdict = "a Wonder's site could not be placed";
        } else {
            // Whether every player in the game but us has seen its ground.
            auto seen_by_all = [&](bool all) {
                for (Core::PlayerId p = 1; p < Core::kMaxPlayers; ++p) {
                    if (p == us || ECS::Players::Find(reg, p) == nullptr) continue;
                    if (ECS::Vision::Explored(reg, p, wx, wy) != all) return false;
                }
                return true;
            };
            const bool unseen_before = seen_by_all(false);
            queue.PushCommand(Core::RepairCommand{{villager}, site, us});
            const bool begun = Hears(sim, ECS::Events::Kind::Begun, 60, [&](const auto& e) {
                return e.subject == site && e.player == us && e.id == wonder_id;
            });
            if (!begun) verdict = "a Wonder begun was not told";
            else if (!unseen_before) verdict = "a Wonder's site was seen by the others before it was begun";
            else if (!seen_by_all(true)) verdict = "a Wonder begun is not seen by every player";
        }
    }

    // Production halted for want of houses: told once, by the first
    // building to wait, then not again until every one has gone on.
    if (verdict == "ok") {
        ECS::Players::Player* me = ECS::Players::Find(reg, us);
        const int villager_id = cfg.GetUnitIdByName("VMBAS");
        const int militia_id = cfg.GetUnitIdByName("SPRMN");
        entt::entity tc = entt::null;
        auto buildings = reg.view<ECS::Components::Building, ECS::Components::Identity,
                                  ECS::Components::Owner>();
        for (auto b : buildings) {
            if (buildings.get<ECS::Components::Identity>(b).unit_id == cfg.GetUnitIdByName("RTWC") &&
                buildings.get<ECS::Components::Owner>(b).player_id == us) {
                tc = b;
            }
        }
        int bx = 0, by = 0;
        const bool open = OpenGround(grid, home.x.ToInt(), home.y.ToInt(), 3, bx, by);
        const entt::entity barracks =
            open ? ECS::Buildings::Place(reg, grid, cfg.GetUnitIdByName("BRKS"), us, bx, by, true)
                 : entt::null;
        if (me == nullptr || tc == entt::null || barracks == entt::null) {
            verdict = "no Town Center or Barracks to train at";
        } else {
            ECS::Players::AddStock(*me, Core::ResourceType::Food, 1000);
            const Core::Fixed32 room = ECS::Players::Resource(*me, Core::Rules::Res::PopulationHeadroom);
            ECS::Players::SetResource(*me, Core::Rules::Res::PopulationHeadroom, Core::Fixed32(0));
            ECS::Production::Enqueue(reg, tc, villager_id, us);
            const int first = Count(sim, ECS::Events::Kind::Halted, us, 2);
            ECS::Production::Enqueue(reg, barracks, militia_id, us);
            const int second = Count(sim, ECS::Events::Kind::Halted, us, 2);
            ECS::Players::SetResource(*me, Core::Rules::Res::PopulationHeadroom, room + Core::Fixed32(10));
            Count(sim, ECS::Events::Kind::Halted, us, 2);
            ECS::Players::SetResource(*me, Core::Rules::Res::PopulationHeadroom, Core::Fixed32(0));
            ECS::Production::Enqueue(reg, tc, villager_id, us);
            const int again = Count(sim, ECS::Events::Kind::Halted, us, 60);
            ECS::Players::SetResource(*me, Core::Rules::Res::PopulationHeadroom, room);
            if (first != 1) verdict = "production halted for want of houses was not told once";
            else if (second != 0) verdict = "a second building halted was told again";
            else if (again != 1) verdict = "production halted anew was not told";
        }
    }

    // Research finished at a building is told with that building: Loom at
    // the Town Center.
    if (verdict == "ok") {
        constexpr int kLoom = 22;
        entt::entity tc = entt::null;
        auto buildings = reg.view<ECS::Components::Building, ECS::Components::Identity,
                                  ECS::Components::Owner>();
        for (auto b : buildings) {
            if (buildings.get<ECS::Components::Identity>(b).unit_id == cfg.GetUnitIdByName("RTWC") &&
                buildings.get<ECS::Components::Owner>(b).player_id == us) {
                tc = b;
            }
        }
        ECS::Players::Player* me = ECS::Players::Find(reg, us);
        if (tc == entt::null || me == nullptr) {
            verdict = "no Town Center to research at";
        } else {
            ECS::Players::AddStock(*me, Core::ResourceType::Gold, 500);
            ECS::Players::AddStock(*me, Core::ResourceType::Food, 500);
            auto& queue_at = reg.get<ECS::Components::ProductionQueue>(tc);
            while (queue_at.count > 0) ECS::Production::Cancel(reg, tc, queue_at.count - 1, us);
            ECS::Production::EnqueueTech(reg, tc, kLoom, us);
            const bool named = Hears(sim, ECS::Events::Kind::Researched, 60, [&](const auto& e) {
                return e.id == kLoom && e.player == us && e.subject == tc;
            });
            if (!named) verdict = "research finished was not told with its building";
        }
    }

    // Spies known by what it does: the Castle's Spies sets the enemy's sight;
    // Loom does not.
    if (verdict == "ok") {
        constexpr int kSpiesTech = 408;
        constexpr int kLoomTech = 22;
        if (!ECS::Techs::Spies(kSpiesTech)) verdict = "Spies is not known as Spies";
        else if (ECS::Techs::Spies(kLoomTech)) verdict = "Loom is taken for Spies";
    }

    // A building site found taken: two villagers sent the same tick to put a
    // Lumber Camp on a House's ground hear of it once, and nothing is laid
    // out or paid for.
    if (verdict == "ok") {
        ECS::Players::Player* me = ECS::Players::Find(reg, us);
        const int camp_id = cfg.GetUnitIdByName("SMIL");
        std::vector<entt::entity> crew;
        for (auto e : workers) {
            if (workers.get<ECS::Components::Owner>(e).player_id == us && crew.size() < 2) crew.push_back(e);
        }
        int hx = 0, hy = 0;
        const bool open = OpenGround(grid, home.x.ToInt(), home.y.ToInt(), 3, hx, hy);
        const entt::entity house =
            open ? ECS::Buildings::Place(reg, grid, cfg.GetUnitIdByName("HOUS"), us, hx, hy, true)
                 : entt::null;
        if (me == nullptr || crew.size() < 2 || house == entt::null) {
            verdict = "no House or crew to try a taken site with";
        } else {
            ECS::Players::AddStock(*me, Core::ResourceType::Wood, 500);
            const Core::Fixed32 wood = ECS::Players::Resource(*me, Core::Rules::Res::Wood);
            const size_t buildings_before = reg.view<ECS::Components::Building>().size();
            for (entt::entity member : crew) {
                queue.PushCommand(Core::BuildCommand{member, camp_id, hx, hy, us});
            }
            const int told = Count(sim, ECS::Events::Kind::Obstructed, us, 1);
            if (told != 1) verdict = "a building site found taken was not told once";
            else if (ECS::Players::Resource(*me, Core::Rules::Res::Wood) != wood) verdict = "a site found taken was paid for";
            else if (reg.view<ECS::Components::Building>().size() != buildings_before) verdict = "a site found taken was laid out";
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
