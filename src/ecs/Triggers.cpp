#include "ecs/Triggers.h"

#include <algorithm>
#include <iostream>

#include "core/Command.h"
#include "core/GameSetup.h"
#include "core/Rules.h"
#include "ecs/Allegiance.h"
#include "ecs/Buildings.h"
#include "ecs/Combat.h"
#include "ecs/Diplomacy.h"
#include "ecs/Garrisons.h"
#include "ecs/Mortality.h"
#include "ecs/Orders.h"
#include "ecs/Players.h"
#include "ecs/Relics.h"
#include "ecs/ScenarioWorld.h"
#include "ecs/Techs.h"
#include "ecs/Trade.h"
#include "ecs/Victory.h"
#include "ecs/Walls.h"
#include "ecs/Vision.h"
#include "ecs/components/Building.h"
#include "ecs/components/Combat.h"
#include "ecs/components/Garrison.h"
#include "ecs/components/Gate.h"
#include "ecs/components/Health.h"
#include "ecs/components/Identity.h"
#include "ecs/components/Mortal.h"
#include "ecs/components/Owner.h"
#include "ecs/components/Resource.h"
#include "ecs/components/Transform.h"
#include "ecs/components/Worker.h"

namespace ECS::Triggers {

namespace {

namespace C = Components;
namespace R = Core::Rules;
namespace CT = Scenario::ConditionType;
namespace ET = Scenario::EffectType;

constexpr int kUnsupportedEffect = 1000;
constexpr int kBits = 256;

} // namespace

bool ConditionSupported(int type) {
    switch (type) {
    case CT::BringObjectToArea:
    case CT::OwnObjects:
    case CT::OwnFewerObjects:
    case CT::ObjectsInArea:
    case CT::DestroyObject:
    case CT::AccumulateAttribute:
    case CT::ResearchTechnology:
    case CT::Timer:
    case CT::ObjectSelected:
    case CT::AISignal:
    case CT::PlayerDefeated:
    case CT::ObjectVisible:
    case CT::ResearchingTechnology:
    case CT::UnitsGarrisoned:
    case CT::DifficultyLevel:
        return true;
    default:
        return false;
    }
}

bool EffectSupported(int type) {
    switch (type) {
    case ET::ChangeDiplomacy:
    case ET::ResearchTechnology:
    case ET::SendChat:
    case ET::PlaySound:
    case ET::SendTribute:
    case ET::UnlockGate:
    case ET::LockGate:
    case ET::ActivateTrigger:
    case ET::DeactivateTrigger:
    case ET::AIScriptGoal:
    case ET::CreateObject:
    case ET::TaskObject:
    case ET::DeclareVictory:
    case ET::KillObject:
    case ET::RemoveObject:
    case ET::ChangeView:
    case ET::Unload:
    case ET::ChangeOwnership:
    case ET::Patrol:
    case ET::DisplayInstructions:
    case ET::ClearInstructions:
    case ET::UseAdvancedButtons:
        return true;
    default:
        return false;
    }
}

namespace {

bool Seat(int player) { return player >= 0 && player < Core::kMaxPlayers; }

Core::PlayerId OwnerOf(const entt::registry& registry, entt::entity e) {
    const auto* owner = registry.try_get<C::Owner>(e);
    return owner != nullptr ? owner->player_id : Core::PLAYER_GAIA;
}

// Still in the game: not destroyed, not dying, not a body -- a unit sheltering
// in a building is.
bool Present(const entt::registry& registry, entt::entity e) {
    if (e == entt::null || !registry.valid(e)) return false;
    if (registry.any_of<C::Dying, C::Corpse>(e)) return false;
    const auto* health = registry.try_get<C::Health>(e);
    return health == nullptr || health->current_hp > 0;
}

// The tile a thing stands on, which is the file's tile number too; false for
// one out of the world, inside a building or a ship.
bool TileOf(const entt::registry& registry, entt::entity e, int& x, int& y) {
    const auto* at = registry.try_get<C::Transform>(e);
    if (at == nullptr) return false;
    x = Physics::TileOf(at->position.x);
    y = Physics::TileOf(at->position.y);
    return true;
}

bool InArea(const entt::registry& registry, entt::entity e, const Scenario::Area& area) {
    int x = 0;
    int y = 0;
    return area.Set() && TileOf(registry, e, x, y) && area.Contains(x, y);
}

// Passes an effect's or a condition's object filter: the dat unit, its class
// and its kind -- the dat's combat level, whose values are the editor's object
// types (2 building, 3 civilian, 4 military), by the owner's own rules.
bool Passes(const entt::registry& registry, entt::entity e, int object, int group, int kind) {
    const auto* identity = registry.try_get<C::Identity>(e);
    if (identity == nullptr) return false;
    if (object >= 0 && identity->unit_id != object) return false;
    if (group < 0 && kind < 0) return true;
    const R::UnitRules* rules = Players::UnitOf(registry, OwnerOf(registry, e), identity->unit_id);
    if (rules == nullptr) return false;
    if (group >= 0 && rules->cls != group) return false;
    if (kind >= 0 && rules->combat_level != kind) return false;
    return true;
}

// How many of `player`'s things pass the filter -- living, a building only
// once finished -- and stand in `area` when one is given.
int CountOwned(const entt::registry& registry, int player, int object, int group, int kind,
               const Scenario::Area* area) {
    int n = 0;
    auto view = registry.view<const C::Identity, const C::Owner>();
    for (auto e : view) {
        if (view.get<const C::Owner>(e).player_id != player) continue;
        if (!Present(registry, e)) continue;
        if (const auto* b = registry.try_get<C::Building>(e); b != nullptr && b->state != C::BuildingState::Complete) {
            continue;
        }
        if (area != nullptr && !InArea(registry, e, *area)) continue;
        if (!Passes(registry, e, object, group, kind)) continue;
        ++n;
    }
    return n;
}

const Placed* FindPlaced(const State& state, int32_t id) {
    auto it = std::lower_bound(state.placed.begin(), state.placed.end(), id,
                               [](const Placed& p, int32_t v) { return p.id < v; });
    return it != state.placed.end() && it->id == id ? &*it : nullptr;
}

entt::entity Named(const entt::registry& registry, const State& state, int32_t id) {
    const Placed* p = FindPlaced(state, id);
    return p != nullptr && registry.valid(p->entity) ? p->entity : entt::null;
}

bool Bit(const uint32_t* words, int bit) {
    return bit >= 0 && bit < kBits && ((words[bit / 32] >> (bit % 32)) & 1u) != 0;
}
void SetBit(uint32_t* words, int bit) {
    if (bit >= 0 && bit < kBits) words[bit / 32] |= 1u << (bit % 32);
}

// Whose eyes "Object Visible" looks through: the condition names no player,
// so the first human seat's, else player 1's.
Core::PlayerId Viewer(const entt::registry& registry) {
    if (const auto* setup = registry.ctx().find<Core::GameSetup>()) {
        for (int p = 1; p < Core::kMaxPlayers; ++p) {
            if (setup->players[p].active && !setup->players[p].computer) return static_cast<Core::PlayerId>(p);
        }
    }
    return 1;
}

bool Holds(const entt::registry& registry, const State& state, const Run& run,
           const Scenario::Condition& c) {
    switch (c.type) {
    case CT::BringObjectToArea: {
        const entt::entity e = Named(registry, state, c.unit);
        return Present(registry, e) && InArea(registry, e, c.area);
    }
    case CT::OwnObjects:
        return Seat(c.player) &&
               CountOwned(registry, c.player, c.object, c.group, c.kind, c.area.Set() ? &c.area : nullptr) >=
                   std::max(c.amount, 0);
    case CT::OwnFewerObjects:
        return Seat(c.player) &&
               CountOwned(registry, c.player, c.object, c.group, c.kind, c.area.Set() ? &c.area : nullptr) <=
                   c.amount;
    case CT::ObjectsInArea:
        return Seat(c.player) && c.area.Set() &&
               CountOwned(registry, c.player, c.object, c.group, c.kind, &c.area) >= std::max(c.amount, 0);
    case CT::DestroyObject: {
        // A unit the file never placed was never there to destroy.
        const Placed* p = FindPlaced(state, c.unit);
        return p != nullptr && !Present(registry, p->entity);
    }
    case CT::AccumulateAttribute: {
        const Players::Player* player = Seat(c.player) ? Players::Find(registry, static_cast<Core::PlayerId>(c.player)) : nullptr;
        return player != nullptr && c.resource >= 0 &&
               Players::Resource(*player, c.resource) >= Core::Fixed32(c.amount);
    }
    case CT::ResearchTechnology:
        return Seat(c.player) && Techs::Researched(registry, static_cast<Core::PlayerId>(c.player), c.technology);
    case CT::Timer:
        return c.timer >= 0 && run.active_ms >= c.timer * 1000;
    case CT::ObjectSelected: {
        const entt::entity e = Named(registry, state, c.unit);
        if (e == entt::null) return false;
        for (const auto& chosen : state.selected) {
            if (std::find(chosen.begin(), chosen.end(), e) != chosen.end()) return true;
        }
        return false;
    }
    case CT::AISignal:
        return Bit(state.signals, c.ai_signal);
    case CT::PlayerDefeated: {
        const Victory::Outcome* outcome = Victory::Read(registry);
        return Seat(c.player) && outcome != nullptr && outcome->Defeated(static_cast<Core::PlayerId>(c.player));
    }
    case CT::ObjectVisible: {
        const entt::entity e = Named(registry, state, c.unit);
        int x = 0;
        int y = 0;
        return Present(registry, e) && TileOf(registry, e, x, y) &&
               Vision::Visible(registry, Viewer(registry), x, y);
    }
    case CT::ResearchingTechnology: {
        const Players::Player* player = Seat(c.player) ? Players::Find(registry, static_cast<Core::PlayerId>(c.player)) : nullptr;
        return player != nullptr && c.technology >= 0 &&
               c.technology < static_cast<int32_t>(player->techs.size()) &&
               player->techs[static_cast<size_t>(c.technology)].state == Players::TechState::Researching;
    }
    case CT::UnitsGarrisoned: {
        // Relics in a monastery are held there as garrisoned units are.
        const entt::entity e = Named(registry, state, c.unit);
        return Present(registry, e) &&
               Garrisons::Count(registry, e) + Relics::Held(registry, e) >= std::max(c.amount, 0);
    }
    case CT::DifficultyLevel: {
        // At least as easy as the level named: the levels run 0 hardest to 4
        // easiest, and a condition naming one level also holds on every
        // easier one.
        const auto* setup = registry.ctx().find<Core::GameSetup>();
        return setup != nullptr && c.amount >= 0 && setup->difficulty >= c.amount;
    }
    default:
        return false;
    }
}

void Post(entt::registry& registry, const State& state, Shown line) {
    auto* display = registry.ctx().find<Display>();
    if (display == nullptr) return;
    line.tick = state.tick;
    display->lines.push_back(std::move(line));
    if (display->lines.size() > kKept) {
        display->lines.erase(display->lines.begin(),
                             display->lines.begin() + static_cast<long>(display->lines.size() - kKept));
    }
}

// Whether something can be given an order: a unit, not a building, a cliff
// or a flag.
bool Orderable(const entt::registry& registry, entt::entity e) {
    const auto* identity = registry.try_get<C::Identity>(e);
    if (identity == nullptr) return false;
    const R::UnitRules* rules = Players::UnitOf(registry, OwnerOf(registry, e), identity->unit_id);
    return rules != nullptr && rules->type == R::UnitType::Creatable;
}

// The things an effect acts on: the units it names, those of them its source
// player owns (any owner when it names no player); or else the source
// player's things in its area, through its filters -- only units, for an
// effect that gives orders.
std::vector<entt::entity> Affected(const entt::registry& registry, const State& state,
                                   const Scenario::Effect& e, bool orders = false) {
    std::vector<entt::entity> out;
    if (!e.units.empty()) {
        for (int32_t id : e.units) {
            const entt::entity unit = Named(registry, state, id);
            if (!Present(registry, unit)) continue;
            if (e.player >= 0 && OwnerOf(registry, unit) != e.player) continue;
            if (orders && !Orderable(registry, unit)) continue;
            out.push_back(unit);
        }
        return out;
    }
    if (!e.area.Set() || !Seat(e.player)) return out;
    auto view = registry.view<const C::Identity, const C::Owner, const C::Transform>();
    for (auto unit : view) {
        if (view.get<const C::Owner>(unit).player_id != e.player) continue;
        if (!Present(registry, unit) || !InArea(registry, unit, e.area)) continue;
        if (!Passes(registry, unit, e.object, e.group, e.kind)) continue;
        if (orders && !Orderable(registry, unit)) continue;
        out.push_back(unit);
    }
    return out;
}

// Sends `unit` at `target` as a right-click would -- to fight it, gather from
// it, trade with it, shelter in it, fetch it -- or else to where it stands, or
// to `point`. Through ecs/Orders, as the unit's owner.
void Task(entt::registry& registry, Physics::Grid& grid, entt::entity unit, entt::entity target,
          const Scenario::Point& point) {
    if (!registry.all_of<C::Transform>(unit)) return;
    const Core::PlayerId owner = OwnerOf(registry, unit);
    if (target != entt::null && Present(registry, target)) {
        if (registry.all_of<C::Combat>(unit) && Combat::Hostile(registry, unit, target)) {
            Orders::Execute(registry, grid, Core::AttackCommand{unit, target, owner});
        } else if (registry.all_of<C::Resource>(target) && registry.all_of<C::Worker>(unit)) {
            Orders::Execute(registry, grid, Core::GatherCommand{unit, target, owner});
        } else if (Trade::CanTrade(registry, unit, target)) {
            Orders::Execute(registry, grid, Core::TradeCommand{unit, target, owner});
        } else if (Garrisons::Accepts(registry, target, unit)) {
            Orders::Execute(registry, grid, Core::GarrisonCommand{unit, target, owner});
        } else if (Relics::CanFetch(registry, unit, target) || Relics::CanDeposit(registry, unit, target)) {
            Orders::Execute(registry, grid, Core::RelicCommand{unit, target, owner});
        } else if (const auto* at = registry.try_get<C::Transform>(target)) {
            Orders::Execute(registry, grid, Core::MoveCommand{unit, at->position.x, at->position.y, owner});
        }
        return;
    }
    if (point.Set()) {
        Orders::Execute(registry, grid,
                        Core::MoveCommand{unit, Core::Fixed32(point.x), Core::Fixed32(point.y), owner});
    }
}

void Kill(entt::registry& registry, Physics::Grid& grid, entt::entity e) {
    if (registry.all_of<C::Building>(e)) {
        Buildings::Destroy(registry, grid, e);
        return;
    }
    const auto* health = registry.try_get<C::Health>(e);
    // A unit dies where it stands; one inside something, or a thing with no
    // life to lose, is simply gone.
    if (health != nullptr && registry.all_of<C::Transform>(e)) {
        if (health->current_hp > 0) Mortality::Kill(registry, grid, e);
        return;
    }
    ScenarioWorld::Remove(registry, grid, e);
}

// A tribute by trigger: free, and no more than the giver has of what is
// asked (so 9999 reads as "everything"); from Gaia, all of it.
void Tribute(entt::registry& registry, int from, int to, int resource, int amount) {
    if (amount <= 0 || resource < 0 || !Seat(from) || !Seat(to)) return;
    Core::Fixed32 given(amount);
    if (from != Core::PLAYER_GAIA) {
        Players::Player* giver = Players::Find(registry, static_cast<Core::PlayerId>(from));
        if (giver == nullptr) return;
        const Core::Fixed32 has = Players::Resource(*giver, resource);
        if (has < given) given = has.value > 0 ? has : Core::Fixed32(0);
        Players::AddResource(*giver, resource, Core::Fixed32(0) - given);
    }
    if (to != Core::PLAYER_GAIA) {
        if (Players::Player* taker = Players::Find(registry, static_cast<Core::PlayerId>(to))) {
            Players::AddResource(*taker, resource, given);
        }
    }
}

Diplomacy::Stance StanceOf(int value) {
    if (value == 0) return Diplomacy::Stance::Ally;
    if (value == 1) return Diplomacy::Stance::Neutral;
    return Diplomacy::Stance::Enemy;
}

void Carry(entt::registry& registry, Physics::Grid& grid, State& state, int trigger,
           const Scenario::Effect& e) {
    switch (e.type) {
    case ET::ChangeDiplomacy:
        if (Seat(e.player) && Seat(e.target_player) && e.player != Core::PLAYER_GAIA &&
            e.target_player != Core::PLAYER_GAIA) {
            Diplomacy::SetStance(registry, static_cast<Core::PlayerId>(e.player),
                                 static_cast<Core::PlayerId>(e.target_player), StanceOf(e.diplomacy));
        }
        break;
    case ET::ResearchTechnology:
        if (Seat(e.player) && e.player != Core::PLAYER_GAIA &&
            !Techs::Researched(registry, static_cast<Core::PlayerId>(e.player), e.technology)) {
            Techs::Complete(registry, static_cast<Core::PlayerId>(e.player), e.technology);
        }
        break;
    case ET::SendChat:
        Post(registry, state, Shown{.kind = Show::Chat,
                                    .player = static_cast<Core::PlayerId>(Seat(e.player) ? e.player : 0),
                                    .trigger = trigger, .string_id = e.string_id,
                                    .text = Scenario::Show(e.message, e.string_id), .sound = e.sound});
        break;
    case ET::PlaySound:
        Post(registry, state, Shown{.kind = Show::Sound,
                                    .player = static_cast<Core::PlayerId>(Seat(e.player) ? e.player : 0),
                                    .trigger = trigger, .sound = e.sound, .sound_id = e.sound_id});
        break;
    case ET::SendTribute:
        Tribute(registry, e.player, e.target_player, e.resource, e.amount);
        break;
    case ET::UnlockGate:
    case ET::LockGate:
        for (entt::entity gate : Affected(registry, state, e)) {
            auto& locked = state.locked_gates;
            locked.erase(std::remove(locked.begin(), locked.end(), gate), locked.end());
            if (e.type == ET::LockGate) locked.push_back(gate);
            // And the gate itself, as its owner's Lock button would (ecs/Walls).
            if (registry.all_of<Components::Gate>(gate)) {
                Walls::SetLocked(registry, grid, gate, e.type == ET::LockGate);
            }
        }
        break;
    case ET::ActivateTrigger:
        if (e.trigger >= 0 && e.trigger < static_cast<int32_t>(state.runs.size())) {
            Run& run = state.runs[static_cast<size_t>(e.trigger)];
            if (!run.on) {
                run.on = true;
                run.active_ms = 0;
            }
        }
        break;
    case ET::DeactivateTrigger:
        if (e.trigger >= 0 && e.trigger < static_cast<int32_t>(state.runs.size())) {
            state.runs[static_cast<size_t>(e.trigger)].on = false;
        }
        break;
    case ET::AIScriptGoal:
        if (Seat(e.player)) SetBit(state.goals[e.player], e.ai_goal);
        break;
    case ET::CreateObject:
        if (Seat(e.player) && e.location.Set() && e.object >= 0) {
            ScenarioWorld::Place(registry, grid, e.object, static_cast<Core::PlayerId>(e.player),
                                 {Core::Fixed32(e.location.x), Core::Fixed32(e.location.y)},
                                 Core::Fixed32(0), 0);
        }
        break;
    case ET::TaskObject: {
        const entt::entity target = e.location_unit >= 0 ? Named(registry, state, e.location_unit) : entt::null;
        for (entt::entity unit : Affected(registry, state, e, true)) Task(registry, grid, unit, target, e.location);
        break;
    }
    case ET::DeclareVictory:
        if (Seat(e.player) && e.player != Core::PLAYER_GAIA) {
            state.declared = static_cast<uint16_t>(state.declared | (1u << e.player));
        }
        break;
    case ET::KillObject:
        for (entt::entity unit : Affected(registry, state, e)) {
            if (registry.valid(unit)) Kill(registry, grid, unit);
        }
        break;
    case ET::RemoveObject:
        for (entt::entity unit : Affected(registry, state, e)) {
            if (registry.valid(unit)) ScenarioWorld::Remove(registry, grid, unit);
        }
        break;
    case ET::ChangeView:
        if (e.location.Set()) {
            Post(registry, state, Shown{.kind = Show::View,
                                        .player = static_cast<Core::PlayerId>(Seat(e.player) ? e.player : 0),
                                        .trigger = trigger, .x = e.location.x, .y = e.location.y});
        }
        break;
    case ET::Unload:
        if (e.location.Set()) {
            for (entt::entity unit : Affected(registry, state, e, true)) {
                if (!Garrisons::IsTransport(registry, unit)) continue;
                Orders::Execute(registry, grid,
                                Core::UnloadCommand{unit, Core::Fixed32(e.location.x),
                                                    Core::Fixed32(e.location.y), OwnerOf(registry, unit)});
            }
        }
        break;
    case ET::ChangeOwnership:
        if (Seat(e.target_player)) {
            for (entt::entity unit : Affected(registry, state, e)) {
                if (registry.valid(unit)) {
                    Allegiance::Change(registry, grid, unit, static_cast<Core::PlayerId>(e.target_player));
                }
            }
        }
        break;
    case ET::Patrol:
        if (e.location.Set()) {
            for (entt::entity unit : Affected(registry, state, e, true)) {
                Orders::Execute(registry, grid,
                                Core::PatrolCommand{{unit}, Core::Fixed32(e.location.x),
                                                    Core::Fixed32(e.location.y), OwnerOf(registry, unit)});
            }
        }
        break;
    case ET::DisplayInstructions:
        Post(registry, state, Shown{.kind = Show::Instructions, .player = 0, .trigger = trigger,
                                    .string_id = e.string_id,
                                    .text = Scenario::Show(e.message, e.string_id), .sound = e.sound,
                                    .seconds = std::max(e.display_time, 0),
                                    .panel = std::max(e.panel, 0)});
        break;
    case ET::ClearInstructions:
        Post(registry, state, Shown{.kind = Show::ClearInstructions, .player = 0, .trigger = trigger,
                                    .panel = std::max(e.panel, 0)});
        break;
    case ET::UseAdvancedButtons:
        Post(registry, state, Shown{.kind = Show::AdvancedButtons, .player = 0, .trigger = trigger});
        break;
    default:
        break;   // named once at setup
    }
}

// The units and buildings the scenario forbids each player, held forbidden:
// a technology researched in play may enable one again.
void HoldDisabled(entt::registry& registry, const Scenario::File& file) {
    for (int p = 1; p <= Scenario::kPlayers; ++p) {
        const Scenario::Player& from = file.players[p];
        if (from.disabled_units.empty() && from.disabled_buildings.empty()) continue;
        Players::Player* player = Players::Find(registry, static_cast<Core::PlayerId>(p));
        if (player == nullptr || !player->active) continue;
        for (const auto* list : {&from.disabled_units, &from.disabled_buildings}) {
            for (int32_t id : *list) {
                if (id >= 0 && id < static_cast<int32_t>(player->units.size())) {
                    player->units[static_cast<size_t>(id)].enabled = false;
                }
            }
        }
    }
}

} // namespace

void Setup(entt::registry& registry, const Scenario::File& file, std::vector<Placed> placed) {
    State state;
    state.runs.resize(file.triggers.size());
    for (size_t i = 0; i < file.triggers.size(); ++i) state.runs[i].on = file.triggers[i].enabled;
    std::stable_sort(placed.begin(), placed.end(),
                     [](const Placed& a, const Placed& b) { return a.id < b.id; });
    state.placed = std::move(placed);

    for (const Scenario::Trigger& t : file.triggers) {
        for (const auto& c : t.conditions) {
            if (!ConditionSupported(c.type) &&
                std::find(state.unsupported.begin(), state.unsupported.end(), c.type) == state.unsupported.end()) {
                state.unsupported.push_back(c.type);
            }
        }
        for (const auto& e : t.effects) {
            const int32_t code = kUnsupportedEffect + e.type;
            if (!EffectSupported(e.type) &&
                std::find(state.unsupported.begin(), state.unsupported.end(), code) == state.unsupported.end()) {
                state.unsupported.push_back(code);
            }
        }
    }
    for (int32_t code : state.unsupported) {
        if (code >= kUnsupportedEffect) {
            std::cerr << "[Triggers] effect " << code - kUnsupportedEffect << " ("
                      << Scenario::EffectName(code - kUnsupportedEffect) << ") is read but not carried out"
                      << std::endl;
        } else {
            std::cerr << "[Triggers] condition " << code << " (" << Scenario::ConditionName(code)
                      << ") is read but never holds" << std::endl;
        }
    }
    HoldDisabled(registry, file);
    registry.ctx().insert_or_assign(std::move(state));
    registry.ctx().insert_or_assign(Display{});
}

void Update(entt::registry& registry, Physics::Grid& grid, int32_t dt_ms) {
    State* state = registry.ctx().find<State>();
    const Scenario::File* file = registry.ctx().find<Scenario::File>();
    if (state == nullptr || file == nullptr) return;
    ++state->tick;
    for (Run& run : state->runs) {
        if (run.on) run.active_ms += dt_ms;
    }
    HoldDisabled(registry, *file);

    state->check_ms -= dt_ms;
    if (state->check_ms > 0) return;
    state->check_ms += kCheckMs;
    // Nothing more happens in a game that is over.
    if (const Victory::Outcome* outcome = Victory::Read(registry); outcome != nullptr && outcome->over) return;

    state->declared = 0;
    const size_t count = std::min(state->runs.size(), file->triggers.size());
    for (size_t i = 0; i < count; ++i) {
        if (!state->runs[i].on) continue;
        const Scenario::Trigger& trigger = file->triggers[i];
        bool all = true;
        for (const auto& condition : trigger.conditions) {
            if (!Holds(registry, *state, state->runs[i], condition)) {
                all = false;
                break;
            }
        }
        if (!all) continue;
        // Off before its effects, so one of them may turn it on again; a
        // looping one starts its timers over.
        state->runs[i].fired = true;
        if (trigger.looping) {
            state->runs[i].active_ms = 0;
        } else {
            state->runs[i].on = false;
        }
        for (const auto& effect : trigger.effects) {
            Carry(registry, grid, *state, static_cast<int>(i), effect);
        }
    }
    if (state->declared != 0) {
        const uint16_t winners = state->declared;
        state->declared = 0;
        Victory::Declare(registry, winners);
    }
}

void NoteSelection(entt::registry& registry, Core::PlayerId player,
                   const std::vector<entt::entity>& units) {
    State* state = registry.ctx().find<State>();
    if (state == nullptr || player >= Core::kMaxPlayers) return;
    state->selected[player] = units;
}

bool WatchesSelection(const entt::registry& registry) {
    const Scenario::File* file = ScenarioOf(registry);
    if (file == nullptr) return false;
    for (const auto& t : file->triggers) {
        for (const auto& c : t.conditions) {
            if (c.type == CT::ObjectSelected) return true;
        }
    }
    return false;
}

void RaiseSignal(entt::registry& registry, int signal) {
    if (State* state = registry.ctx().find<State>()) SetBit(state->signals, signal);
}

bool GoalRaised(const entt::registry& registry, Core::PlayerId player, int goal) {
    const State* state = Read(registry);
    return state != nullptr && player < Core::kMaxPlayers && Bit(state->goals[player], goal);
}

bool TakeGoal(entt::registry& registry, Core::PlayerId player, int goal) {
    State* state = registry.ctx().find<State>();
    if (state == nullptr || player >= Core::kMaxPlayers || !Bit(state->goals[player], goal)) return false;
    state->goals[player][goal / 32] &= ~(1u << (goal % 32));
    return true;
}

bool GateLocked(const entt::registry& registry, entt::entity gate) {
    const State* state = Read(registry);
    return state != nullptr &&
           std::find(state->locked_gates.begin(), state->locked_gates.end(), gate) != state->locked_gates.end();
}

entt::entity EntityOf(const entt::registry& registry, int32_t id) {
    const State* state = Read(registry);
    return state != nullptr ? Named(registry, *state, id) : entt::null;
}

const Scenario::File* ScenarioOf(const entt::registry& registry) {
    return registry.ctx().find<Scenario::File>();
}

const State* Read(const entt::registry& registry) { return registry.ctx().find<State>(); }

const Display* ReadDisplay(const entt::registry& registry) { return registry.ctx().find<Display>(); }

std::vector<Objective> Objectives(const entt::registry& registry) {
    std::vector<Objective> out;
    const Scenario::File* file = ScenarioOf(registry);
    const State* state = Read(registry);
    if (file == nullptr || state == nullptr) return out;
    for (size_t i = 0; i < file->triggers.size(); ++i) {
        const Scenario::Trigger& t = file->triggers[i];
        if (!t.objective) continue;
        Objective o;
        o.trigger = static_cast<int32_t>(i);
        o.order = t.objective_order;
        o.string_id = t.string_id;
        o.text = Scenario::Show(t.description, t.string_id);
        if (i < state->runs.size()) {
            o.on = state->runs[i].on;
            o.fired = state->runs[i].fired;
        }
        out.push_back(std::move(o));
    }
    std::stable_sort(out.begin(), out.end(),
                     [](const Objective& a, const Objective& b) { return a.order < b.order; });
    return out;
}

} // namespace ECS::Triggers
