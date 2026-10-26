#include "verify/Harness.h"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "core/Command.h"
#include "core/ConfigManager.h"
#include "core/FixedMath.h"
#include "core/GameSetup.h"
#include "core/Rules.h"
#include "ecs/Buildings.h"
#include "ecs/Events.h"
#include "ecs/Orders.h"
#include "ecs/Players.h"
#include "ecs/Simulation.h"
#include "ecs/Units.h"
#include "ecs/Walls.h"
#include "ecs/components/Building.h"
#include "ecs/components/Combat.h"
#include "ecs/components/Gate.h"
#include "ecs/components/Health.h"
#include "ecs/components/Identity.h"
#include "ecs/components/Owner.h"
#include "ecs/components/Resource.h"
#include "ecs/components/Sprite.h"
#include "ecs/components/SpritePieces.h"
#include "ecs/components/Transform.h"
#include "ecs/components/Worker.h"

namespace Verify {

namespace {

using Core::Fixed32;
using ECS::Components::Vector2D;
constexpr int kTicksPerSecond = 1000 / 16;

void Run(ECS::Simulation& sim, int seconds) {
    for (int t = 0; t < seconds * kTicksPerSecond; ++t) sim.Update(16);
}

Vector2D At(int x, int y) { return {Fixed32(x), Fixed32(y)}; }

// The middle of a square of open ground `half` tiles each way, nothing
// standing on it, nearest the map's middle.
bool OpenSquare(ECS::Simulation& sim, int half, int& out_x, int& out_y) {
    auto& grid = sim.GetGrid();
    const int cx = grid.GetWidth() / 2, cy = grid.GetHeight() / 2;
    for (int r = 0; r < grid.GetWidth() / 2; ++r) {
        for (int dy = -r; dy <= r; ++dy) {
            for (int dx = -r; dx <= r; ++dx) {
                if (std::abs(dx) != r && std::abs(dy) != r) continue;
                bool open = true;
                for (int oy = -half; oy <= half && open; ++oy) {
                    for (int ox = -half; ox <= half && open; ++ox) {
                        const int x = cx + dx + ox, y = cy + dy + oy;
                        open = grid.IsWalkable(x, y);
                    }
                }
                if (open) {
                    out_x = cx + dx;
                    out_y = cy + dy;
                    return true;
                }
            }
        }
    }
    return false;
}

// Takes every unit off a square of ground, so nothing on it is anyone's but
// what the scenario puts there.
void ClearUnits(ECS::Simulation& sim, int x, int y, int half) {
    auto& reg = sim.GetRegistry();
    std::vector<entt::entity> near;
    auto units = reg.view<ECS::Components::Transform, ECS::Components::Health>();
    for (auto e : units) {
        if (reg.all_of<ECS::Components::Building>(e)) continue;
        const auto& p = units.get<ECS::Components::Transform>(e).position;
        if (std::abs(Physics::TileOf(p.x) - x) <= half + 2 && std::abs(Physics::TileOf(p.y) - y) <= half + 2) {
            near.push_back(e);
        }
    }
    for (auto e : near) reg.destroy(e);
}

entt::entity WallAt(const entt::registry& reg, int x, int y) {
    auto view = reg.view<const ECS::Components::Building, const ECS::Components::Identity,
                         const ECS::Components::Transform>();
    for (auto b : view) {
        if (!ECS::Walls::IsWall(view.get<const ECS::Components::Identity>(b).unit_id)) continue;
        const auto& p = view.get<const ECS::Components::Transform>(b).position;
        if (Physics::TileOf(p.x) == x && Physics::TileOf(p.y) == y) return b;
    }
    return entt::null;
}

void CompleteAll(ECS::Simulation& sim, const std::vector<entt::entity>& sites) {
    for (auto site : sites) {
        if (!sim.GetRegistry().valid(site)) continue;
        sim.GetRegistry().get<ECS::Components::Building>(site).progress = Fixed32(1);
        ECS::Buildings::Complete(sim.GetRegistry(), sim.GetGrid(), site);
    }
}

// The piece a segment shows, from its art: the renderer draws a wall's
// sprite at the angle its facing names.
int ShownPiece(const entt::registry& reg, entt::entity wall) {
    const auto* sprite = reg.try_get<ECS::Components::Sprite>(wall);
    return sprite != nullptr ? static_cast<int>(sprite->facing) : -1;
}

int Stock(const entt::registry& reg, Core::PlayerId p, Core::ResourceType type) {
    const auto* player = ECS::Players::Find(reg, p);
    return player != nullptr ? ECS::Players::Stock(*player, type) : 0;
}

bool InsideRing(const Vector2D& at, int x0, int y0, int side) {
    const int x = Physics::TileOf(at.x), y = Physics::TileOf(at.y);
    return x > x0 && x < x0 + side - 1 && y > y0 && y < y0 + side - 1;
}

entt::entity Soldier(ECS::Simulation& sim, int unit_id, Core::PlayerId player, Vector2D at) {
    const entt::entity e = ECS::Units::Spawn(sim.GetRegistry(), unit_id, player, at);
    if (auto* combat = sim.GetRegistry().try_get<ECS::Components::Combat>(e)) {
        combat->stance = ECS::Components::Stance::NoAttack;
    }
    return e;
}

void Order(ECS::Simulation& sim, const Core::Command& command) {
    ECS::Orders::Execute(sim.GetRegistry(), sim.GetGrid(), command);
}

} // namespace

// Walls and gates, from the dat. A palisade dragged out along each of the
// four directions is laid as one foundation a tile, each charged the dat's
// price; finished, every segment shows its line's run and the two ends a
// post, the piece its neighbours choose (the dat's adjacent mode). A segment
// taken out leaves posts either side, and a tower set in the gap turns them
// back into runs. One villager given the line raises all of it, moving on
// from segment to segment. A Gate put on a stone wall turns to it -- along y
// it is GTAA2, along x GTBA2 -- and replaces the segments under it; finished,
// its arch lets its owner and its ally through and stops the enemy, and
// locked, stops everyone.
bool CheckWalls() {
    const char* verdict = "ok";
    auto ok = [&]() { return std::strcmp(verdict, "ok") == 0; };
    auto& cfg = Core::ConfigManager::Get();
    const int palisade = cfg.GetUnitIdByName("WALL");
    const int stone = cfg.GetUnitIdByName("WALL2");
    const int gate_button = cfg.GetUnitIdByName("GTAX2");
    const int gate_y = cfg.GetUnitIdByName("GTAA2");
    const int gate_x = cfg.GetUnitIdByName("GTBA2");
    const int tower = cfg.GetUnitIdByName("WCTW");
    const int militia = cfg.GetUnitIdByName("SPRMN");
    const int villager = cfg.GetUnitIdByName("VMBAS");
    const auto* pal_rules = Core::Rules::Unit(palisade);
    const int pal_wood = pal_rules != nullptr && pal_rules->cost_count > 0 ? pal_rules->costs[0].amount : 0;

    // -- Pieces, in the Dark Age, with palisades ------------------------------
    {
        Core::GameSetup setup = Core::DefaultSetup();
        ECS::Simulation sim;
        sim.Init(setup);
        auto& reg = sim.GetRegistry();
        auto& grid = sim.GetGrid();
        int ax = 0, ay = 0;
        if (!OpenSquare(sim, 10, ax, ay)) verdict = "no open ground for the walls";
        if (ok()) ClearUnits(sim, ax, ay, 10);
        auto* me = ECS::Players::Find(reg, 1);
        if (ok() && me != nullptr) ECS::Players::AddStock(*me, Core::ResourceType::Wood, 1000);

        // The preview of a line before it is laid: a piece a tile, each the
        // one the line will give it.
        if (ok()) {
            const auto ghost = ECS::Walls::Ghost(reg, grid, 1, palisade, ax - 2, ay + 6, ax + 6, ay + 6);
            if (ghost.size() != 9) verdict = "the preview of a dragged wall is not a piece a tile";
            for (size_t i = 0; i < ghost.size() && ok(); ++i) {
                const bool end = i == 0 || i + 1 == ghost.size();
                const int want = end ? ECS::Walls::kPost : ECS::Walls::kAlongX;
                if (!ghost[i].clear || ghost[i].frame != want) {
                    verdict = "the preview of a dragged wall shows the wrong pieces";
                }
            }
        }

        // Along y, along x, and both diagonals, apart from one another.
        struct Stretch { int x0, y0, x1, y1, piece; };
        const Stretch runs[] = {
            {ax - 6, ay - 8, ax - 6, ay, ECS::Walls::kAlongY},
            {ax - 2, ay + 6, ax + 6, ay + 6, ECS::Walls::kAlongX},
            {ax + 1, ay - 8, ax + 5, ay - 4, ECS::Walls::kDown},
            {ax + 2, ay + 2, ax + 7, ay - 3, ECS::Walls::kAcross},
        };
        std::vector<std::vector<entt::entity>> laid;
        for (const Stretch& run : runs) {
            if (!ok()) break;
            const int wood = Stock(reg, 1, Core::ResourceType::Wood);
            auto sites = ECS::Walls::Lay(reg, grid, 1, palisade, run.x0, run.y0, run.x1, run.y1);
            const size_t tiles = ECS::Walls::Line(run.x0, run.y0, run.x1, run.y1).size();
            if (sites.size() != tiles) verdict = "a dragged wall was not laid on every tile of its line";
            else if (wood - Stock(reg, 1, Core::ResourceType::Wood) != pal_wood * static_cast<int>(tiles)) {
                verdict = "a wall line was not charged the dat's price a segment";
            }
            laid.push_back(sites);
        }
        for (auto& sites : laid) CompleteAll(sim, sites);
        for (size_t r = 0; r < laid.size() && ok(); ++r) {
            const auto& sites = laid[r];
            for (size_t i = 0; i < sites.size() && ok(); ++i) {
                const bool end = i == 0 || i + 1 == sites.size();
                const int want = end ? ECS::Walls::kPost : runs[r].piece;
                if (ECS::Walls::PieceOf(reg, sites[i]) != want) {
                    verdict = end ? "a wall's end is not a post" : "a wall's run shows the wrong piece";
                } else if (ShownPiece(reg, sites[i]) != want) {
                    verdict = "a wall segment's art is not its piece";
                }
            }
        }
        // A segment taken out of the run along y: posts either side of the
        // gap. A tower set in it: the run joins it again.
        if (ok()) {
            const auto& column = laid[0];
            const entt::entity gap = column[4];
            const int gx = ax - 6, gy = ay - 4;
            ECS::Buildings::Destroy(reg, grid, gap);
            const entt::entity above = WallAt(reg, gx, gy - 1), below = WallAt(reg, gx, gy + 1);
            if (ShownPiece(reg, above) != ECS::Walls::kPost || ShownPiece(reg, below) != ECS::Walls::kPost) {
                verdict = "a wall either side of a gap is not a post";
            } else {
                ECS::Buildings::Place(reg, grid, tower, 1, gx, gy, true);
                if (ShownPiece(reg, above) != ECS::Walls::kAlongY ||
                    ShownPiece(reg, below) != ECS::Walls::kAlongY) {
                    verdict = "a tower set in a wall does not join it";
                }
            }
        }
        // One villager, one order, a whole line.
        if (ok()) {
            const entt::entity worker = ECS::Units::Spawn(reg, villager, 1, At(ax, ay - 2));
            Order(sim, Core::WallCommand{{worker}, palisade, ax - 2, ay - 1, ax + 3, ay - 1, 1});
            std::vector<entt::entity> line;
            for (int x = ax - 2; x <= ax + 3; ++x) line.push_back(WallAt(reg, x, ay - 1));
            bool all = true;
            for (auto e : line) all = all && e != entt::null;
            if (!all) verdict = "a wall command laid no line";
            for (int s = 0; s < 120 && ok(); ++s) {
                Run(sim, 1);
                bool done = true;
                for (auto e : line) {
                    done = done && reg.valid(e) &&
                           reg.get<ECS::Components::Building>(e).state ==
                               ECS::Components::BuildingState::Complete;
                }
                if (done) break;
                if (s == 119) verdict = "one villager did not raise the whole line";
            }
        }
    }

    // -- Gates, in the Feudal Age, with stone walls ---------------------------
    if (ok()) {
        Core::GameSetup setup = Core::DefaultSetup();
        setup.starting_age = 1;
        setup.players[1].team = 1;
        setup.players[3] = {true, 1, 1};   // player 1's ally
        ECS::Simulation sim;
        sim.Init(setup);
        auto& reg = sim.GetRegistry();
        auto& grid = sim.GetGrid();
        int ax = 0, ay = 0;
        if (!OpenSquare(sim, 9, ax, ay)) verdict = "no open ground for the gate";
        if (ok()) ClearUnits(sim, ax, ay, 9);
        for (Core::PlayerId p : {Core::PlayerId(1), Core::PlayerId(2), Core::PlayerId(3)}) {
            if (auto* pl = ECS::Players::Find(reg, p)) {
                ECS::Players::AddStock(*pl, Core::ResourceType::Stone, 2000);
            }
        }
        // A ring of stone wall nine tiles a side.
        constexpr int kSide = 9;
        const int x0 = ax - 4, y0 = ay - 4, x1 = x0 + kSide - 1, y1 = y0 + kSide - 1;
        std::vector<entt::entity> ring;
        for (const auto& [fx, fy, tx, ty] : {std::array<int, 4>{x0, y0, x1, y0},
                                             std::array<int, 4>{x1, y0, x1, y1},
                                             std::array<int, 4>{x1, y1, x0, y1},
                                             std::array<int, 4>{x0, y1, x0, y0}}) {
            auto sites = ECS::Walls::Lay(reg, grid, 1, stone, fx, fy, tx, ty);
            ring.insert(ring.end(), sites.begin(), sites.end());
        }
        if (ok() && static_cast<int>(ring.size()) != 4 * (kSide - 1)) verdict = "the wall ring was not laid";
        CompleteAll(sim, ring);

        // A Gate on the east side, which runs along y; another on the north,
        // along x.
        const entt::entity builder = ECS::Units::Spawn(reg, villager, 1, At(ax, ay));
        const int stone_before = Stock(reg, 1, Core::ResourceType::Stone);
        Order(sim, Core::BuildCommand{builder, gate_button, x1, ay, 1});
        Order(sim, Core::BuildCommand{builder, gate_button, ax, y0, 1});
        entt::entity east = entt::null, north = entt::null;
        auto gates = reg.view<ECS::Components::Building, ECS::Components::Identity,
                              ECS::Components::Transform>();
        for (auto g : gates) {
            const int id = gates.get<ECS::Components::Identity>(g).unit_id;
            const auto& p = gates.get<ECS::Components::Transform>(g).position;
            if (id == gate_y && Physics::TileOf(p.x) == x1) east = g;
            if (id == gate_x && Physics::TileOf(p.y) == y0) north = g;
        }
        const auto* gate_rules = Core::Rules::Unit(gate_button);
        const int gate_stone = gate_rules != nullptr && gate_rules->cost_count > 0
                             ? gate_rules->costs[0].amount : 0;
        if (ok() && east == entt::null) verdict = "a Gate on a wall along y is not GTAA2";
        else if (ok() && north == entt::null) verdict = "a Gate on a wall along x is not GTBA2";
        else if (ok() && stone_before - Stock(reg, 1, Core::ResourceType::Stone) != 2 * gate_stone) {
            verdict = "a Gate was not charged the dat's price";
        } else if (ok() && WallAt(reg, x1, ay) != entt::null) {
            verdict = "the wall a gate was put on is still there";
        }
        if (ok()) {
            CompleteAll(sim, {east, north});
            // The arch: the gate's own two middle tiles.
            const int arch_y = Physics::TileOf(reg.get<ECS::Components::Transform>(east).position.y);
            const Physics::Layer land = Physics::kLand;
            for (int y = arch_y - 1; y <= arch_y && ok(); ++y) {
                if (!grid.Passable(Physics::Keyed(land, 1), x1, y)) verdict = "a gate is shut to its owner";
                else if (!grid.Passable(Physics::Keyed(land, 3), x1, y)) verdict = "a gate is shut to its owner's ally";
                else if (grid.Passable(Physics::Keyed(land, 2), x1, y)) verdict = "a gate is open to the enemy";
                else if (grid.Passable(land, x1, y)) verdict = "a gate is open to nobody's key";
            }
            if (ok() && grid.Passable(Physics::Keyed(land, 1), x1, arch_y - 2)) {
                verdict = "a gate's post lets its owner through";
            }
        }
        // Through it, from inside: the owner's soldier and its ally's come and
        // go; the enemy's stays out.
        const Vector2D inside = At(ax, ay);
        const Vector2D outside = At(x1 + 4, ay);
        if (ok()) {
            const entt::entity ours = Soldier(sim, militia, 1, At(ax - 1, ay));
            const entt::entity ally = Soldier(sim, militia, 3, At(x1 + 3, ay + 2));
            const entt::entity enemy = Soldier(sim, militia, 2, At(x1 + 3, ay - 2));
            Order(sim, Core::MoveCommand{ours, outside.x, outside.y, 1});
            Order(sim, Core::MoveCommand{ally, inside.x, inside.y, 3});
            Order(sim, Core::MoveCommand{enemy, inside.x, inside.y, 2});
            bool raised = false;
            for (int t = 0; t < 40 * kTicksPerSecond; ++t) {
                sim.Update(16);
                const auto* g = reg.try_get<ECS::Components::Gate>(east);
                const auto* art = reg.try_get<ECS::Components::SpritePieces>(east);
                if (g != nullptr && art != nullptr && g->open && art->piece[0].slp_id == g->open_slp) {
                    raised = true;
                }
            }
            const auto& ours_at = reg.get<ECS::Components::Transform>(ours).position;
            if (InsideRing(ours_at, x0, y0, kSide)) verdict = "the owner's soldier could not leave by its gate";
            else if (!InsideRing(reg.get<ECS::Components::Transform>(ally).position, x0, y0, kSide)) {
                verdict = "an ally's soldier could not come in by the gate";
            } else if (InsideRing(reg.get<ECS::Components::Transform>(enemy).position, x0, y0, kSide)) {
                verdict = "the enemy came in by the gate";
            } else if (!raised) {
                verdict = "the gate was never drawn open for its own side";
            }
            // Locked, it shuts to its own side too.
            if (ok()) {
                Order(sim, Core::GateCommand{east, true, 1});
                Order(sim, Core::GateCommand{north, true, 1});
                Order(sim, Core::MoveCommand{ally, outside.x + Fixed32(1), outside.y, 3});
                Run(sim, 40);
                if (!InsideRing(reg.get<ECS::Components::Transform>(ally).position, x0, y0, kSide)) {
                    verdict = "a locked gate let its owner's ally out";
                }
                const auto* g = reg.try_get<ECS::Components::Gate>(east);
                if (ok() && (g == nullptr || g->open)) verdict = "a locked gate is drawn open";
                if (ok() && grid.Passable(Physics::Keyed(Physics::kLand, 1), x1,
                                          Physics::TileOf(reg.get<ECS::Components::Transform>(east).position.y))) {
                    verdict = "a locked gate is open to its owner";
                }
                // Unlocked again, out it goes.
                Order(sim, Core::GateCommand{east, false, 1});
                Order(sim, Core::MoveCommand{ally, outside.x + Fixed32(1), outside.y, 3});
                Run(sim, 40);
                if (ok() && InsideRing(reg.get<ECS::Components::Transform>(ally).position, x0, y0, kSide)) {
                    verdict = "an unlocked gate did not open again";
                }
            }
            // The enemy cannot lock someone else's gate.
            if (ok()) {
                Order(sim, Core::GateCommand{east, true, 2});
                if (reg.get<ECS::Components::Gate>(east).locked) verdict = "the enemy locked a gate";
            }
        }
    }

    std::printf("walls=%s\n", verdict);
    return ok();
}

} // namespace Verify
