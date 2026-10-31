#include "ai/Relics.h"

#include <algorithm>
#include <utility>
#include <vector>

#include "ai/Brain.h"
#include "ai/Census.h"
#include "ai/Facts.h"
#include "ai/Language.h"
#include "ai/Military.h"
#include "core/ConfigManager.h"
#include "core/FixedMath.h"
#include "ecs/Relics.h"
#include "ecs/Vision.h"
#include "ecs/components/Identity.h"
#include "ecs/components/Monk.h"
#include "ecs/components/Owner.h"
#include "ecs/components/Path.h"
#include "ecs/components/Relic.h"
#include "ecs/components/Transform.h"

namespace AI::Relics {

namespace {

namespace C = ECS::Components;

// The lobby's lowest difficulty (Core::GameSetup::difficulty).
constexpr int kEasiest = 4;

int RelicUnit() {
    static const int relic = Core::ConfigManager::Get().GetUnitIdByName("RELIC");
    return relic;
}

int MonasteryUnit() {
    static const int thing = Lang::FindThing("monastery");
    return thing >= 0 ? Lang::Things()[thing].ids[0] : -1;
}

bool Walking(const entt::registry& registry, entt::entity e) {
    const auto* path = registry.try_get<C::Path>(e);
    return path != nullptr && path->current_waypoint_index < path->waypoints.size();
}

Core::Fixed32 Apart(const C::Vector2D& a, const C::Vector2D& b) {
    return Core::FixedMath::Distance(a.x - b.x, a.y - b.y);
}

// A carrier with nowhere to go yet: to the nearest of the player's finished
// buildings that takes relics.
void TakeHome(Turn& turn, entt::entity bearer) {
    entt::registry& registry = *turn.registry;
    const C::Vector2D& at = registry.get<C::Transform>(bearer).position;
    entt::entity best = entt::null;
    Core::Fixed32 best_at{};
    for (const Census::Site& site : turn.census->buildings) {
        if (!site.complete || !ECS::Relics::CanDeposit(registry, bearer, site.entity)) continue;
        const Core::Fixed32 d = Apart(registry.get<C::Transform>(site.entity).position, at);
        if (best == entt::null || d < best_at) {
            best = site.entity;
            best_at = d;
        }
    }
    if (best != entt::null) Give(registry, *turn.grid, Core::RelicCommand{bearer, best, turn.player});
}

} // namespace

void Run(Turn& turn) {
    if (turn.registry == nullptr || turn.census == nullptr) return;
    if (turn.setup != nullptr && turn.setup->difficulty == kEasiest) return;
    entt::registry& registry = *turn.registry;
    Census& census = *turn.census;
    // Who is on an errand already, and the relics they are after.
    std::vector<entt::entity> busy, sought;
    auto errands = registry.view<const C::RelicErrand, const C::Owner>();
    for (auto e : errands) {
        if (errands.get<const C::Owner>(e).player_id != turn.player) continue;
        busy.push_back(e);
        const entt::entity relic = errands.get<const C::RelicErrand>(e).relic;
        if (relic != entt::null) sought.push_back(relic);
    }
    auto bearers = registry.view<const C::RelicBearer, const C::Owner, const C::Transform>();
    for (auto e : bearers) {
        if (bearers.get<const C::Owner>(e).player_id != turn.player) continue;
        busy.push_back(e);
        if (!registry.all_of<C::RelicErrand>(e) && !Walking(registry, e)) TakeHome(turn, e);
    }
    // Relics lying where the player has explored, nearest home first, each
    // to the nearest idle Monk who can fetch it.
    if (census.Count(MonasteryUnit(), true) > 0) {
        std::vector<std::pair<Core::Fixed32, entt::entity>> relics;
        auto view = registry.view<const C::Identity, const C::Transform>();
        for (auto e : view) {
            if (view.get<const C::Identity>(e).unit_id != RelicUnit()) continue;
            if (std::find(sought.begin(), sought.end(), e) != sought.end()) continue;
            const C::Vector2D& at = view.get<const C::Transform>(e).position;
            if (!ECS::Vision::Explored(registry, turn.player, Physics::TileOf(at.x), Physics::TileOf(at.y))) continue;
            relics.emplace_back(Apart(at, census.home), e);
        }
        std::sort(relics.begin(), relics.end());
        std::vector<entt::entity> monks;
        for (entt::entity e : census.idle_military) {
            if (registry.all_of<C::Monk>(e) && !Military::Attacking(turn, e)) monks.push_back(e);
        }
        for (const auto& [far, relic] : relics) {
            const C::Vector2D& at = registry.get<C::Transform>(relic).position;
            auto pick = monks.end();
            Core::Fixed32 pick_at{};
            for (auto it = monks.begin(); it != monks.end(); ++it) {
                if (!ECS::Relics::CanFetch(registry, *it, relic)) continue;
                const Core::Fixed32 d = Apart(registry.get<C::Transform>(*it).position, at);
                if (pick == monks.end() || d < pick_at) {
                    pick = it;
                    pick_at = d;
                }
            }
            if (pick == monks.end()) continue;
            Give(registry, *turn.grid, Core::RelicCommand{*pick, relic, turn.player});
            busy.push_back(*pick);
            monks.erase(pick);
        }
    }
    // Out of the army while on their errands.
    for (entt::entity e : busy) {
        std::erase(census.military, e);
        std::erase(census.idle_military, e);
    }
}

bool EnemyCaptured(const Turn& turn) {
    if (turn.registry == nullptr) return false;
    const entt::registry& registry = *turn.registry;
    int total = 0;
    auto all = registry.view<const C::Identity>();
    for (auto e : all) total += all.get<const C::Identity>(e).unit_id == RelicUnit() ? 1 : 0;
    if (total == 0) return false;
    int held = 0;
    auto shrines = registry.view<const C::RelicShrine, const C::Owner>();
    for (auto s : shrines) {
        const Core::PlayerId p = shrines.get<const C::Owner>(s).player_id;
        if (p == turn.player || Facts::StanceToward(turn, p) != Lang::Stance::Enemy) continue;
        held += static_cast<int>(shrines.get<const C::RelicShrine>(s).relics.size());
    }
    return held == total;
}

} // namespace AI::Relics
