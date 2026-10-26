#include "ecs/Missiles.h"

#include <algorithm>
#include <vector>

#include "core/ConfigManager.h"
#include "core/FixedMath.h"
#include "core/IsometricMath.h"
#include "core/Random.h"
#include "ecs/BuildingGeometry.h"
#include "ecs/Combat.h"
#include "ecs/Garrisons.h"
#include "ecs/Players.h"
#include "ecs/Resources.h"
#include "ecs/components/Building.h"
#include "ecs/components/Health.h"
#include "ecs/components/Identity.h"
#include "ecs/components/Owner.h"
#include "ecs/components/Projectile.h"
#include "ecs/components/Resource.h"
#include "ecs/components/Sprite.h"
#include "ecs/components/Transform.h"

namespace ECS::Missiles {

namespace {

namespace R = Core::Rules;

// A missile reaching the ground within this of a unit's body hits it.
const Core::Fixed32 kHitSlack = Core::Fixed32(25) / Core::Fixed32(100);


// A missile with no arc of its own in the dat still climbs a little, so it
// reads as thrown rather than sliding along the ground.
const Core::Fixed32 kLeastArc = Core::Fixed32(1) / Core::Fixed32(10);

// Sixteen directions, as fixed-point unit vectors scaled by 1024, for placing
// a miss without trigonometry inside the tick.
constexpr int kCos[16] = {1024, 946, 724, 392, 0, -392, -724, -946,
                          -1024, -946, -724, -392, 0, 392, 724, 946};
constexpr int kSin[16] = {0, 392, 724, 946, 1024, 946, 724, 392,
                          0, -392, -724, -946, -1024, -946, -724, -392};

Core::PlayerId OwnerOf(const entt::registry& registry, entt::entity e) {
    const auto* owner = registry.try_get<Components::Owner>(e);
    return owner != nullptr ? owner->player_id : Core::PLAYER_GAIA;
}

void LooseOne(entt::registry& registry, entt::entity shooter, entt::entity target,
              const R::UnitRules& rules, int missile_id, const R::UnitRules& missile) {
    const Core::PlayerId owner = OwnerOf(registry, shooter);
    const auto& from = registry.get<Components::Transform>(shooter).position;
    Components::Vector2D to = registry.get<Components::Transform>(target).position;

    // The accuracy roll, from the simulation's own generator. A miss lands
    // off the target by up to its distance times the shooter's dispersion, in
    // any direction -- so a near miss at short range can still find its mark.
    auto& rng = registry.ctx().get<Core::Random>();
    const bool on_target = static_cast<int>(rng.NextInt(100)) < rules.accuracy;
    if (!on_target) {
        const int direction = static_cast<int>(rng.NextInt(16));
        const Core::Fixed32 distance =
            Core::FixedMath::Distance(to.x - from.x, to.y - from.y);
        const Core::Fixed32 off = distance * rules.dispersion *
            Core::Fixed32(static_cast<int>(rng.NextInt(101))) / Core::Fixed32(100);
        to.x = to.x + off * Core::Fixed32(kCos[direction]) / Core::Fixed32(1024);
        to.y = to.y + off * Core::Fixed32(kSin[direction]) / Core::Fixed32(1024);
    }

    Components::Projectile flight;
    flight.shooter = shooter;
    flight.target = target;
    flight.owner = owner;
    flight.unit_id = missile_id;
    flight.from = from;
    flight.to = to;
    flight.length = Core::FixedMath::Distance(to.x - from.x, to.y - from.y);
    flight.speed = missile.speed.value > 0 ? missile.speed : Core::Fixed32(5);
    const Core::Fixed32 arc = Core::FixedMath::Abs(missile.missile.arc);
    flight.arc = arc < kLeastArc ? kLeastArc : arc;
    flight.on_target = on_target;
    std::copy(std::begin(rules.attacks), std::end(rules.attacks), std::begin(flight.attacks));
    flight.attack_count = rules.attack_count;
    flight.blast_width = rules.blast_width;
    flight.blast_level = rules.blast_level;

    const entt::entity arrow = registry.create();
    registry.emplace<Components::Transform>(arrow, from, from);
    // The missile's own graphic, turned to face along its flight.
    Components::Sprite sprite{};
    const auto art = Core::ConfigManager::Get().GetUnitGraphicById(
        Players::ArtOf(registry, owner, missile_id), "idle_graphic0");
    if (art.Valid()) {
        sprite.slp_id = art.slp_id;
        sprite.is_file_sprite = true;
        sprite.frames_per_angle = std::max(1, art.frame_count);
        sprite.still = true;
        sprite.shadow_resolved = true;
        sprite.width = 0;
        sprite.height = 0;
    } else {
        sprite.width = 6;
        sprite.height = 2;
        sprite.color = 0xFF3A2A10;
    }
    if (flight.length.value > 0) {
        sprite.facing = Core::FixedMath::WorldVectorToFacing(to.x - from.x, to.y - from.y);
    }
    registry.emplace<Components::Sprite>(arrow, sprite);
    registry.emplace<Components::Projectile>(arrow, flight);
}

struct Landing {
    entt::entity victim;
    Core::Fixed32 amount;
    entt::entity shooter;
    Core::PlayerId owner;   // whose missile: a kill is theirs (ecs/Score)
};

// The dat's blast levels.
constexpr int kBlastFellsTrees = 1;   // this level and below fell trees too
constexpr int kBlastTargetOnly = 3;

// Who a missile coming down at its point hits: its target, if it is still
// there -- aimed true or a near miss alike -- or else the first enemy of its
// owner standing there, a stray. A blast takes every unit and building in its
// reach, whosever they are -- a mangonel's stone falls on its own side too --
// and at the lowest levels fells the trees there.
void Victims(const entt::registry& registry, const Components::Projectile& flight,
             entt::entity arrow, std::vector<std::pair<entt::entity, bool>>& out,
             std::vector<entt::entity>& felled) {
    const Components::Vector2D at = flight.to;
    auto body = [&](entt::entity e) {
        const R::UnitRules* rules = Combat::DefendingRules(registry, e);
        return rules != nullptr ? rules->size_x : Core::Fixed32(0);
    };
    auto near = [&](entt::entity e, Core::Fixed32 reach) {
        const auto& p = registry.get<Components::Transform>(e).position;
        if (registry.all_of<Components::Building, Components::Identity>(e)) {
            return Buildings::FootprintBounds(p, registry.get<Components::Identity>(e).unit_id)
                       .DistanceFrom(at) <= reach;
        }
        return Core::FixedMath::Distance(p.x - at.x, p.y - at.y) <= body(e) + reach;
    };

    if (flight.blast_width.value > 0 && flight.blast_level < kBlastTargetOnly) {
        auto all = registry.view<const Components::Transform, const Components::Health>();
        for (auto e : all) {
            if (e == flight.shooter || !Combat::Alive(registry, e)) continue;
            if (near(e, flight.blast_width)) out.emplace_back(e, false);
        }
        if (flight.blast_level <= kBlastFellsTrees) {
            auto trees = registry.view<const Components::Transform, const Components::Resource>();
            for (auto e : trees) {
                if (trees.get<const Components::Resource>(e).type != Core::ResourceType::Wood) {
                    continue;
                }
                const auto& p = trees.get<const Components::Transform>(e).position;
                if (Core::FixedMath::Distance(p.x - at.x, p.y - at.y) <= flight.blast_width) {
                    felled.push_back(e);
                }
            }
        }
        return;
    }
    if (Combat::Alive(registry, flight.target) && near(flight.target, kHitSlack)) {
        out.emplace_back(flight.target, false);
        return;
    }
    // A stray: the first enemy standing where it lands.
    auto all = registry.view<const Components::Transform, const Components::Health,
                             const Components::Owner>();
    for (auto e : all) {
        if (e == arrow || !Combat::Alive(registry, e)) continue;
        if (registry.get<Components::Owner>(e).player_id == flight.owner) continue;
        if (registry.all_of<Components::Building>(e)) continue;
        if (near(e, Core::Fixed32(0))) {
            out.emplace_back(e, true);
            return;
        }
    }
}

} // namespace

Volley VolleyOf(const entt::registry& registry, entt::entity shooter) {
    Volley volley;
    const R::UnitRules* rules = Combat::FightingRules(registry, shooter);
    if (rules == nullptr || rules->attack_count == 0) return volley;
    if (!registry.all_of<Components::Building>(shooter)) {
        volley.own = rules->projectile >= 0 ? 1 : 0;
        return volley;
    }
    volley.own = rules->projectile >= 0 ? std::max(1, rules->missiles.ToInt()) : 0;
    const int secondary = rules->secondary_projectile >= 0 ? rules->secondary_projectile
                                                           : rules->projectile;
    if (secondary < 0) return volley;

    // The building's own pierce damage a second, which each extra arrow costs.
    Core::Fixed32 own_firepower(0);
    for (int a = 0; a < rules->attack_count; ++a) {
        if (rules->attacks[a].cls == 3 && rules->reload.value > 0) {
            own_firepower = own_firepower + Core::Fixed32(rules->attacks[a].amount) / rules->reload;
        }
    }
    if (own_firepower.value <= 0) return volley;
    const int lent = (Garrisons::LentFirepower(registry, shooter) / own_firepower).ToInt();
    volley.garrison = std::clamp(lent, 0, std::max(0, rules->max_missiles - volley.own));
    return volley;
}

void Fire(entt::registry& registry, const Physics::Grid& grid, entt::entity shooter,
          entt::entity target) {
    if (!registry.valid(shooter) || !Combat::Alive(registry, target)) return;
    const R::UnitRules* rules = Combat::FightingRules(registry, shooter);
    if (rules == nullptr) return;
    // Its missiles are those of the table it plays by.
    const Core::PlayerId owner = Players::RulesPlayer(registry, shooter);
    const Volley volley = VolleyOf(registry, shooter);
    (void)grid;
    if (volley.own > 0) {
        if (const R::UnitRules* missile = Players::UnitOf(registry, owner, rules->projectile)) {
            for (int i = 0; i < volley.own; ++i) {
                LooseOne(registry, shooter, target, *rules, rules->projectile, *missile);
            }
        }
    }
    if (volley.garrison > 0) {
        const int secondary = rules->secondary_projectile >= 0 ? rules->secondary_projectile
                                                               : rules->projectile;
        if (const R::UnitRules* missile = Players::UnitOf(registry, owner, secondary)) {
            for (int i = 0; i < volley.garrison; ++i) {
                LooseOne(registry, shooter, target, *rules, secondary, *missile);
            }
        }
    }
}

void Update(entt::registry& registry, Physics::Grid& grid, int32_t dt_ms) {
    std::vector<entt::entity> landed;
    std::vector<Landing> hits;
    std::vector<std::pair<entt::entity, bool>> victims;
    std::vector<entt::entity> felled;

    auto flying = registry.view<Components::Projectile, Components::Transform,
                                Components::Sprite>();
    for (auto arrow : flying) {
        auto& flight = flying.get<Components::Projectile>(arrow);
        auto& transform = flying.get<Components::Transform>(arrow);
        auto& sprite = flying.get<Components::Sprite>(arrow);
        flight.travelled = flight.travelled +
                           flight.speed * Core::Fixed32(dt_ms) / Core::Fixed32(1000);
        if (flight.travelled >= flight.length || flight.length.value == 0) {
            transform.position = flight.to;
            victims.clear();
            Victims(registry, flight, arrow, victims, felled);
            // Damage is worked out as it lands: from the shooter's rules now
            // -- a technology finished while it flew counts -- and from the
            // hill the shooter stands on, if it is still alive to stand on
            // one. A dead shooter's missile keeps the attack it was loosed
            // with and loses the hill.
            const bool shooter_alive = Combat::Alive(registry, flight.shooter);
            const R::UnitRules* now =
                shooter_alive ? Combat::FightingRules(registry, flight.shooter) : nullptr;
            const R::ClassAmount* attacks = now != nullptr ? now->attacks : flight.attacks;
            const int attack_count = now != nullptr ? now->attack_count : flight.attack_count;
            for (const auto& [victim, stray] : victims) {
                const R::UnitRules* defender = Combat::DefendingRules(registry, victim);
                if (defender == nullptr) continue;
                const int victim_level = Combat::LevelOf(registry, grid, victim);
                const int shooter_level = shooter_alive
                    ? Combat::LevelOf(registry, grid, flight.shooter) : victim_level;
                hits.push_back({victim,
                                Combat::Blow(Combat::Damage(attacks, attack_count, *defender),
                                             shooter_level, victim_level, stray),
                                flight.shooter, flight.owner});
            }
            landed.push_back(arrow);
            continue;
        }
        const Core::Fixed32 t = flight.travelled / flight.length;
        transform.position.x = flight.from.x + (flight.to.x - flight.from.x) * t;
        transform.position.y = flight.from.y + (flight.to.y - flight.from.y) * t;
        // Its height on the screen, a parabola peaking halfway. Presentation
        // only: the flight itself is on the ground track above.
        const Core::Fixed32 height =
            flight.arc * flight.length * Core::Fixed32(4) * t * (Core::Fixed32(1) - t);
        sprite.lift_px = (height * Core::Fixed32(Core::IsometricMath::TILE_HEIGHT)).ToInt();
    }

    for (auto arrow : landed) registry.destroy(arrow);
    for (auto tree : felled) {
        if (registry.valid(tree)) Resources::Exhaust(registry, grid, tree);
    }
    for (const Landing& hit : hits) {
        Combat::Hurt(registry, grid, hit.victim, hit.amount, hit.shooter, hit.owner);
    }
}

} // namespace ECS::Missiles
