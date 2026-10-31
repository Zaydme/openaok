#include "ecs/Animals.h"
#include "ecs/Nearby.h"
#include "ecs/Navigation.h"

#include <vector>

#include "core/FixedMath.h"
#include "core/Random.h"
#include "core/Rules.h"
#include "ecs/Combat.h"
#include "ecs/Events.h"
#include "ecs/Players.h"
#include "ecs/components/Building.h"
#include "ecs/components/Combat.h"
#include "ecs/components/Grazer.h"
#include "ecs/components/Health.h"
#include "ecs/components/Identity.h"
#include "ecs/components/Mortal.h"
#include "ecs/components/Owner.h"
#include "ecs/components/Path.h"
#include "ecs/components/Transform.h"
#include "ecs/components/Worker.h"
#include "pathfinding/Pathfinder.h"

namespace ECS::Animals {

namespace {

namespace R = Core::Rules;

// How long a grazer stands between steps, and how far one step and its
// wandering reach.
constexpr int32_t kLeastWaitMs = 4000;
constexpr int32_t kWaitSpreadMs = 8000;
const Core::Fixed32 kStep = Core::Fixed32(3) / Core::Fixed32(4);
const Core::Fixed32 kWanderReach = Core::Fixed32(2);

// How far prey runs from a blow, as a multiple of its own sight, and for how
// long.
constexpr int kFleeSights = 3;
constexpr int32_t kFleeMs = 6000;

// How often herdables look about them, staggered by entity.
constexpr int32_t kHerdCheckTicks = 8;

const R::UnitRules* RulesOf(const entt::registry& registry, entt::entity entity) {
    const auto* identity = registry.try_get<Components::Identity>(entity);
    if (identity == nullptr) return nullptr;
    return Players::RulesFor(registry, entity, identity->unit_id);
}

bool HasTask(const R::UnitRules& rules, int action) {
    for (int t = 0; t < rules.task_count; ++t) {
        if (rules.tasks[t].action == action) return true;
    }
    return false;
}

// By unit id, whether that unit can ever graze: its own rules have a Graze
// task, or an upgrade can make its rules those of one that does, however
// many upgrades away. A player's rules are the dat's, changed only by effects,
// and only an upgrade changes a unit's tasks.
const std::vector<bool>& MayGraze() {
    static const std::vector<bool> may = [] {
        const R::Tables& tables = R::Get();
        std::vector<bool> out(tables.units.size(), false);
        for (size_t id = 0; id < tables.units.size(); ++id) {
            out[id] = tables.units[id].valid && HasTask(tables.units[id], R::TaskAction::Graze);
        }
        for (bool changed = true; changed;) {
            changed = false;
            for (const R::Effect& effect : tables.effects) {
                for (const R::EffectCommand& c : effect.commands) {
                    if (c.type != R::EffectType::UpgradeUnit || c.a < 0 || c.b < 0) continue;
                    if (static_cast<size_t>(c.a) >= out.size() || static_cast<size_t>(c.b) >= out.size()) continue;
                    if (out[static_cast<size_t>(c.b)] && !out[static_cast<size_t>(c.a)]) {
                        out[static_cast<size_t>(c.a)] = true;
                        changed = true;
                    }
                }
            }
        }
        return out;
    }();
    return may;
}

// Something that can claim a herdable: a living unit of a player's, out in
// the world -- not a building, not a body, not another herdable.
bool Claims(const entt::registry& registry, entt::entity entity) {
    if (registry.all_of<Components::Building>(entity)) return false;
    if (!Combat::Alive(registry, entity)) return false;
    if (!registry.any_of<Components::Combat, Components::Worker>(entity)) return false;
    return !Herdable(registry, entity);
}

bool Busy(const entt::registry& registry, entt::entity animal) {
    const auto* path = registry.try_get<Components::Path>(animal);
    if (path != nullptr && path->current_waypoint_index < path->waypoints.size()) return true;
    if (const auto* combat = registry.try_get<Components::Combat>(animal)) {
        if (combat->state != Components::CombatState::Idle) return true;
    }
    return registry.all_of<Components::Dying>(animal);
}

void WalkTo(entt::registry& registry, const Physics::Grid& grid, entt::entity animal,
            const Components::Vector2D& to) {
    const auto& at = registry.get<Components::Transform>(animal).position;
    if (!Navigation::CanStand(registry, grid, animal, to)) return;
    auto path = Navigation::Route(registry, grid, animal, at, to);
    if (!path.waypoints.empty()) registry.emplace_or_replace<Components::Path>(animal, std::move(path));
}

// Each herdable checks who is about: its owner's units keep it; failing
// those, the nearest unit of another player's within its sight takes it.
void Herd(entt::registry& registry, const Physics::Grid& grid, uint64_t tick) {
    std::vector<std::pair<entt::entity, Core::PlayerId>> changes;
    auto herd = registry.view<Components::Grazer, Components::Transform, Components::Owner>();
    auto units = registry.view<const Components::Transform, Components::Owner>();
    static Nearby::Buckets near;
    bool built = false;
    for (auto animal : herd) {
        if ((tick + entt::to_integral(animal)) % kHerdCheckTicks != 0) continue;
        if (!Herdable(registry, animal) || !Combat::Alive(registry, animal)) continue;
        const R::UnitRules* rules = RulesOf(registry, animal);
        if (rules == nullptr) continue;
        const auto& at = herd.get<Components::Transform>(animal).position;
        const Core::PlayerId owner = herd.get<Components::Owner>(animal).player_id;

        if (!built) {
            // Only another player's unit, never a building, claims one.
            const auto& buildings = registry.storage<Components::Building>();
            Nearby::Build(near, units, grid.GetWidth(), grid.GetHeight(), [&](entt::entity e) {
                return units.get<Components::Owner>(e).player_id != Core::PLAYER_GAIA && !buildings.contains(e);
            });
            built = true;
        }
        bool kept = false;
        entt::entity nearest = entt::null;
        Core::Fixed32 nearest_d{};
        Nearby::ForEachNear(near, at, rules->los, [&](entt::entity unit) {
            if (kept) return;
            const Core::PlayerId player = units.get<Components::Owner>(unit).player_id;
            if (player == Core::PLAYER_GAIA) return;
            // Out of sight on either axis is out of sight: the cheap test first.
            const auto& p = units.get<const Components::Transform>(unit).position;
            if (Core::FixedMath::Abs(p.x - at.x) > rules->los || Core::FixedMath::Abs(p.y - at.y) > rules->los) {
                return;
            }
            const Core::Fixed32 d = Core::FixedMath::Distance(p.x - at.x, p.y - at.y);
            if (d > rules->los || !Claims(registry, unit)) return;
            if (player == owner) {
                kept = true;
                return;
            }
            if (nearest == entt::null || d < nearest_d ||
                (d == nearest_d && unit < nearest)) {
                nearest = unit;
                nearest_d = d;
            }
        });
        if (!kept && nearest != entt::null) {
            changes.emplace_back(animal, units.get<Components::Owner>(nearest).player_id);
        }
    }
    for (const auto& [animal, player] : changes) {
        auto& owner = registry.get<Components::Owner>(animal);
        const Core::PlayerId was = owner.player_id;
        owner.player_id = player;
        // A sheep changing hands stops wherever it was being led.
        registry.remove<Components::Path>(animal);
        Events::Emit(registry, {.kind = Events::Kind::Claimed, .subject = animal, .player = player,
                                .id = was, .at = registry.get<Components::Transform>(animal).position});
    }
}

} // namespace

bool Herdable(const entt::registry& registry, entt::entity entity) {
    const R::UnitRules* rules = RulesOf(registry, entity);
    return rules != nullptr && HasTask(*rules, R::TaskAction::AutoConvert);
}

bool Prey(const entt::registry& registry, entt::entity entity) {
    const R::UnitRules* rules = RulesOf(registry, entity);
    return rules != nullptr && rules->attack_count == 0 && HasTask(*rules, R::TaskAction::Graze) &&
           !HasTask(*rules, R::TaskAction::AutoConvert);
}

void Flee(entt::registry& registry, const Physics::Grid& grid, entt::entity animal,
          entt::entity attacker) {
    if (!Prey(registry, animal) || !Combat::Alive(registry, animal)) return;
    if (!registry.all_of<Components::Transform>(attacker)) return;
    const R::UnitRules* rules = RulesOf(registry, animal);
    const auto& at = registry.get<Components::Transform>(animal).position;
    const auto& from = registry.get<Components::Transform>(attacker).position;
    Core::Fixed32 dx = at.x - from.x;
    Core::Fixed32 dy = at.y - from.y;
    const Core::Fixed32 length = Core::FixedMath::Distance(dx, dy);
    if (length.value <= 0) {
        dx = Core::Fixed32(1);
        dy = Core::Fixed32(0);
    } else {
        dx = dx / length;
        dy = dy / length;
    }
    const Core::Fixed32 run = rules->los * Core::Fixed32(kFleeSights);
    WalkTo(registry, grid, animal, {at.x + dx * run, at.y + dy * run});
    auto& grazer = registry.get_or_emplace<Components::Grazer>(animal, Components::Grazer{at});
    grazer.flee_ms = kFleeMs;
    // Where it fled to is home now.
    grazer.home = {at.x + dx * run, at.y + dy * run};
}

namespace {

// Whether a tick must look for new grazers: something has been given an
// identity or a place, or changed its identity, since the last look. Grazing is only ever taken
// up, so between such changes there is nothing new to find.
struct Unsurveyed {
    bool look = true;
};

void MarkUnsurveyed(entt::registry& registry, entt::entity) {
    registry.ctx().get<Unsurveyed>().look = true;
}

} // namespace

void Changed(entt::registry& registry) {
    if (auto* unsurveyed = registry.ctx().find<Unsurveyed>()) unsurveyed->look = true;
}

namespace {

// Every grazer gets its place on first sight: those of the things given an
// identity or a place, or a new identity, since the last look.
void TakeUpGrazing(entt::registry& registry) {
    if (!registry.ctx().contains<Unsurveyed>()) {
        registry.ctx().emplace<Unsurveyed>();
        registry.on_construct<Components::Identity>().connect<&MarkUnsurveyed>();
        registry.on_construct<Components::Transform>().connect<&MarkUnsurveyed>();
    }
    auto& unsurveyed = registry.ctx().get<Unsurveyed>();
    if (!unsurveyed.look) return;
    unsurveyed.look = false;
    static std::vector<entt::entity> fresh;
    fresh.clear();
    auto animals = registry.view<Components::Identity, Components::Transform>(
        entt::exclude<Components::Grazer, Components::Building>);
    const std::vector<bool>& may_graze = MayGraze();
    for (auto e : animals) {
        const int id = animals.get<Components::Identity>(e).unit_id;
        if (id < 0 || static_cast<size_t>(id) >= may_graze.size() || !may_graze[static_cast<size_t>(id)]) continue;
        const R::UnitRules* rules = RulesOf(registry, e);
        if (rules != nullptr && HasTask(*rules, R::TaskAction::Graze)) fresh.push_back(e);
    }
    auto& rng = registry.ctx().get<Core::Random>();
    for (auto e : fresh) {
        Components::Grazer grazer{registry.get<Components::Transform>(e).position};
        grazer.wait_ms = kLeastWaitMs + static_cast<int32_t>(rng.NextInt(kWaitSpreadMs));
        registry.emplace<Components::Grazer>(e, grazer);
    }
}

} // namespace

void Update(entt::registry& registry, const Physics::Grid& grid, int32_t dt_ms) {
    TakeUpGrazing(registry);
    auto& rng = registry.ctx().get<Core::Random>();
    if (const Events::Log* log = Events::Read(registry)) Herd(registry, grid, log->tick);

    auto grazers = registry.view<Components::Grazer, Components::Transform>();
    std::vector<std::pair<entt::entity, Components::Vector2D>> steps;
    for (auto animal : grazers) {
        auto& grazer = grazers.get<Components::Grazer>(animal);
        if (grazer.flee_ms > 0) {
            grazer.flee_ms -= dt_ms;
            continue;
        }
        if (Busy(registry, animal)) continue;
        // Only the wild wander: a sheep a player has taken stands where it
        // is put.
        if (registry.all_of<Components::Owner>(animal) &&
            registry.get<Components::Owner>(animal).player_id != Core::PLAYER_GAIA) {
            continue;
        }
        grazer.wait_ms -= dt_ms;
        if (grazer.wait_ms > 0) continue;
        grazer.wait_ms = kLeastWaitMs + static_cast<int32_t>(rng.NextInt(kWaitSpreadMs));
        const auto& at = grazers.get<Components::Transform>(animal).position;
        // A step in one of sixteen directions, drawn back toward home if it
        // has strayed.
        const int direction = static_cast<int>(rng.NextInt(16));
        static constexpr int kCos[16] = {1024, 946, 724, 392, 0, -392, -724, -946,
                                         -1024, -946, -724, -392, 0, 392, 724, 946};
        static constexpr int kSin[16] = {0, 392, 724, 946, 1024, 946, 724, 392,
                                         0, -392, -724, -946, -1024, -946, -724, -392};
        Components::Vector2D to{at.x + kStep * Core::Fixed32(kCos[direction]) / Core::Fixed32(1024),
                                at.y + kStep * Core::Fixed32(kSin[direction]) / Core::Fixed32(1024)};
        if (Core::FixedMath::Distance(to.x - grazer.home.x, to.y - grazer.home.y) > kWanderReach) {
            to = {at.x + (grazer.home.x - at.x) / Core::Fixed32(2),
                  at.y + (grazer.home.y - at.y) / Core::Fixed32(2)};
        }
        steps.emplace_back(animal, to);
    }
    for (const auto& [animal, to] : steps) WalkTo(registry, grid, animal, to);
}

} // namespace ECS::Animals
