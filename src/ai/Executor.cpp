#include "ai/Executor.h"

#include <algorithm>

#include "ai/Economy.h"
#include "ai/Facts.h"
#include "ai/Military.h"
#include "ai/Navy.h"
#include "ai/Relics.h"
#include "ai/Scouting.h"
#include "ai/Things.h"
#include "core/FixedMath.h"
#include "core/Rules.h"
#include "ecs/Events.h"
#include "ecs/Players.h"
#include "ecs/Population.h"
#include "ecs/Techs.h"
#include "ecs/components/Building.h"
#include "ecs/components/Health.h"
#include "ecs/components/Identity.h"
#include "ecs/components/Owner.h"
#include "ecs/components/Transform.h"
#include "ecs/components/Worker.h"

namespace AI::Executor {

namespace {

namespace R = Core::Rules;

int Sn(const Turn& turn, Lang::SN sn) { return turn.mind->sn[static_cast<size_t>(sn)]; }

void MarkBusy(Census& census, entt::entity building) {
    for (Census::Site& site : census.buildings) {
        if (site.entity == building) site.idle = false;
    }
}

// What the event log says happened since the brain last looked: blows
// struck in its town by an enemy.
void News(Turn& turn) {
    const ECS::Events::Log* log = ECS::Events::Read(*turn.registry);
    if (log == nullptr) return;
    Brain& brain = *turn.brain;
    const uint64_t since = brain.events_tick;
    brain.events_tick = log->tick;
    // Tribute given to the player, for players-tribute and its memory.
    for (const ECS::Events::Event& e : log->events) {
        if (e.tick <= since || e.kind != ECS::Events::Kind::Tribute || e.player != turn.player) continue;
        if (e.from >= Core::kMaxPlayers || e.id < 0 || e.id > 3) continue;
        const auto r = static_cast<size_t>(Core::Rules::StockpileOf(e.id));
        turn.mind->tribute[e.from][r] += e.amount;
        turn.mind->tribute_memory[e.from][r] += e.amount;
    }
    // Blows from an ally: enough of them and, unless teams are locked, the
    // player turns on it.
    for (const ECS::Events::Event& e : log->events) {
        if (e.tick <= since || e.kind != ECS::Events::Kind::Struck || e.player != turn.player) continue;
        if (!turn.registry->valid(e.other)) continue;
        const auto* attacker = turn.registry->try_get<ECS::Components::Owner>(e.other);
        if (attacker == nullptr) continue;
        const Core::PlayerId p = attacker->player_id;
        if (p == turn.player || p == Core::PLAYER_GAIA || p >= Core::kMaxPlayers) continue;
        if (Facts::StanceToward(turn, p) != Lang::Stance::Ally) continue;
        int& hits = brain.ally_hits[p];
        ++hits;
        if (hits < Sn(turn, Lang::SN::HitsBeforeAllianceChange)) continue;
        if (turn.setup == nullptr || turn.setup->lock_teams) continue;
        hits = 0;
        Give(*turn.registry, *turn.grid,
             Core::DiplomacyCommand{p, Facts::DiplomacyStance(static_cast<int>(Lang::Stance::Enemy)), turn.player});
        turn.mind->stance[p] = static_cast<uint8_t>(Lang::Stance::Enemy);
    }
    if (turn.census->town_center == entt::null) return;
    const entt::registry& registry = *turn.registry;
    const Core::Fixed32 town(std::max(8, Sn(turn, Lang::SN::MaximumTownSize)) +
                             Sn(turn, Lang::SN::SentryDistance));
    for (const ECS::Events::Event& e : log->events) {
        if (e.tick <= since || e.kind != ECS::Events::Kind::Struck) continue;
        if (!registry.valid(e.subject) || !registry.valid(e.other)) continue;
        const auto* victim = registry.try_get<ECS::Components::Owner>(e.subject);
        const auto* attacker = registry.try_get<ECS::Components::Owner>(e.other);
        if (victim == nullptr || attacker == nullptr || victim->player_id != turn.player) continue;
        const Core::PlayerId p = attacker->player_id;
        if (p == turn.player || p == Core::PLAYER_GAIA || p >= Core::kMaxPlayers) continue;
        if (Core::FixedMath::Distance(e.at.x - turn.census->home.x, e.at.y - turn.census->home.y) > town) continue;
        brain.town_struck_ms = turn.now_ms;
        brain.intruder = e.other;
    }
}

} // namespace

void Refresh(Turn& turn) {
    Census& c = *turn.census;
    if (const ECS::Players::Player* seat = ECS::Players::Find(*turn.registry, turn.player)) {
        for (int r = 0; r < 4; ++r) c.stock[r] = ECS::Players::Stock(*seat, static_cast<Core::ResourceType>(r));
    }
    c.population = ECS::Population::Used(*turn.registry, turn.player);
    c.capacity = ECS::Population::Capacity(*turn.registry, turn.player);
}

bool Train(Turn& turn, int slot) {
    const R::UnitRules* rules = ECS::Players::UnitOf(*turn.registry, turn.player, slot);
    if (rules == nullptr) return false;
    // The same criteria as the can-train fact.
    if (!Things::Affords(*turn.census, *turn.mind, rules->costs, rules->cost_count, false)) return false;
    if (!ECS::Population::HasRoom(*turn.registry, turn.player, ECS::Population::CostOf(*rules))) return false;
    const entt::entity at = Things::IdleAt(*turn.census, rules->train_loc);
    if (at == entt::null) return false;
    Give(*turn.registry, *turn.grid, Core::TrainCommand{at, slot, turn.player});
    MarkBusy(*turn.census, at);
    if (turn.world != nullptr) {
        Counts& own = turn.world->all[turn.player];
        const int form = rules->cls == 4 ? 83 : turn.world->form_of[turn.player][static_cast<size_t>(slot)];
        ++own.units_total[static_cast<size_t>(form)];
        ++own.unit_total;
    }
    Refresh(turn);
    return true;
}

bool Research(Turn& turn, int tech) {
    const R::TechRules* rules = R::Tech(tech);
    if (rules == nullptr || !ECS::Techs::Available(*turn.registry, turn.player, tech)) return false;
    int count = 0;
    const R::Cost* costs = Things::TechCosts(*turn.registry, turn.player, tech, count);
    if (costs == nullptr || !Things::Affords(*turn.census, *turn.mind, costs, count, false)) return false;
    const entt::entity at = Things::IdleAt(*turn.census, rules->location);
    if (at == entt::null) return false;
    Give(*turn.registry, *turn.grid, Core::ResearchCommand{at, tech, turn.player});
    MarkBusy(*turn.census, at);
    Refresh(turn);
    return true;
}

bool Build(Turn& turn, int slot, bool forward) {
    const R::UnitRules* rules = ECS::Players::UnitOf(*turn.registry, turn.player, slot);
    if (rules == nullptr) return false;
    if (!Things::Affords(*turn.census, *turn.mind, rules->costs, rules->cost_count, false)) return false;
    if (!Economy::Build(turn, slot, forward)) return false;
    Refresh(turn);
    return true;
}

void AttackNow(Turn& turn) { Military::AttackNow(turn); }

bool Delete(Turn& turn, int thing, bool building) {
    // Deletes exactly one of the given type: the newest.
    const Things::Line line = Things::LineOf(Lang::Things()[thing], turn.world->seats[turn.player].civ);
    if (line.count == 0) return false;
    const entt::registry& registry = *turn.registry;
    entt::entity pick = entt::null;
    auto view = registry.view<const ECS::Components::Identity, const ECS::Components::Owner,
                              const ECS::Components::Health>();
    for (auto e : view) {
        if (view.get<const ECS::Components::Owner>(e).player_id != turn.player) continue;
        if (registry.all_of<ECS::Components::Building>(e) != building) continue;
        if (view.get<const ECS::Components::Health>(e).current_hp <= 0) continue;
        int form = turn.world->form_of[turn.player][static_cast<size_t>(view.get<const ECS::Components::Identity>(e).unit_id)];
        if (const auto* w = registry.try_get<ECS::Components::Worker>(e); w != nullptr && w->gatherer < 0) form = 83;
        bool fits = false;
        for (int i = 0; i < line.count; ++i) {
            if ((line.member < 0 || i == line.member) && line.ids[i] == form) fits = true;
        }
        if (fits && (pick == entt::null || e > pick)) pick = e;
    }
    if (pick == entt::null) return false;
    Give(*turn.registry, *turn.grid, Core::DeleteCommand{pick, turn.player});
    Prune(*turn.registry, *turn.census);
    Refresh(turn);
    return true;
}

void Resign(Turn& turn) {
    if (turn.brain->resigned) return;
    turn.brain->resigned = true;
    Give(*turn.registry, *turn.grid, Core::ResignCommand{turn.player});
    Prune(*turn.registry, *turn.census);
}

void Run(Turn& turn) {
    // The rules' own orders this pass may have taken things out.
    Prune(*turn.registry, *turn.census);
    News(turn);
    Economy::Run(turn);
    Scouting::Claim(turn);
    Relics::Run(turn);
    Military::Run(turn);
    Navy::Run(turn);
    Scouting::Run(turn);
}

} // namespace AI::Executor
