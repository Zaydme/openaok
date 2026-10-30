#include "ecs/Monks.h"
#include "ecs/Navigation.h"

#include <vector>

#include "core/ConfigManager.h"
#include "core/FixedMath.h"
#include "core/Random.h"
#include "core/Rules.h"
#include "ecs/Allegiance.h"
#include "ecs/Combat.h"
#include "ecs/Diplomacy.h"
#include "ecs/Events.h"
#include "ecs/Players.h"
#include "ecs/Score.h"
#include "ecs/components/Building.h"
#include "ecs/components/Health.h"
#include "ecs/components/Identity.h"
#include "ecs/components/Monk.h"
#include "ecs/components/Relic.h"
#include "ecs/components/Owner.h"
#include "ecs/components/Path.h"
#include "ecs/components/Transform.h"
#include "pathfinding/Pathfinder.h"

namespace ECS::Monks {

namespace {

namespace R = Core::Rules;

// The civ table's own entries.
constexpr int kMonkConversion = 27;       // monks may convert monks (Atonement)
constexpr int kBuildingConversion = 28;   // and buildings (Redemption)
constexpr int kFaithRecharge = 35;        // faith a second

// Figures the dat does not carry: faith's ceiling; the longest a conversion
// takes, and its chance each second past the shortest.
const Core::Fixed32 kFullFaith(100);
constexpr int32_t kLongestConversionMs = 10000;
constexpr int kChancePercent = 28;
constexpr int32_t kRollMs = 1000;

// The dat's class for a monk.
constexpr int kMonkClass = 18;

// What a monk never converts, and what only Redemption lets it convert:
// buildings other than Town Centers, Castles, Monasteries, Farms, Fish Traps,
// walls, towers, Gates and Wonders, and siege weapons. The dat has no flag
// for either: they are listed by class, and by unit for the four that share
// the building class.
constexpr int kNeverConvertedClasses[] = {
    27,   // walls
    39,   // gates
    52,   // towers
    49,   // farms and fish traps
};
constexpr const char* kNeverConvertedUnits[] = {"RTWC", "CSTL", "CRCH", "WNDR"};
constexpr int kSiegeClasses[] = {
    13,   // siege weapons: rams, mangonels, the bombard cannon
    51,   // packed siege: the trebuchet packed
    54,   // the trebuchet set up
    55,   // scorpions
};

template <typename List>
bool Listed(const List& list, int cls) {
    for (int c : list) {
        if (c == cls) return true;
    }
    return false;
}

// Whether monks never convert it, Redemption or not.
bool NeverConverted(const entt::registry& registry, entt::entity target, int cls) {
    if (Listed(kNeverConvertedClasses, cls)) return true;
    const auto* identity = registry.try_get<Components::Identity>(target);
    if (identity == nullptr) return false;
    auto& cfg = Core::ConfigManager::Get();
    for (const char* name : kNeverConvertedUnits) {
        if (cfg.GetUnitIdByName(name) == identity->unit_id) return true;
    }
    return false;
}

constexpr int32_t kChaseReplanMs = 500;
constexpr int32_t kHealScanMs = 1000;

Core::PlayerId OwnerOf(const entt::registry& registry, entt::entity e) {
    const auto* owner = registry.try_get<Components::Owner>(e);
    return owner != nullptr ? owner->player_id : Core::PLAYER_GAIA;
}

const R::UnitRules* RulesOf(const entt::registry& registry, entt::entity e) {
    const auto* identity = registry.try_get<Components::Identity>(e);
    if (identity == nullptr) return nullptr;
    return Players::RulesFor(registry, e, identity->unit_id);
}

const R::Task* TaskOf(const R::UnitRules& rules, int action) {
    for (int t = 0; t < rules.task_count; ++t) {
        if (rules.tasks[t].action == action) return &rules.tasks[t];
    }
    return nullptr;
}

constexpr int kConvert = 104;
constexpr int kHeal = 105;

Core::Fixed32 Resource(const entt::registry& registry, Core::PlayerId player, int id) {
    const Players::Player* seat = Players::Find(registry, player);
    return seat != nullptr ? Players::Resource(*seat, id) : Core::Fixed32(0);
}

Core::Fixed32 Gap(const entt::registry& registry, entt::entity a, entt::entity b) {
    return Combat::Gap(registry, a, b);
}

void Chase(entt::registry& registry, const Physics::Grid& grid, entt::entity monk,
           entt::entity target) {
    const auto& from = registry.get<Components::Transform>(monk).position;
    const auto& to = registry.get<Components::Transform>(target).position;
    auto path = Navigation::Route(registry, grid, monk, from, to);
    if (!path.waypoints.empty()) registry.emplace_or_replace<Components::Path>(monk, std::move(path));
}

bool Wounded(const entt::registry& registry, entt::entity e) {
    const auto* health = registry.try_get<Components::Health>(e);
    return health != nullptr && health->current_hp < health->max_hp;
}

// Whether `monk` may heal `target`: one of its own side's units, wounded.
bool CanHeal(const entt::registry& registry, entt::entity monk, entt::entity target) {
    if (monk == target || !Combat::Alive(registry, target)) return false;
    if (registry.all_of<Components::Building>(target)) return false;
    // Its own side's: its owner's, or an ally's.
    if (Diplomacy::StanceOf(registry, OwnerOf(registry, monk), OwnerOf(registry, target)) !=
        Diplomacy::Stance::Ally) {
        return false;
    }
    const R::UnitRules* rules = RulesOf(registry, monk);
    return rules != nullptr && TaskOf(*rules, kHeal) != nullptr && Wounded(registry, target);
}

} // namespace

bool CanConvert(const entt::registry& registry, entt::entity monk, entt::entity target) {
    if (monk == target || !Combat::Alive(registry, monk) || !Combat::Alive(registry, target)) {
        return false;
    }
    const R::UnitRules* rules = RulesOf(registry, monk);
    if (rules == nullptr || TaskOf(*rules, kConvert) == nullptr) return false;
    const Core::PlayerId player = OwnerOf(registry, monk);
    const Core::PlayerId theirs = OwnerOf(registry, target);
    if (theirs == player || theirs == Core::PLAYER_GAIA) return false;
    // Never an ally's.
    if (Diplomacy::StanceOf(registry, player, theirs) == Diplomacy::Stance::Ally) return false;
    const R::UnitRules* target_rules = RulesOf(registry, target);
    const int cls = target_rules != nullptr ? target_rules->cls : -1;
    if (NeverConverted(registry, target, cls)) return false;
    // Buildings and siege weapons only with Redemption.
    if ((registry.all_of<Components::Building>(target) || Listed(kSiegeClasses, cls)) &&
        Resource(registry, player, kBuildingConversion).value <= 0) {
        return false;
    }
    if (target_rules != nullptr && target_rules->cls == kMonkClass &&
        Resource(registry, player, kMonkConversion).value <= 0) {
        return false;
    }
    return true;
}

bool Convert(entt::registry& registry, entt::entity monk, entt::entity target) {
    if (!CanConvert(registry, monk, target)) return false;
    auto& state = registry.get_or_emplace<Components::Monk>(monk);
    state.target = target;
    state.converting = true;
    state.ordered = true;
    state.elapsed_ms = 0;
    state.chase_ms = 0;
    state.roll_ms = 0;
    registry.remove<Components::Path>(monk);
    return true;
}

bool Heal(entt::registry& registry, entt::entity monk, entt::entity target) {
    if (!CanHeal(registry, monk, target)) return false;
    auto& state = registry.get_or_emplace<Components::Monk>(monk);
    state.target = target;
    state.converting = false;
    state.ordered = true;
    state.chase_ms = 0;
    registry.remove<Components::Path>(monk);
    return true;
}

void Update(entt::registry& registry, const Physics::Grid& grid, int32_t dt_ms) {
    auto& rng = registry.ctx().get<Core::Random>();
    std::vector<std::pair<entt::entity, Core::PlayerId>> converted;
    std::vector<entt::entity> began;

    auto monks = registry.view<Components::Monk, Components::Transform>();
    for (auto monk : monks) {
        auto& state = monks.get<Components::Monk>(monk);
        state.chanting = false;
        if (!Combat::Alive(registry, monk)) continue;
        const R::UnitRules* rules = RulesOf(registry, monk);
        if (rules == nullptr) continue;
        const Core::PlayerId player = OwnerOf(registry, monk);

        // Faith comes back at the civ's rate.
        if (state.faith < kFullFaith) {
            state.faith = state.faith + Resource(registry, player, kFaithRecharge) *
                                            Core::Fixed32(dt_ms) / Core::Fixed32(1000);
            if (state.faith > kFullFaith) state.faith = kFullFaith;
        }

        // An idle healer looks about it for its own wounded: not one walking,
        // fetching a relic or carrying one.
        if (state.target == entt::null) {
            const R::Task* heal = TaskOf(*rules, kHeal);
            const auto* path = registry.try_get<Components::Path>(monk);
            const bool walking = path != nullptr &&
                                 path->current_waypoint_index < path->waypoints.size();
            const bool errand = registry.any_of<Components::RelicErrand, Components::RelicBearer>(monk);
            if (heal == nullptr || walking || errand) continue;
            state.scan_ms -= dt_ms;
            if (state.scan_ms > 0) continue;
            state.scan_ms = kHealScanMs;
            entt::entity best = entt::null;
            Core::Fixed32 best_gap{};
            auto bodies = registry.view<Components::Health, Components::Transform,
                                        Components::Owner>();
            for (auto other : bodies) {
                if (!CanHeal(registry, monk, other)) continue;
                const Core::Fixed32 gap = Gap(registry, monk, other);
                if (gap > rules->search_radius) continue;
                if (best == entt::null || gap < best_gap || (gap == best_gap && other < best)) {
                    best = other;
                    best_gap = gap;
                }
            }
            if (best != entt::null) {
                state.target = best;
                state.converting = false;
                state.ordered = false;
                state.chase_ms = 0;
            }
            continue;
        }

        // The work in hand: still worth doing?
        const entt::entity target = state.target;
        const bool valid = state.converting
            ? (Combat::Alive(registry, target) && OwnerOf(registry, target) != player &&
               OwnerOf(registry, target) != Core::PLAYER_GAIA)
            : CanHeal(registry, monk, target);
        if (!valid) {
            state.target = entt::null;
            state.elapsed_ms = 0;
            continue;
        }

        const Core::Fixed32 gap = Gap(registry, monk, target);
        if (gap > rules->max_range) {
            state.chase_ms -= dt_ms;
            const auto* path = registry.try_get<Components::Path>(monk);
            const bool walking = path != nullptr &&
                                 path->current_waypoint_index < path->waypoints.size();
            if (state.chase_ms <= 0 || !walking) {
                state.chase_ms = kChaseReplanMs;
                Chase(registry, grid, monk, target);
            }
            continue;
        }
        registry.remove<Components::Path>(monk);
        // At work, it looks at whom it works on, following it as it moves.
        Combat::Face(registry, monk, target);

        if (!state.converting) {
            // Healing: the Heal task's hit points a second.
            const R::Task* heal = TaskOf(*rules, kHeal);
            if (heal == nullptr) {
                state.target = entt::null;
                continue;
            }
            state.chanting = true;
            auto& health = registry.get<Components::Health>(target);
            health.wound = health.wound - heal->work_value * Core::Fixed32(dt_ms) /
                                              Core::Fixed32(1000);
            while (health.wound.value < 0 && health.current_hp < health.max_hp) {
                health.wound = health.wound + Core::Fixed32(1);
                ++health.current_hp;
            }
            if (health.current_hp >= health.max_hp) {
                health.wound = Core::Fixed32(0);
                state.target = entt::null;
            }
            continue;
        }

        // Converting: only with full faith; each second past the shortest a
        // chance of taking, and certain at the longest.
        if (state.faith < kFullFaith) {
            if (!state.ordered) state.target = entt::null;
            continue;
        }
        const R::Task* convert = TaskOf(*rules, kConvert);
        const int32_t shortest = convert != nullptr
            ? (convert->work_value * Core::Fixed32(1000)).ToInt() : 4000;
        if (state.elapsed_ms == 0) began.push_back(monk);
        state.chanting = true;
        state.elapsed_ms += dt_ms;
        if (state.elapsed_ms < shortest) continue;
        state.roll_ms -= dt_ms;
        if (state.roll_ms > 0 && state.elapsed_ms < kLongestConversionMs) continue;
        state.roll_ms = kRollMs;
        const bool takes = state.elapsed_ms >= kLongestConversionMs ||
                           static_cast<int>(rng.NextInt(100)) < kChancePercent;
        if (!takes) continue;
        converted.emplace_back(target, player);
        state.faith = Core::Fixed32(0);
        state.target = entt::null;
        state.elapsed_ms = 0;
    }

    for (entt::entity monk : began) {
        const auto& state = registry.get<Components::Monk>(monk);
        const auto* at = registry.try_get<Components::Transform>(state.target);
        Events::Emit(registry, {.kind = Events::Kind::Converting, .subject = state.target,
                                .other = monk, .player = OwnerOf(registry, state.target),
                                .at = at != nullptr ? at->position : Components::Vector2D{}});
    }
    for (const auto& [target, player] : converted) {
        if (!Combat::Alive(registry, target)) continue;
        // Counted while the target is still its old owner's (ecs/Score).
        Score::Converted(registry, target, player);
        Allegiance::Change(registry, grid, target, player);
    }
}

} // namespace ECS::Monks
