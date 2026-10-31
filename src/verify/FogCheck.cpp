#include "verify/Harness.h"

#include <cstdio>
#include <cstdlib>
#include <string>

#include "core/ConfigManager.h"
#include "ecs/Buildings.h"
#include "ecs/Diplomacy.h"
#include "ecs/Fog.h"
#include "ecs/Players.h"
#include "ecs/Resources.h"
#include "ecs/Simulation.h"
#include "ecs/Units.h"
#include "ecs/Vision.h"
#include "ecs/components/Doppelganger.h"
#include "ecs/components/Identity.h"
#include "ecs/components/Owner.h"
#include "ecs/components/Resource.h"
#include "ecs/components/Transform.h"
#include "ecs/components/Worker.h"
#include "physics/Grid.h"

namespace Verify {

namespace {

namespace C = ECS::Components;
constexpr Core::PlayerId kUs = Core::PLAYER_LOCAL;
constexpr int kTicksPerSecond = 1000 / 16;

void Run(ECS::Simulation& sim, int seconds) {
    for (int t = 0; t < seconds * kTicksPerSecond; ++t) sim.Update(16);
}

// A square of open ground `half` tiles each way nearest (x, y), ring by ring
// from `from` tiles out, none of it explored by us.
bool Unexplored(const ECS::Simulation& sim, int x, int y, int from, int half, int& out_x, int& out_y) {
    const auto& grid = sim.GetGrid();
    for (int ring = from; ring < grid.GetWidth(); ++ring) {
        for (int dy = -ring; dy <= ring; ++dy) {
            for (int dx = -ring; dx <= ring; ++dx) {
                if (std::abs(dx) != ring && std::abs(dy) != ring) continue;
                bool ok = true;
                for (int oy = -half; oy <= half && ok; ++oy) {
                    for (int ox = -half; ox <= half && ok; ++ox) {
                        const int tx = x + dx + ox, ty = y + dy + oy;
                        ok = grid.IsWalkable(tx, ty) && !ECS::Vision::Explored(sim.GetRegistry(), kUs, tx, ty);
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

// `player`'s picture of `source`: the piece of it the minimap marks.
entt::entity PictureOf(entt::registry& reg, Core::PlayerId player, entt::entity source) {
    auto view = reg.view<const C::Doppelganger>();
    for (auto e : view) {
        const auto& d = view.get<const C::Doppelganger>(e);
        if (d.player == player && d.source == source && d.marked) return e;
    }
    return entt::null;
}

// One of ours looking at the tile for a tick, then gone.
void Look(ECS::Simulation& sim, int unit_id, int x, int y) {
    auto& reg = sim.GetRegistry();
    const entt::entity eye = ECS::Units::Spawn(reg, unit_id, kUs, {Core::Fixed32(x + 2), Core::Fixed32(y)});
    sim.Update(16);
    reg.destroy(eye);
    sim.Update(16);
}

} // namespace

bool CheckFog() {
    ECS::Simulation sim;
    sim.Init();
    auto& reg = sim.GetRegistry();
    auto& grid = sim.GetGrid();
    auto& cfg = Core::ConfigManager::Get();
    std::string verdict = "ok";
    auto ok = [&]() { return verdict == "ok"; };
    sim.Update(16);

    const int house = cfg.GetUnitIdByName("HOUS");
    const int farm = cfg.GetUnitIdByName("FARM");
    const int militia = cfg.GetUnitIdByName("SPRMN");
    Core::PlayerId them = 0;
    for (Core::PlayerId p = 1; p < Core::kMaxPlayers && them == 0; ++p) {
        if (p != kUs && ECS::Players::Find(reg, p) != nullptr && !ECS::Diplomacy::Allied(reg, kUs, p)) them = p;
    }
    entt::entity villager = entt::null;
    auto workers = reg.view<const C::Worker, const C::Owner>();
    for (auto e : workers) {
        if (villager == entt::null && workers.get<const C::Owner>(e).player_id == kUs) villager = e;
    }
    if (them == 0 || villager == entt::null) verdict = "no enemy or no villager";
    const auto home = ok() ? reg.get<C::Transform>(villager).position : C::Vector2D{};
    const int hx = home.x.ToInt(), hy = home.y.ToInt();

    // An enemy building put up where we have been and do not look: not drawn
    // for us at all.
    int bx = 0, by = 0;
    entt::entity built = entt::null;
    if (ok() && Unexplored(sim, hx, hy, 30, 3, bx, by)) {
        ECS::Vision::Explore(reg, kUs, bx, by, 6);
        built = ECS::Buildings::Place(reg, grid, house, them, bx, by, true);
        sim.Update(16);
        if (built == entt::null) verdict = "no building could be placed";
        else if (ECS::Vision::Shows(reg, kUs, built)) verdict = "a building put up out of sight was shown in the fog";
        else if (PictureOf(reg, kUs, built) != entt::null) verdict = "a building never seen left a picture";
    } else if (ok()) {
        verdict = "no unexplored ground for a building";
    }

    // Seen and then out of sight: a picture of it, ours alone, in its owner's
    // colour, in its place.
    if (ok()) {
        Look(sim, militia, bx, by);
        const entt::entity picture = PictureOf(reg, kUs, built);
        if (picture == entt::null) {
            verdict = "a building gone out of sight left no picture";
        } else if (ECS::Vision::Shows(reg, kUs, built)) {
            verdict = "a building out of sight was shown as itself";
        } else if (!ECS::Vision::Shows(reg, kUs, picture)) {
            verdict = "a picture was not shown to its player";
        } else if (ECS::Vision::Shows(reg, them, picture)) {
            verdict = "a picture was shown to another player";
        } else if (reg.get<C::Doppelganger>(picture).colour != them) {
            verdict = "a picture is not in its owner's colour";
        } else if (reg.get<C::Transform>(picture).position.x != reg.get<C::Transform>(built).position.x ||
                   reg.get<C::Transform>(picture).position.y != reg.get<C::Transform>(built).position.y) {
            verdict = "a picture is not where its building stands";
        }
    }

    // Destroyed out of sight: the picture stays until its ground is seen.
    if (ok()) {
        ECS::Buildings::Destroy(reg, grid, built);
        Run(sim, 2);
        const bool kept = PictureOf(reg, kUs, built) != entt::null;
        Look(sim, militia, bx, by);
        if (!kept) verdict = "a building destroyed out of sight took its picture with it";
        else if (PictureOf(reg, kUs, built) != entt::null) verdict = "a picture outlasted its ground being seen";
    }

    // A mine kept shown in the fog, used up out of sight: a picture of it.
    if (ok()) {
        entt::entity mine = entt::null;
        auto mines = reg.view<const C::Resource, const C::Transform, const C::Identity>();
        for (auto e : mines) {
            const auto& at = mines.get<const C::Transform>(e).position;
            if (mine == entt::null && mines.get<const C::Resource>(e).type == Core::ResourceType::Gold &&
                !ECS::Vision::Explored(reg, kUs, Physics::TileOf(at.x), Physics::TileOf(at.y))) {
                mine = e;
            }
        }
        if (mine == entt::null) {
            verdict = "no gold mine out of our sight";
        } else {
            const auto at = reg.get<C::Transform>(mine).position;
            const int mx = Physics::TileOf(at.x), my = Physics::TileOf(at.y);
            ECS::Vision::Explore(reg, kUs, mx, my, 3);
            const bool shown = ECS::Vision::Shows(reg, kUs, mine);
            ECS::Resources::Exhaust(reg, grid, mine);
            sim.Update(16);
            const entt::entity picture = PictureOf(reg, kUs, mine);
            const bool drawn = picture != entt::null && ECS::Vision::Shows(reg, kUs, picture);
            Look(sim, militia, mx, my);
            if (!shown) verdict = "a mine found was not shown in the fog";
            else if (!drawn) verdict = "a mine used up out of sight left no picture";
            else if (PictureOf(reg, kUs, mine) != entt::null) verdict = "a mine's picture outlasted its ground being seen";
        }
    }

    // The ground: a farm laid out of sight is, for us, the ground it was until
    // we look again; for its owner, the farm's.
    if (ok()) {
        int fx = 0, fy = 0;
        if (!Unexplored(sim, hx, hy, 30, 3, fx, fy)) {
            verdict = "no unexplored ground for a farm";
        } else {
            ECS::Vision::Explore(reg, kUs, fx, fy, 6);
            const Core::TerrainType was = grid.GetTile(fx, fy).terrain;
            ECS::Buildings::Place(reg, grid, farm, them, fx, fy, true);
            sim.Update(16);
            const Core::TerrainType now = grid.GetTile(fx, fy).terrain;
            const Core::TerrainType ours = ECS::Fog::GroundSeen(reg, kUs, fx, fy, now);
            const Core::TerrainType theirs = ECS::Fog::GroundSeen(reg, them, fx, fy, now);
            Look(sim, militia, fx, fy);
            if (now == was) verdict = "a farm laid no ground";
            else if (ours != was) verdict = "ground changed out of sight was not remembered as it was";
            else if (theirs != now) verdict = "a farm's owner does not see its ground";
            else if (ECS::Fog::GroundSeen(reg, kUs, fx, fy, now) != now) verdict = "ground seen again is still remembered";
        }
    }

    std::printf("fog=%s\n", verdict.c_str());
    return verdict == "ok";
}

} // namespace Verify
