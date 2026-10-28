#include "verify/Harness.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "core/Command.h"
#include "core/CommandQueue.h"
#include "core/ConfigManager.h"
#include "core/Rules.h"
#include "ecs/Buildings.h"
#include "ecs/Diplomacy.h"
#include "ecs/Players.h"
#include "ecs/Simulation.h"
#include "ecs/Units.h"
#include "ecs/Vision.h"
#include "ecs/components/Building.h"
#include "ecs/components/Health.h"
#include "ecs/components/Identity.h"
#include "ecs/components/Owner.h"
#include "ecs/components/Transform.h"
#include "ecs/components/Worker.h"
#include "physics/Grid.h"

namespace Verify {

namespace {

namespace R = Core::Rules;
constexpr Core::PlayerId kUs = Core::PLAYER_LOCAL;
constexpr int kTicksPerSecond = 1000 / 16;

void Run(ECS::Simulation& sim, int seconds) {
    for (int t = 0; t < seconds * kTicksPerSecond; ++t) sim.Update(16);
}

// A square of open ground `half` tiles each way nearest (x, y), ring by ring
// from `from` tiles out; with `unexplored`, none of it explored by `player`.
bool Open(const ECS::Simulation& sim, int x, int y, int from, int half, int& out_x, int& out_y,
          bool unexplored = false, Core::PlayerId player = kUs) {
    const auto& grid = sim.GetGrid();
    for (int ring = from; ring < grid.GetWidth(); ++ring) {
        for (int dy = -ring; dy <= ring; ++dy) {
            for (int dx = -ring; dx <= ring; ++dx) {
                if (std::abs(dx) != ring && std::abs(dy) != ring) continue;
                bool ok = true;
                for (int oy = -half; oy <= half && ok; ++oy) {
                    for (int ox = -half; ox <= half && ok; ++ox) {
                        const int tx = x + dx + ox, ty = y + dy + oy;
                        ok = grid.IsWalkable(tx, ty) &&
                             (!unexplored || !ECS::Vision::Explored(sim.GetRegistry(), player, tx, ty));
                    }
                }
                if (ok) {
                    out_x = x + dx;
                    out_y = y + dy;
                    return true;
                }
            }
        }
    }
    return false;
}

entt::entity WorkerOf(entt::registry& reg, Core::PlayerId player) {
    auto view = reg.view<ECS::Components::Worker, ECS::Components::Owner>();
    for (auto e : view) {
        if (view.get<ECS::Components::Owner>(e).player_id == player) return e;
    }
    return entt::null;
}

entt::entity SiteAt(entt::registry& reg, Core::PlayerId player, int unit_id, int x, int y) {
    auto view = reg.view<ECS::Components::Building, ECS::Components::Identity, ECS::Components::Owner,
                         ECS::Components::Transform>();
    for (auto e : view) {
        const auto& at = view.get<ECS::Components::Transform>(e).position;
        if (view.get<ECS::Components::Identity>(e).unit_id == unit_id &&
            view.get<ECS::Components::Owner>(e).player_id == player &&
            Physics::TileOf(at.x) == x && Physics::TileOf(at.y) == y) {
            return e;
        }
    }
    return entt::null;
}

int Wood(entt::registry& reg, Core::PlayerId player) {
    return ECS::Players::Resource(*ECS::Players::Find(reg, player), R::Res::Wood).ToInt();
}

} // namespace

bool CheckFoundations() {
    ECS::Simulation sim;
    sim.Init();
    auto& reg = sim.GetRegistry();
    auto& grid = sim.GetGrid();
    auto& cfg = Core::ConfigManager::Get();
    auto& queue = Core::CommandQueue::Get();
    std::string verdict = "ok";
    auto ok = [&]() { return verdict == "ok"; };
    sim.Update(16);

    const int house = cfg.GetUnitIdByName("HOUS");
    const int town_center = cfg.GetUnitIdByName("RTWC");
    const int militia = cfg.GetUnitIdByName("SPRMN");
    Core::PlayerId them = 0;
    for (Core::PlayerId p = 1; p < Core::kMaxPlayers && them == 0; ++p) {
        if (p != kUs && ECS::Players::Find(reg, p) != nullptr && !ECS::Diplomacy::Allied(reg, kUs, p)) them = p;
    }
    const entt::entity builder = WorkerOf(reg, kUs);
    const entt::entity their_builder = them != 0 ? WorkerOf(reg, them) : entt::null;
    if (them == 0 || builder == entt::null || their_builder == entt::null) verdict = "no enemy or no villagers";
    const auto home = ok() ? reg.get<ECS::Components::Transform>(builder).position : ECS::Components::Vector2D{};
    const int hx = home.x.ToInt(), hy = home.y.ToInt();
    if (ok()) {
        for (Core::PlayerId p : {kUs, them}) {
            ECS::Players::AddStock(*ECS::Players::Find(reg, p), Core::ResourceType::Wood, 2000);
            ECS::Players::AddStock(*ECS::Players::Find(reg, p), Core::ResourceType::Stone, 2000);
            ECS::Vision::Explore(reg, p, hx, hy, 24);
        }
    }

    // A foundation not yet begun: no ground held, shown to its owner's side
    // alone, blocking its own side's placing and nobody else's, and not
    // anyone's target.
    int fx = 0, fy = 0;
    entt::entity site = entt::null;
    if (ok() && Open(sim, hx, hy, 6, 2, fx, fy)) {
        site = ECS::Buildings::Place(reg, grid, house, kUs, fx, fy, false);
        sim.Update(16);
        ECS::Vision::ToggleSeeAll(reg, them);
        const bool hidden = !ECS::Vision::Shows(reg, them, site);
        ECS::Vision::ToggleSeeAll(reg, them);
        if (site == entt::null) verdict = "a foundation could not be placed";
        else if (!grid.IsWalkable(fx, fy)) verdict = "a foundation not begun holds its ground";
        else if (!ECS::Vision::Shows(reg, kUs, site)) verdict = "a foundation not begun is hidden from its owner";
        else if (!hidden) verdict = "a foundation not begun is shown to an enemy";
        else if (ECS::Buildings::Check(reg, grid, kUs, house, fx, fy, ECS::Buildings::Ask::Placing) !=
                 ECS::Buildings::Site::Foundation) verdict = "our foundation not begun does not block our placing";
        else if (ECS::Buildings::Check(reg, grid, them, house, fx, fy, ECS::Buildings::Ask::Placing) !=
                 ECS::Buildings::Site::Clear) verdict = "our foundation not begun blocks an enemy's placing";
    } else if (ok()) {
        verdict = "no open ground for a foundation";
    }
    if (ok()) {
        const auto& at = reg.get<ECS::Components::Transform>(site).position;
        const entt::entity soldier = ECS::Units::Spawn(reg, militia, them, {at.x + Core::Fixed32(2), at.y});
        const int hp = reg.get<ECS::Components::Health>(site).current_hp;
        Run(sim, 4);
        if (!reg.valid(site) || reg.get<ECS::Components::Health>(site).current_hp != hp) {
            verdict = "an enemy soldier attacked a foundation not begun";
        }
        if (reg.valid(soldier)) reg.destroy(soldier);
    }

    // What placing runs into: an enemy unit on the ground; ground we have not
    // explored.
    if (ok()) {
        int ux = 0, uy = 0, nx = 0, ny = 0;
        if (!Open(sim, hx, hy, 12, 2, ux, uy) || !Open(sim, hx, hy, 30, 3, nx, ny, true)) {
            verdict = "no ground for the placing tests";
        } else {
            const entt::entity soldier = ECS::Units::Spawn(reg, militia, them, {Core::Fixed32(ux), Core::Fixed32(uy)});
            const auto blocked = ECS::Buildings::Check(reg, grid, kUs, house, ux, uy, ECS::Buildings::Ask::Placing);
            reg.destroy(soldier);
            if (blocked != ECS::Buildings::Site::Unit) verdict = "an enemy unit on a site does not block placing";
            else if (ECS::Buildings::Check(reg, grid, kUs, house, nx, ny, ECS::Buildings::Ask::Placing) !=
                     ECS::Buildings::Site::Unexplored) verdict = "a site never explored was not refused";
        }
    }

    // A building not finished sees only its own ground; finished, all round.
    if (ok()) {
        int tx = 0, ty = 0;
        if (!Open(sim, hx, hy, 30, 7, tx, ty, true)) {
            verdict = "no unexplored ground for a Town Center";
        } else {
            const entt::entity tc = ECS::Buildings::Place(reg, grid, town_center, kUs, tx, ty, false);
            sim.Update(16);
            const bool under = ECS::Vision::Explored(reg, kUs, tx, ty);
            const bool beyond = ECS::Vision::Explored(reg, kUs, tx + 5, ty);
            ECS::Buildings::Complete(reg, grid, tc);
            sim.Update(16);
            if (!under) verdict = "a foundation does not see its own ground";
            else if (beyond) verdict = "a foundation sees beyond its own ground";
            else if (!ECS::Vision::Explored(reg, kUs, tx + 5, ty)) verdict = "a finished building does not see round itself";
        }
    }

    // Overlapping foundations: whichever is begun first stands, and the
    // other, an enemy's, goes with all of its price back.
    if (ok()) {
        int ox = 0, oy = 0;
        if (!Open(sim, hx, hy, 18, 2, ox, oy)) {
            verdict = "no open ground for overlapping foundations";
        } else {
            const int before = Wood(reg, them);
            queue.PushCommand(Core::BuildCommand{their_builder, house, ox, oy, them});
            sim.Update(16);
            const entt::entity theirs = SiteAt(reg, them, house, ox, oy);
            const int paid = Wood(reg, them);
            queue.PushCommand(Core::BuildCommand{builder, house, ox, oy, kUs});
            sim.Update(16);
            const entt::entity ours = SiteAt(reg, kUs, house, ox, oy);
            for (int t = 0; t < 40 * kTicksPerSecond && reg.valid(ours) && !ECS::Buildings::Begun(reg, ours); ++t) {
                sim.Update(16);
            }
            if (theirs == entt::null || paid >= before) verdict = "the enemy's foundation was not laid out and paid for";
            else if (ours == entt::null) verdict = "a foundation over an enemy's not begun was refused";
            else if (!ECS::Buildings::Begun(reg, ours)) verdict = "our foundation was never begun";
            else if (reg.valid(theirs)) verdict = "the enemy's foundation outlasted ours being begun";
            else if (Wood(reg, them) != before) verdict = "the enemy's foundation was not refunded in full";
            else if (grid.IsWalkable(ox, oy)) verdict = "a foundation begun does not hold its ground";
        }
    }

    // Deleting refunds what is not yet built: all of a foundation not begun,
    // half of one half built, nothing of a finished building.
    if (ok()) {
        int dx = 0, dy = 0;
        const R::UnitRules* rules = ECS::Players::UnitOf(reg, kUs, house);
        int price = 0;
        for (int i = 0; i < rules->cost_count; ++i) {
            if (rules->costs[i].resource == R::Res::Wood) price = rules->costs[i].amount;
        }
        if (!Open(sim, hx, hy, 24, 2, dx, dy) || price <= 0) {
            verdict = "no ground or price for the refunds";
        } else {
            auto refund_of = [&](int stage) {
                ECS::Players::AddStock(*ECS::Players::Find(reg, kUs), Core::ResourceType::Wood, -price);
                const entt::entity b = ECS::Buildings::Place(reg, grid, house, kUs, dx, dy, stage == 2);
                if (stage == 1) {
                    ECS::Buildings::Begin(reg, grid, b);
                    reg.get<ECS::Components::Building>(b).progress = Core::Fixed32(1) / Core::Fixed32(2);
                }
                const int before = Wood(reg, kUs);
                queue.PushCommand(Core::DeleteCommand{b, kUs});
                sim.Update(16);
                Run(sim, 1);
                return Wood(reg, kUs) - before;
            };
            const int whole = refund_of(0);
            const int half = refund_of(1);
            const int none = refund_of(2);
            if (whole != price) verdict = "a foundation not begun was not refunded in full";
            else if (half != price / 2) verdict = "a half-built foundation was not refunded half";
            else if (none != 0) verdict = "a finished building was refunded";
        }
    }

    // The ground's lie, by the dat's hill rule. A Barracks (3) minds only its
    // two front edges, within a level of the front corner: its back corner
    // two levels up is no matter, an edge two up is. A Town Center (2) wants
    // all its ground flat, and a tile beside it one level up makes the one
    // next to it a slope.
    if (ok()) {
        const int barracks = cfg.GetUnitIdByName("BRKS");
        int gx = 0, gy = 0;
        if (!Open(sim, hx, hy, 34, 5, gx, gy)) {
            verdict = "no open ground for the hill rules";
        } else {
            auto level = [&](int x, int y) -> int& { return grid.GetTileMut(x, y).elevation; };
            const int base = level(gx, gy);
            for (int y = gy - 5; y <= gy + 5; ++y) {
                for (int x = gx - 5; x <= gx + 5; ++x) level(x, y) = base;
            }
            const auto f = ECS::Buildings::FootprintOf(barracks);
            const int bx0 = gx + f.x.low, by0 = gy + f.y.low, bx1 = gx + f.x.high, by1 = gy + f.y.high;
            level(bx0, by0) = base + 2;
            level(bx0 + 1, by0) = base + 1;
            level(bx0, by0 + 1) = base + 1;
            const bool back = ECS::Buildings::CanPlace(grid, barracks, gx, gy);
            level(bx1, by0) = base + 2;
            const bool front = ECS::Buildings::CanPlace(grid, barracks, gx, gy);
            for (int y = by0; y <= by1; ++y) {
                for (int x = bx0; x <= bx1; ++x) level(x, y) = base;
            }
            const auto t = ECS::Buildings::FootprintOf(town_center);
            const bool flat = ECS::Buildings::CanPlace(grid, town_center, gx, gy);
            level(gx + t.x.high + 1, gy) = base + 1;
            const bool beside = ECS::Buildings::CanPlace(grid, town_center, gx, gy);
            level(gx + t.x.high + 1, gy) = base;
            if (!back) verdict = "a Barracks was refused for its back corner, which the rule does not look at";
            else if (front) verdict = "a Barracks was placed with a front edge two levels off";
            else if (!flat) verdict = "a Town Center was refused on flat ground";
            else if (beside) verdict = "a Town Center was placed beside a rise";
        }
    }

    // A dock by the dat's placement terrains: its middle tile on water, the
    // beach straight beside that tile -- within its own ground, half ashore --
    // and nothing asked of the ring around it. Its middle on the beach will
    // not do, nor the beach only at a corner of the middle.
    if (ok()) {
        const int dock = cfg.GetUnitIdByName("DOCK");
        Core::TerrainType water{}, beach{};
        for (int t = 0; t < static_cast<int>(Core::TerrainType::COUNT); ++t) {
            const int dat = cfg.GetTerrainArt(static_cast<Core::TerrainType>(t)).dat_id;
            if (dat == 1) water = static_cast<Core::TerrainType>(t);
            if (dat == 2) beach = static_cast<Core::TerrainType>(t);
        }
        int wx = 0, wy = 0;
        if (!Open(sim, hx, hy, 46, 4, wx, wy)) {
            verdict = "no open ground for the docks";
        } else {
            std::vector<Core::TerrainType> was;
            for (int y = wy - 4; y <= wy + 4; ++y) {
                for (int x = wx - 4; x <= wx + 4; ++x) {
                    was.push_back(grid.GetTile(x, y).terrain);
                    grid.SetTerrain(x, y, water);
                }
            }
            grid.SetTerrain(wx + 1, wy, beach);
            const bool ashore = ECS::Buildings::CanPlace(grid, dock, wx, wy);
            grid.SetTerrain(wx + 1, wy, water);
            grid.SetTerrain(wx + 1, wy + 1, beach);
            const bool corner = ECS::Buildings::CanPlace(grid, dock, wx, wy);
            grid.SetTerrain(wx + 1, wy + 1, water);
            grid.SetTerrain(wx, wy, beach);
            grid.SetTerrain(wx + 1, wy, beach);
            const bool aground = ECS::Buildings::CanPlace(grid, dock, wx, wy);
            size_t i = 0;
            for (int y = wy - 4; y <= wy + 4; ++y) {
                for (int x = wx - 4; x <= wx + 4; ++x) grid.SetTerrain(x, y, was[i++]);
            }
            if (!ashore) verdict = "a dock was refused with its middle on water and the beach beside it";
            else if (corner) verdict = "a dock was placed with the beach only at a corner of its middle";
            else if (aground) verdict = "a dock was placed with its middle on the beach";
        }
    }

    std::printf("foundations=%s\n", verdict.c_str());
    return verdict == "ok";
}

} // namespace Verify
