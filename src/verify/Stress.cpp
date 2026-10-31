#include "verify/Stress.h"

#include <vector>

#include "core/Command.h"
#include "core/CommandQueue.h"
#include "core/ConfigManager.h"
#include "ecs/ScenarioWorld.h"
#include "ecs/Units.h"
#include "ecs/components/Building.h"
#include "ecs/components/Identity.h"
#include "ecs/components/Owner.h"
#include "ecs/components/Transform.h"
#include "verify/Shore.h"

namespace Verify::Stress {

namespace {

constexpr int kPlayers = 8;
constexpr int kKnights = 25;
constexpr int kArchers = 25;
constexpr int kPikemen = 20;
constexpr int kGalleys = 8;
constexpr int kTowers = 4;
constexpr int kSkirmishers = 20;   // of each of archers and spearmen, a side
// How far either side of the middle between two towns the skirmishers stand.
constexpr int kSkirmishApart = 5;
// How far from its Town Center a player's army and towers stand, in tiles.
constexpr int kArmyOut = 10;
constexpr int kTowerOut = 7;
// How far from its town a fleet may be put to sea, in tiles.
constexpr int kSeaReach = 40;

entt::entity TownCenter(entt::registry& reg, Core::PlayerId player) {
    const int tc = Core::ConfigManager::Get().GetUnitIdByName("RTWC");
    auto view = reg.view<const ECS::Components::Identity, const ECS::Components::Owner, const ECS::Components::Building>();
    for (auto e : view) {
        if (view.get<const ECS::Components::Owner>(e).player_id == player &&
            view.get<const ECS::Components::Identity>(e).unit_id == tc) {
            return e;
        }
    }
    return entt::null;
}

// The open tiles of a ring round (x, y), on land or at sea.
void Ring(const ECS::Simulation& sim, int x, int y, int radius, bool sea, std::vector<ECS::Components::Vector2D>& out) {
    auto& grid = const_cast<ECS::Simulation&>(sim).GetGrid();
    const Physics::Layer layer = sea ? Shore::Sea(sim) : Physics::kLand;
    for (int dy = -radius; dy <= radius; ++dy) {
        for (int dx = -radius; dx <= radius; ++dx) {
            if (std::abs(dx) != radius && std::abs(dy) != radius) continue;
            const int tx = x + dx, ty = y + dy;
            if (!grid.InBounds(tx, ty) || !grid.Passable(layer, tx, ty)) continue;
            if (sea && grid.IsWalkable(tx, ty)) continue;
            out.push_back(Shore::At(tx, ty));
        }
    }
}

} // namespace

Core::GameSetup Setup() {
    Core::GameSetup setup = Core::DefaultSetup();
    setup.map_script = "Coastal";
    setup.map_size = 240;
    setup.seed = 7;
    setup.starting_age = 3;
    setup.resources = Core::StartingResources::High;
    setup.population_limit = 200;
    for (int p = 1; p <= kPlayers; ++p) {
        setup.players[p].active = true;
        setup.players[p].civ = 1 + (p - 1) % 13;
        setup.players[p].computer = true;
    }
    return setup;
}

Forces Populate(ECS::Simulation& sim) {
    Forces forces;
    auto& reg = sim.GetRegistry();
    auto& grid = sim.GetGrid();
    const auto& cfg = Core::ConfigManager::Get();
    const int knight = cfg.GetUnitIdByName("KNGHT");
    const int archer = cfg.GetUnitIdByName("ARCHR");
    const int pikeman = cfg.GetUnitIdByName("PKEMN");
    const int galley = cfg.GetUnitIdByName("GALLY");
    const int tower = cfg.GetUnitIdByName("WCTW");
    const int spearman = cfg.GetUnitIdByName("SPRMN");
    std::vector<entt::entity> centers(kPlayers + 1);
    for (auto& c : centers) c = entt::null;
    std::vector<std::vector<entt::entity>> armies(kPlayers + 1), fleets(kPlayers + 1);
    std::vector<ECS::Components::Vector2D> spots;
    for (int p = 1; p <= kPlayers; ++p) {
        const auto player = static_cast<Core::PlayerId>(p);
        centers[p] = TownCenter(reg, player);
        if (centers[p] == entt::null) continue;
        const auto& home = reg.get<ECS::Components::Transform>(centers[p]).position;
        const int hx = home.x.ToInt(), hy = home.y.ToInt();

        // The army, ring by ring out from the town.
        spots.clear();
        for (int r = kArmyOut; r < kArmyOut + 8 && static_cast<int>(spots.size()) < kKnights + kArchers + kPikemen; ++r) {
            Ring(sim, hx, hy, r, false, spots);
        }
        size_t s = 0;
        for (const auto& [unit, count] : {std::pair{knight, kKnights}, std::pair{archer, kArchers}, std::pair{pikeman, kPikemen}}) {
            for (int i = 0; i < count && s < spots.size(); ++i, ++s) {
                const entt::entity e = ECS::Units::Spawn(reg, unit, player, spots[s]);
                if (e != entt::null) armies[p].push_back(e);
            }
        }

        // The towers, on the open tiles nearest a ring round the town.
        spots.clear();
        Ring(sim, hx, hy, kTowerOut, false, spots);
        for (int t = 0; t < kTowers && !spots.empty(); ++t) {
            const auto& at = spots[spots.size() * static_cast<size_t>(t) / kTowers];
            int tx = 0, ty = 0;
            if (Shore::NearestSpot(grid, tower, at.x.ToInt(), at.y.ToInt(), 4, tx, ty)) {
                ECS::ScenarioWorld::Place(reg, grid, tower, player, Shore::At(tx, ty), {}, 0);
            }
        }

        // The fleet, on the nearest water wide enough.
        spots.clear();
        for (int r = 1; r <= kSeaReach && static_cast<int>(spots.size()) < kGalleys * 3; ++r) Ring(sim, hx, hy, r, true, spots);
        for (int i = 0; i < kGalleys && static_cast<size_t>(i * 3) < spots.size(); ++i) {
            const entt::entity e = ECS::Units::Spawn(reg, galley, player, spots[static_cast<size_t>(i * 3)]);
            if (e != entt::null) fleets[p].push_back(e);
        }
    }

    // Skirmishes half way between each odd player's town and the next's.
    auto& queue = Core::CommandQueue::Get();
    for (int p = 1; p + 1 <= kPlayers; p += 2) {
        const int q = p + 1;
        if (centers[p] == entt::null || centers[q] == entt::null) continue;
        const auto& a = reg.get<ECS::Components::Transform>(centers[p]).position;
        const auto& b = reg.get<ECS::Components::Transform>(centers[q]).position;
        const int mx = (a.x.ToInt() + b.x.ToInt()) / 2, my = (a.y.ToInt() + b.y.ToInt()) / 2;
        std::vector<entt::entity> sides[2];
        for (int side = 0; side < 2; ++side) {
            const auto player = static_cast<Core::PlayerId>(side == 0 ? p : q);
            const int cx = mx + (side == 0 ? -kSkirmishApart : kSkirmishApart);
            spots.clear();
            for (int r = 0; r < 8 && static_cast<int>(spots.size()) < kSkirmishers * 2; ++r) Ring(sim, cx, my, r, false, spots);
            for (size_t i = 0; i < spots.size() && i < static_cast<size_t>(kSkirmishers * 2); ++i) {
                const entt::entity e = ECS::Units::Spawn(reg, i % 2 == 0 ? archer : spearman, player, spots[i]);
                if (e != entt::null) sides[side].push_back(e);
            }
        }
        for (int side = 0; side < 2; ++side) {
            const auto player = static_cast<Core::PlayerId>(side == 0 ? p : q);
            const auto& foes = sides[1 - side];
            for (size_t i = 0; i < sides[side].size() && !foes.empty(); ++i) {
                queue.PushCommand(Core::AttackCommand{sides[side][i], foes[i % foes.size()], player});
            }
            forces.skirmish += static_cast<int>(sides[side].size());
        }
    }

    // Each player's army at the next one's Town Center, its fleet at the
    // next one's fleet.
    for (int p = 1; p <= kPlayers; ++p) {
        const int q = p % kPlayers + 1;
        const auto player = static_cast<Core::PlayerId>(p);
        if (centers[q] != entt::null) {
            for (entt::entity e : armies[p]) queue.PushCommand(Core::AttackCommand{e, centers[q], player});
        }
        for (size_t i = 0; i < fleets[p].size() && !fleets[q].empty(); ++i) {
            queue.PushCommand(Core::AttackCommand{fleets[p][i], fleets[q][i % fleets[q].size()], player});
        }
        forces.army += static_cast<int>(armies[p].size());
        forces.fleet += static_cast<int>(fleets[p].size());
    }
    return forces;
}

} // namespace Verify::Stress
