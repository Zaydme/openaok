#include "ai/World.h"

#include <algorithm>

#include "ai/Language.h"
#include "core/Rules.h"
#include "ecs/Animals.h"
#include "ecs/Players.h"
#include "ecs/Population.h"
#include "ecs/Techs.h"
#include "ecs/Victory.h"
#include "ecs/Vision.h"
#include "ecs/components/Building.h"
#include "ecs/components/Health.h"
#include "ecs/components/Identity.h"
#include "ecs/components/Mortal.h"
#include "ecs/components/Owner.h"
#include "ecs/components/PartOf.h"
#include "ecs/components/ProductionQueue.h"
#include "ecs/components/Projectile.h"
#include "ecs/components/Worker.h"

namespace AI {

namespace {

namespace R = Core::Rules;

constexpr int kVillager = 83;
// The dat's classes for trade units: the cart and the cog.
constexpr int kTradeCartClass = 19;
constexpr int kTradeBoatClass = 2;

void Clear(Counts& c, size_t units) {
    for (auto* v : {&c.units, &c.units_total, &c.buildings, &c.buildings_total}) {
        v->assign(units, 0);
    }
    c.unit_count = c.unit_total = c.building_count = c.building_total = 0;
}

void Forms(const entt::registry& registry, Core::PlayerId player, std::vector<int16_t>& out) {
    const size_t units = R::Get().units.size();
    out.resize(units);
    for (size_t i = 0; i < units; ++i) out[i] = static_cast<int16_t>(i);
    if (ECS::Players::Find(registry, player) == nullptr) return;
    const Lang::Thing* things = Lang::Things();
    for (int t = 0; t < Lang::ThingCount(); ++t) {
        const Lang::Thing& thing = things[t];
        if (!thing.line || thing.count < 2) continue;
        const int base = thing.ids[0];
        if (base < 0 || base >= static_cast<int>(units)) continue;
        const int art = ECS::Players::ArtOf(registry, player, base);
        for (int m = 0; m < thing.count; ++m) {
            if (thing.ids[m] == art) out[static_cast<size_t>(base)] = static_cast<int16_t>(art);
        }
    }
}

bool Civilian(const R::UnitRules* rules, bool worker) {
    return worker || (rules != nullptr && (rules->cls == kTradeCartClass || rules->cls == kTradeBoatClass));
}

} // namespace

int FormOf(const entt::registry& registry, Core::PlayerId player, int unit_id) {
    const Lang::Thing* things = Lang::Things();
    for (int t = 0; t < Lang::ThingCount(); ++t) {
        const Lang::Thing& thing = things[t];
        if (!thing.line || thing.ids[0] != unit_id) continue;
        const int art = ECS::Players::ArtOf(registry, player, unit_id);
        for (int m = 0; m < thing.count; ++m) {
            if (thing.ids[m] == art) return art;
        }
        return unit_id;
    }
    return unit_id;
}

int CountOf(const std::vector<int16_t>& table, const int* ids, int count, int member) {
    auto at = [&](int id) { return id >= 0 && id < static_cast<int>(table.size()) ? table[static_cast<size_t>(id)] : 0; };
    if (member >= 0) return at(ids[member]);
    int n = 0;
    for (int i = 0; i < count; ++i) n += at(ids[i]);
    return n;
}

void Take(const entt::registry& registry, Core::PlayerId viewer, const bool* allied,
          const int64_t* age_since, World& world) {
    const size_t units = R::Get().units.size();
    world.viewer = viewer;
    const auto* setup = registry.ctx().find<Core::GameSetup>();
    const ECS::Victory::Outcome* outcome = ECS::Victory::Read(registry);
    for (int p = 0; p < Core::kMaxPlayers; ++p) {
        Seat& seat = world.seats[p];
        seat = Seat{};
        Clear(world.all[p], units);
        Clear(world.seen[p], units);
        Forms(registry, static_cast<Core::PlayerId>(p), world.form_of[p]);
        if (p == Core::PLAYER_GAIA || setup == nullptr || !setup->players[p].active) continue;
        seat.valid = true;
        seat.computer = setup->players[p].computer;
        seat.civ = setup->players[p].civ;
        seat.team = setup->players[p].team;
        seat.in_game = outcome == nullptr || !outcome->Defeated(static_cast<Core::PlayerId>(p));
        seat.age = ECS::Techs::Age(registry, static_cast<Core::PlayerId>(p));
        seat.age_since_ms = age_since != nullptr ? age_since[p] : 0;
        seat.population = ECS::Population::Used(registry, static_cast<Core::PlayerId>(p));
    }

    auto view = registry.view<const ECS::Components::Identity, const ECS::Components::Owner,
                              const ECS::Components::Health>();
    for (auto e : view) {
        const Core::PlayerId p = view.get<const ECS::Components::Owner>(e).player_id;
        if (p == Core::PLAYER_GAIA || p >= Core::kMaxPlayers || !world.seats[p].valid) continue;
        if (view.get<const ECS::Components::Health>(e).current_hp <= 0) continue;
        if (registry.any_of<ECS::Components::Dying, ECS::Components::Corpse,
                            ECS::Components::PartOf, ECS::Components::Projectile>(e)) {
            continue;
        }
        const int id = view.get<const ECS::Components::Identity>(e).unit_id;
        if (id < 0 || id >= static_cast<int>(units)) continue;
        const bool building = registry.all_of<ECS::Components::Building>(e);
        const auto* worker = registry.try_get<ECS::Components::Worker>(e);
        // A herd animal a player has taken is food, not a unit of theirs.
        if (!building && ECS::Animals::Herdable(registry, e)) continue;
        int form = world.form_of[p][static_cast<size_t>(id)];
        if (worker != nullptr && worker->gatherer < 0) form = kVillager;
        const bool shown = p == viewer || (allied != nullptr && allied[p]) ||
                           ECS::Vision::Shows(registry, viewer, e);
        for (Counts* c : {&world.all[p], shown ? &world.seen[p] : nullptr}) {
            if (c == nullptr) continue;
            if (building) {
                ++c->buildings_total[static_cast<size_t>(form)];
                ++c->building_total;
                if (registry.get<ECS::Components::Building>(e).state ==
                    ECS::Components::BuildingState::Complete) {
                    ++c->buildings[static_cast<size_t>(form)];
                    ++c->building_count;
                }
            } else {
                ++c->units[static_cast<size_t>(form)];
                ++c->units_total[static_cast<size_t>(form)];
                ++c->unit_count;
                ++c->unit_total;
            }
        }
        if (!building) {
            const R::UnitRules* rules = ECS::Players::UnitOf(registry, p, id);
            if (Civilian(rules, worker != nullptr)) ++world.seats[p].civilians;
            else ++world.seats[p].military;
        }
        // What its buildings have queued counts toward its totals.
        if (const auto* queue = registry.try_get<ECS::Components::ProductionQueue>(e)) {
            for (int i = 0; i < queue->count; ++i) {
                if (queue->items[i].tech || queue->items[i].id < 0 ||
                    queue->items[i].id >= static_cast<int>(units)) {
                    continue;
                }
                const int queued = queue->items[i].id;
                const R::UnitRules* rules = ECS::Players::UnitOf(registry, p, queued);
                int qform = world.form_of[p][static_cast<size_t>(queued)];
                if (rules != nullptr && rules->cls == 4) qform = kVillager;
                for (Counts* c : {&world.all[p], shown ? &world.seen[p] : nullptr}) {
                    if (c == nullptr) continue;
                    ++c->units_total[static_cast<size_t>(qform)];
                    ++c->unit_total;
                }
            }
        }
    }
}

} // namespace AI
