#include "ai/Things.h"

#include "ai/Catalog.h"
#include "ecs/Players.h"
#include "ecs/Techs.h"

namespace AI::Things {

namespace {

namespace R = Core::Rules;

// The castle: where every civilisation's unique unit is trained and its
// elite technology researched.
constexpr int kCastle = 82;

Unique FindUnique(int civ) {
    Unique out;
    const auto& techs = R::Get().techs;
    for (int t = 0; t < static_cast<int>(techs.size()); ++t) {
        const R::TechRules& tech = techs[static_cast<size_t>(t)];
        if (!tech.valid || tech.civ != civ || tech.location != kCastle || tech.effect < 0) continue;
        const auto& effects = R::Get().effects;
        if (tech.effect >= static_cast<int>(effects.size())) continue;
        for (const R::EffectCommand& c : effects[static_cast<size_t>(tech.effect)].commands) {
            if (c.type != R::EffectType::UpgradeUnit) continue;
            const R::UnitRules* from = R::Unit(c.a);
            if (from == nullptr || from->train_loc != kCastle) continue;
            out.unit = c.a;
            out.elite = c.b;
            out.tech = t;
            return out;
        }
    }
    return out;
}

} // namespace

Unique UniqueOf(int civ) {
    // The dat does not change in a game: worked out once per civilisation.
    constexpr int kCivs = 64;
    static Unique table[kCivs];
    static bool known[kCivs] = {};
    if (civ < 0 || civ >= kCivs) return FindUnique(civ);
    if (!known[civ]) {
        table[civ] = FindUnique(civ);
        known[civ] = true;
    }
    return table[civ];
}

Line LineOf(const Lang::Thing& thing, int civ) {
    Line line;
    switch (thing.kind) {
    case Lang::ThingKind::Unit:
    case Lang::ThingKind::Building:
        for (int i = 0; i < thing.count; ++i) line.ids[i] = thing.ids[i];
        line.count = thing.count;
        line.member = thing.line ? -1 : thing.member;
        break;
    case Lang::ThingKind::UniqueUnit:
    case Lang::ThingKind::EliteUniqueUnit:
    case Lang::ThingKind::UniqueUnitLine: {
        const Unique u = UniqueOf(civ);
        if (u.unit < 0) break;
        line.ids[0] = u.unit;
        line.ids[1] = u.elite;
        line.count = u.elite >= 0 ? 2 : 1;
        line.member = thing.kind == Lang::ThingKind::UniqueUnit ? 0
                    : thing.kind == Lang::ThingKind::EliteUniqueUnit ? 1 : -1;
        break;
    }
    default:
        break;
    }
    return line;
}

int Slot(const entt::registry& registry, const World& world, Core::PlayerId player,
         const Lang::Thing& thing, int civ) {
    const Line line = LineOf(thing, civ);
    if (line.count == 0) return -1;
    const int slot = line.ids[0];
    const R::UnitRules* rules = ECS::Players::UnitOf(registry, player, slot);
    if (rules == nullptr || !rules->valid) return -1;
    if (line.member >= 0) {
        const auto& forms = world.form_of[player];
        const int form = slot < static_cast<int>(forms.size()) ? forms[static_cast<size_t>(slot)] : slot;
        if (form != line.ids[line.member]) return -1;
    }
    if (thing.kind == Lang::ThingKind::Building) {
        if (Catalog::Buildable(registry, player, slot)) return slot;
        // A building villagers raise from a button unit of its own -- a Town
        // Center is placed from RTWC1X and its age's kin, whose stack unit is
        // the Town Center.
        const auto& units = R::Get().units;
        for (int id = 0; id < static_cast<int>(units.size()); ++id) {
            if (units[static_cast<size_t>(id)].stack_unit != slot) continue;
            if (Catalog::Buildable(registry, player, id)) return id;
        }
        return -1;
    }
    return rules->enabled && rules->train_loc >= 0 ? slot : -1;
}

int Tech(const entt::registry& registry, Core::PlayerId player, const Lang::Thing& thing, int civ) {
    switch (thing.kind) {
    case Lang::ThingKind::Research:
        return thing.tech;
    case Lang::ThingKind::Age:
        return ECS::Techs::AgeTech(registry, player, thing.tech);
    case Lang::ThingKind::UniqueUnitUpgrade:
        return UniqueOf(civ).tech;
    default:
        return -1;
    }
}

bool Affords(const Census& census, const Mind& mind, const R::Cost* costs, int count,
             bool with_escrow) {
    for (int c = 0; c < count; ++c) {
        if (!costs[c].paid || costs[c].amount <= 0) continue;
        const Core::ResourceType kind = R::StockpileOf(costs[c].resource);
        if (kind == Core::ResourceType::COUNT) continue;
        const int k = static_cast<int>(kind);
        const int have = census.stock[k] - (with_escrow ? 0 : mind.escrow[static_cast<size_t>(k)]);
        if (have < costs[c].amount) return false;
    }
    return true;
}

entt::entity IdleAt(const Census& census, int unit_id) {
    for (const Census::Site& site : census.buildings) {
        if (site.unit_id == unit_id && site.complete && site.idle) return site.entity;
    }
    return entt::null;
}

const R::Cost* TechCosts(const entt::registry& registry, Core::PlayerId player, int tech, int& count) {
    count = 0;
    const ECS::Players::Player* seat = ECS::Players::Find(registry, player);
    if (seat == nullptr || tech < 0 || tech >= static_cast<int>(seat->techs.size())) return nullptr;
    const auto& status = seat->techs[static_cast<size_t>(tech)];
    count = status.cost_count;
    return status.costs;
}

} // namespace AI::Things
