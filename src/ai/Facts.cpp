#include "ai/Facts.h"

#include <algorithm>

#include "ai/Things.h"
#include "core/FixedMath.h"
#include "core/Rules.h"
#include "ecs/Animals.h"
#include "ecs/BuildingGeometry.h"
#include "ecs/Combat.h"
#include "ecs/Diplomacy.h"
#include "ecs/DropSites.h"
#include "ecs/Market.h"
#include "ecs/Players.h"
#include "ecs/Population.h"
#include "ecs/Score.h"
#include "ecs/Techs.h"
#include "ecs/Vision.h"
#include "ecs/components/Building.h"
#include "ecs/components/Identity.h"
#include "ecs/components/Mortal.h"
#include "ecs/components/Owner.h"
#include "ecs/components/Resource.h"
#include "ecs/components/Transform.h"
#include "ecs/components/Worker.h"

namespace AI::Facts {

namespace {

namespace R = Core::Rules;
using Lang::Fact;
using Lang::RelOp;
using Vector2D = ECS::Components::Vector2D;

constexpr int kForageClass = 7;
constexpr int kTreeClass = 15;
constexpr int kGoldClass = 32;
constexpr int kStoneClass = 8;
// Within 8 tiles of a Mill or Town Center.
const Core::Fixed32 kSheepAndForageReach(8);
// How long a town stays "under attack" after the last blow struck in it.
constexpr int64_t kUnderAttackMs = 10000;

int Seconds(int64_t ms) { return static_cast<int>(ms / 1000); }

bool Compare(int lhs, int32_t op, int32_t rhs) {
    return Lang::Compare(lhs, static_cast<RelOp>(op), rhs);
}

const Lang::Thing& ThingOf(int32_t index) { return Lang::Things()[index]; }

int Civ(const Turn& turn) {
    return turn.world != nullptr ? turn.world->seats[turn.player].civ
                                 : (turn.setup != nullptr ? turn.setup->players[turn.player].civ : 0);
}

const Seat* SeatOf(const Turn& turn, int p) {
    if (turn.world == nullptr || p < 0 || p >= Core::kMaxPlayers) return nullptr;
    return &turn.world->seats[p];
}

// The player arguments of facts: a seat, a rule variable's player, or a
// wildcard -- true for one such player (remembered in the rule's variable) or
// for every one of them (and at least one).
template <class F>
bool Players(Turn& turn, int32_t encoded, F&& holds) {
    if (encoded >= 0 && encoded < Core::kMaxPlayers) return holds(static_cast<int>(encoded));
    if (encoded >= Lang::kVariableBase) {
        const int p = turn.vars[encoded - Lang::kVariableBase];
        return p > 0 && holds(p);
    }
    if (encoded < Lang::kWildcardBase) return false;
    const auto who = static_cast<Lang::Who>(encoded - Lang::kWildcardBase);
    const bool every = Lang::IsEvery(who);
    int matched = 0;
    for (int p = 1; p < Core::kMaxPlayers; ++p) {
        if (!Matches(turn, encoded, p)) continue;
        ++matched;
        if (holds(p)) {
            if (!every) {
                turn.vars[static_cast<int>(who)] = p;
                return true;
            }
        } else if (every) {
            return false;
        }
    }
    return every && matched > 0;
}

// How many of a unit or building word a counts table holds.
int Count(const Turn& turn, const std::vector<int16_t>& table, int32_t thing) {
    const Things::Line line = Things::LineOf(ThingOf(thing), Civ(turn));
    if (line.count == 0) return 0;
    return CountOf(table, line.ids, line.count, line.member);
}

bool Owned(const Turn& turn) { return turn.registry != nullptr && turn.census != nullptr && turn.world != nullptr; }

// can-train, can-afford-unit, unit-available.
enum class Check { Available, Afford, Start };

bool UnitCheck(Turn& turn, int32_t thing, Check check, bool with_escrow) {
    if (!Owned(turn)) return false;
    const int slot = Things::Slot(*turn.registry, *turn.world, turn.player, ThingOf(thing), Civ(turn));
    if (slot < 0) return false;
    if (check == Check::Available) return true;
    const R::UnitRules* rules = ECS::Players::UnitOf(*turn.registry, turn.player, slot);
    if (!Things::Affords(*turn.census, *turn.mind, rules->costs, rules->cost_count, with_escrow)) return false;
    if (check == Check::Afford) return true;
    if (!ECS::Population::HasRoom(*turn.registry, turn.player, ECS::Population::CostOf(*rules))) return false;
    return Things::IdleAt(*turn.census, rules->train_loc) != entt::null;
}

bool BuildingCheck(Turn& turn, int32_t thing, Check check, bool with_escrow) {
    if (!Owned(turn)) return false;
    const int slot = Things::Slot(*turn.registry, *turn.world, turn.player, ThingOf(thing), Civ(turn));
    if (slot < 0) return false;
    if (check == Check::Available) return true;
    const R::UnitRules* rules = ECS::Players::UnitOf(*turn.registry, turn.player, slot);
    return Things::Affords(*turn.census, *turn.mind, rules->costs, rules->cost_count, with_escrow);
}

bool ResearchCheck(Turn& turn, int32_t thing, Check check, bool with_escrow) {
    if (!Owned(turn)) return false;
    const int tech = Things::Tech(*turn.registry, turn.player, ThingOf(thing), Civ(turn));
    if (tech < 0 || !ECS::Techs::Available(*turn.registry, turn.player, tech)) return false;
    if (check == Check::Available) return true;
    int count = 0;
    const R::Cost* costs = Things::TechCosts(*turn.registry, turn.player, tech, count);
    if (costs == nullptr || !Things::Affords(*turn.census, *turn.mind, costs, count, with_escrow)) return false;
    if (check == Check::Afford) return true;
    const R::TechRules* rules = R::Tech(tech);
    return rules != nullptr && Things::IdleAt(*turn.census, rules->location) != entt::null;
}

int Escrowed(const Turn& turn, int r) { return turn.mind->escrow[static_cast<size_t>(r)]; }

// How far, at least, one of the player's gatherers of a resource walks from
// its work to the nearest place that takes it, in whole tiles; -1 while
// nobody gathers it.
int DropsiteMinDistance(const Turn& turn, int resource) {
    if (!Owned(turn)) return -1;
    const entt::registry& registry = *turn.registry;
    const auto type = static_cast<Core::ResourceType>(resource);
    int best = -1;
    for (entt::entity v : turn.census->villagers) {
        const auto& worker = registry.get<ECS::Components::Worker>(v);
        if (worker.carry_type != type || !registry.valid(worker.target_resource)) continue;
        const auto* at = registry.try_get<ECS::Components::Transform>(worker.target_resource);
        if (at == nullptr) continue;
        Core::Fixed32 nearest(1 << 20);
        for (const Census::Site& site : turn.census->buildings) {
            if (!site.complete || !ECS::DropSites::Accepts(site.unit_id, type)) continue;
            const auto ground = ECS::Buildings::FootprintBounds(
                registry.get<ECS::Components::Transform>(site.entity).position, site.unit_id);
            nearest = std::min(nearest, ground.DistanceFrom(at->position));
        }
        if (nearest.ToInt() >= (1 << 19)) continue;
        const int d = nearest.ToInt();
        if (best < 0 || d < best) best = d;
    }
    return best;
}

// No forage bush and no sheep within eight tiles of any Mill or Town Center.
bool SheepAndForageTooFar(const Turn& turn) {
    if (!Owned(turn)) return false;
    const entt::registry& registry = *turn.registry;
    std::vector<ECS::Buildings::Bounds> sites;
    for (const Census::Site& site : turn.census->buildings) {
        if (!site.complete || !ECS::DropSites::Accepts(site.unit_id, Core::ResourceType::Food)) continue;
        const R::UnitRules* rules = ECS::Players::UnitOf(registry, turn.player, site.unit_id);
        // Farms take nothing in; the Mill and the Town Center do.
        if (rules == nullptr || rules->cls == 49) continue;
        sites.push_back(ECS::Buildings::FootprintBounds(
            registry.get<ECS::Components::Transform>(site.entity).position, site.unit_id));
    }
    if (sites.empty()) return true;
    auto near = [&](const Vector2D& at) {
        for (const auto& ground : sites) {
            if (ground.DistanceFrom(at) <= kSheepAndForageReach) return true;
        }
        return false;
    };
    auto stock = registry.view<const ECS::Components::Resource, const ECS::Components::Identity,
                               const ECS::Components::Transform>();
    for (auto e : stock) {
        const auto& r = stock.get<const ECS::Components::Resource>(e);
        if (r.type != Core::ResourceType::Food || r.current_amount <= 0) continue;
        const R::UnitRules* rules = R::Unit(stock.get<const ECS::Components::Identity>(e).unit_id);
        const bool forage = rules != nullptr && rules->cls == kForageClass;
        const bool sheep = ECS::Animals::Herdable(registry, e) ||
                           (registry.all_of<ECS::Components::Corpse>(e) && rules != nullptr &&
                            rules->cls == 58);
        if (!forage && !sheep) continue;
        if (sheep) {
            const auto* owner = registry.try_get<ECS::Components::Owner>(e);
            if (owner != nullptr && owner->player_id != turn.player && owner->player_id != Core::PLAYER_GAIA) continue;
        }
        if (near(stock.get<const ECS::Components::Transform>(e).position)) return false;
    }
    return true;
}

// Once true for a resource it stays true: a forage site seen for food, forest
// enough for wood (sn-minimum-forest-tiles trees uncovered), a mine for gold
// and stone.
bool ResourceFound(Turn& turn, int resource) {
    if (turn.mind->found[static_cast<size_t>(resource)]) return true;
    if (!Owned(turn)) return false;
    const entt::registry& registry = *turn.registry;
    const int forest = turn.mind->sn[static_cast<size_t>(Lang::SN::RequiredForestTiles)];
    int trees = 0;
    auto stock = registry.view<const ECS::Components::Resource, const ECS::Components::Identity,
                               const ECS::Components::Transform>();
    for (auto e : stock) {
        const auto& r = stock.get<const ECS::Components::Resource>(e);
        if (static_cast<int>(r.type) != resource || r.current_amount <= 0) continue;
        const R::UnitRules* rules = R::Unit(stock.get<const ECS::Components::Identity>(e).unit_id);
        if (rules == nullptr) continue;
        const int cls = rules->cls;
        const bool fits = (resource == 0 && cls == kForageClass) || (resource == 1 && cls == kTreeClass) ||
                          (resource == 2 && cls == kGoldClass) || (resource == 3 && cls == kStoneClass);
        if (!fits) continue;
        const auto& at = stock.get<const ECS::Components::Transform>(e).position;
        if (!ECS::Vision::Explored(registry, turn.player, Physics::TileOf(at.x), Physics::TileOf(at.y))) continue;
        if (resource == 1 && ++trees < forest) continue;
        turn.mind->found[static_cast<size_t>(resource)] = true;
        return true;
    }
    return false;
}

bool EnemyBuildingsInTown(Turn& turn) {
    if (!Owned(turn) || turn.census->town_center == entt::null) return false;
    const entt::registry& registry = *turn.registry;
    const Core::Fixed32 reach(turn.mind->sn[static_cast<size_t>(Lang::SN::MaximumTownSize)]);
    auto all = registry.view<const ECS::Components::Building, const ECS::Components::Owner,
                             const ECS::Components::Transform>();
    for (auto e : all) {
        const int p = all.get<const ECS::Components::Owner>(e).player_id;
        if (p == turn.player || p == Core::PLAYER_GAIA || p >= Core::kMaxPlayers) continue;
        if (StanceToward(turn, p) != Lang::Stance::Enemy) continue;
        const auto& at = all.get<const ECS::Components::Transform>(e).position;
        if (Core::FixedMath::Distance(at.x - turn.census->home.x, at.y - turn.census->home.y) > reach) continue;
        if (!ECS::Vision::Shows(registry, turn.player, e)) continue;
        return true;
    }
    return false;
}

int Stock(const Turn& turn, Core::ResourceType type) {
    return turn.census != nullptr ? turn.census->stock[static_cast<int>(type)] : 0;
}

int AttackSoldiers(const Turn& turn) {
    return turn.brain != nullptr ? static_cast<int>(turn.brain->attack.units.size()) : 0;
}
int AttackBoats(const Turn& turn) {
    return turn.brain != nullptr ? static_cast<int>(turn.brain->boat_attack.units.size()) : 0;
}

// The same letters in the same order, case and everything else aside: the
// lobby's "Black Forest" is the script's black-forest.
bool SameLetters(std::string_view a, std::string_view b) {
    auto letter = [](char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); };
    auto lower = [](char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; };
    size_t i = 0, j = 0;
    for (;;) {
        while (i < a.size() && !letter(a[i])) ++i;
        while (j < b.size() && !letter(b[j])) ++j;
        if (i == a.size() || j == b.size()) return i == a.size() && j == b.size();
        if (lower(a[i]) != lower(b[j])) return false;
        ++i;
        ++j;
    }
}

int StartingResources(const Core::GameSetup& setup) {
    switch (setup.resources) {
    case Core::StartingResources::Medium: return 1;
    case Core::StartingResources::High:   return 2;
    default:                               return 0;   // standard plays as low
    }
}

} // namespace

Lang::Stance StanceToward(const Turn& turn, int other) {
    if (turn.mind == nullptr || other < 0 || other >= Core::kMaxPlayers) return Lang::Stance::Enemy;
    return static_cast<Lang::Stance>(turn.mind->stance[static_cast<size_t>(other)]);
}

int ScoreOf(const Turn& turn, int player) {
    if (turn.registry == nullptr || player < 0 || player >= Core::kMaxPlayers) return 0;
    const ECS::Score::Sheet* sheet = ECS::Score::Of(*turn.registry, static_cast<Core::PlayerId>(player));
    return sheet != nullptr ? sheet->total : 0;
}

entt::entity OwnMarket(const Turn& turn) {
    if (turn.registry == nullptr) return entt::null;
    entt::entity found = entt::null;
    auto view = turn.registry->view<const ECS::Components::Building, const ECS::Components::Owner>();
    for (auto b : view) {
        if (view.get<const ECS::Components::Owner>(b).player_id != turn.player) continue;
        if (!ECS::Market::IsMarket(*turn.registry, b, turn.player)) continue;
        if (found == entt::null || b < found) found = b;
    }
    return found;
}

Lang::Stance StanceIn(const entt::registry& registry, Core::PlayerId from, Core::PlayerId to) {
    switch (ECS::Diplomacy::StanceOf(registry, from, to)) {
    case ECS::Diplomacy::Stance::Ally:    return Lang::Stance::Ally;
    case ECS::Diplomacy::Stance::Neutral: return Lang::Stance::Neutral;
    default:                              return Lang::Stance::Enemy;
    }
}

uint8_t DiplomacyStance(int stance) {
    switch (static_cast<Lang::Stance>(stance)) {
    case Lang::Stance::Ally:    return static_cast<uint8_t>(ECS::Diplomacy::Stance::Ally);
    case Lang::Stance::Neutral: return static_cast<uint8_t>(ECS::Diplomacy::Stance::Neutral);
    default:                    return static_cast<uint8_t>(ECS::Diplomacy::Stance::Enemy);
    }
}

Lang::Stance StanceOf(const Turn& turn, int other) {
    // How the other regards this player: the game's diplomacy. Without a
    // world (the language's own checks), a computer player's brain, or the
    // lobby's teams.
    if (turn.registry != nullptr && other >= 0 && other < Core::kMaxPlayers) {
        return StanceIn(*turn.registry, static_cast<Core::PlayerId>(other), turn.player);
    }
    if (turn.brains != nullptr) {
        for (const Brain& b : turn.brains->brains) {
            if (b.player == other) return static_cast<Lang::Stance>(b.mind.stance[turn.player]);
        }
    }
    const Seat* a = SeatOf(turn, other);
    const Seat* b = SeatOf(turn, turn.player);
    if (a != nullptr && b != nullptr && a->team != 0 && a->team == b->team) return Lang::Stance::Ally;
    return Lang::Stance::Enemy;
}

bool Matches(const Turn& turn, int encoded, int p) {
    if (encoded >= 0 && encoded < Core::kMaxPlayers) return p == encoded;
    if (encoded >= Lang::kVariableBase) return p == turn.vars[encoded - Lang::kVariableBase];
    if (encoded < Lang::kWildcardBase) return false;
    // Wildcards never match the player itself.
    if (p == turn.player) return false;
    const Seat* seat = SeatOf(turn, p);
    if (seat == nullptr || !seat->valid || !seat->in_game) return false;
    const Lang::Criteria c = Lang::CriteriaFor(static_cast<Lang::Who>(encoded - Lang::kWildcardBase));
    if (c.computer >= 0 && seat->computer != (c.computer == 1)) return false;
    if (c.stance >= 0 && StanceToward(turn, p) != static_cast<Lang::Stance>(c.stance)) return false;
    return true;
}

bool Test(Turn& turn, const Script::Expr& e) {
    const int32_t* a = e.args;
    Mind& mind = *turn.mind;
    const Census* census = turn.census;
    const World* world = turn.world;
    const Core::GameSetup* setup = turn.setup;
    const Seat* self = SeatOf(turn, turn.player);
    switch (static_cast<Fact>(e.id)) {
    case Fact::True:  return true;
    case Fact::False: return false;

    // --- The rules' own memory.
    case Fact::Goal:
        return a[0] > 0 && a[0] < Lang::kGoals && mind.goals[static_cast<size_t>(a[0])] == a[1];
    case Fact::SharedGoal:
        return turn.brains != nullptr && a[0] >= 0 && a[0] < Lang::kSharedGoals &&
               turn.brains->shared_goals[static_cast<size_t>(a[0])] == a[1];
    case Fact::StrategicNumber:
        return Compare(mind.sn[static_cast<size_t>(a[0])], a[1], a[2]);
    case Fact::TimerTriggered: {
        if (a[0] <= 0 || a[0] >= Lang::kTimers) return false;
        const Timer& t = mind.timers[static_cast<size_t>(a[0])];
        return t.on && turn.now_ms >= t.due_ms;
    }
    case Fact::RandomNumber:   return Compare(mind.random_number, a[0], a[1]);
    case Fact::Doctrine:       return mind.doctrine == a[0];
    case Fact::EscrowAmount:   return Compare(Escrowed(turn, a[0]), a[1], a[2]);
    case Fact::EventDetected:  return a[1] >= 0 && a[1] < Lang::kEvents && mind.events.test(static_cast<size_t>(a[1]));
    case Fact::TauntDetected:
        return a[1] >= 0 && a[1] < 256 &&
               Players(turn, a[0], [&](int p) { return mind.taunts[static_cast<size_t>(p)].test(static_cast<size_t>(a[1])); });

    // --- The lobby.
    case Fact::GameTime:          return Compare(Seconds(turn.now_ms), a[0], a[1]);
    case Fact::Difficulty:        return setup != nullptr && Compare(setup->difficulty, a[0], a[1]);
    case Fact::CheatsEnabled:     return false;
    case Fact::DeathMatchGame:    return setup != nullptr && setup->resources == Core::StartingResources::DeathMatch;
    case Fact::RegicideGame:      return setup != nullptr && setup->regicide;
    case Fact::PopulationCap:     return setup != nullptr && Compare(setup->population_limit, a[0], a[1]);
    case Fact::StartingAge:       return setup != nullptr && Compare(setup->starting_age, a[0], a[1]);
    case Fact::StartingResources: return setup != nullptr && Compare(StartingResources(*setup), a[0], a[1]);
    case Fact::VictoryCondition:
        return setup != nullptr && a[0] == (setup->victory == Core::VictoryKind::Conquest ? 1 : 0);
    case Fact::MapType:
        return setup != nullptr && SameLetters(setup->map_script, Lang::MapTypeName(a[0]));
    case Fact::MapSize: {
        if (setup == nullptr) return false;
        int index = -1;
        for (int i = 0; i < 6; ++i) {
            if (Core::kMapSizes[i].tiles == setup->map_size) index = i;
        }
        return index == a[0];
    }

    // --- The seats.
    case Fact::CivSelected:   return self != nullptr && self->civ == a[0];
    case Fact::PlayerNumber:  return Players(turn, a[0], [&](int p) { return p == turn.player; });
    case Fact::PlayerValid:   return Players(turn, a[0], [&](int p) { const Seat* s = SeatOf(turn, p); return s && s->valid; });
    case Fact::PlayerInGame:  return Players(turn, a[0], [&](int p) { const Seat* s = SeatOf(turn, p); return s && s->valid && s->in_game; });
    case Fact::PlayerComputer:return Players(turn, a[0], [&](int p) { const Seat* s = SeatOf(turn, p); return s && s->valid && s->computer; });
    case Fact::PlayerHuman:   return Players(turn, a[0], [&](int p) { const Seat* s = SeatOf(turn, p); return s && s->valid && !s->computer; });
    case Fact::PlayerResigned:return Players(turn, a[0], [&](int p) { const Seat* s = SeatOf(turn, p); return s && s->resigned; });
    case Fact::PlayersCiv:    return Players(turn, a[0], [&](int p) { const Seat* s = SeatOf(turn, p); return s && s->civ == a[1]; });
    case Fact::PlayersStance: return Players(turn, a[0], [&](int p) { return StanceOf(turn, p) == static_cast<Lang::Stance>(a[1]); });
    case Fact::StanceToward:  return Players(turn, a[0], [&](int p) { return StanceToward(turn, p) == static_cast<Lang::Stance>(a[1]); });
    case Fact::PlayersCurrentAge:
        return Players(turn, a[0], [&](int p) { const Seat* s = SeatOf(turn, p); return s && Compare(s->age, a[1], a[2]); });
    case Fact::PlayersCurrentAgeTime:
        return Players(turn, a[0], [&](int p) {
            const Seat* s = SeatOf(turn, p);
            return s && Compare(Seconds(turn.now_ms - s->age_since_ms), a[1], a[2]);
        });
    case Fact::PlayersPopulation:
        return Players(turn, a[0], [&](int p) { const Seat* s = SeatOf(turn, p); return s && Compare(s->population, a[1], a[2]); });
    case Fact::PlayersCivilianPopulation:
        return Players(turn, a[0], [&](int p) { const Seat* s = SeatOf(turn, p); return s && Compare(s->civilians, a[1], a[2]); });
    case Fact::PlayersMilitaryPopulation:
        return Players(turn, a[0], [&](int p) { const Seat* s = SeatOf(turn, p); return s && Compare(s->military, a[1], a[2]); });
    // The score, as the game keeps it (ecs/Score).
    case Fact::PlayersScore:
        return Players(turn, a[0], [&](int p) { return Compare(ScoreOf(turn, p), a[1], a[2]); });
    case Fact::CurrentScore:  return Compare(ScoreOf(turn, turn.player), a[0], a[1]);
    // Tribute the player has given this one, in all or since the rules
    // cleared their memory of it.
    case Fact::PlayersTribute:
    case Fact::PlayersTributeMemory: {
        if (turn.mind == nullptr || a[1] < 0 || a[1] > 3) return false;
        const auto& record = static_cast<Fact>(e.id) == Fact::PlayersTribute ? turn.mind->tribute
                                                                               : turn.mind->tribute_memory;
        return Players(turn, a[0], [&](int p) {
            return Compare(record[static_cast<size_t>(p)][static_cast<size_t>(a[1])], a[2], a[3]);
        });
    }

    case Fact::PlayersUnitCount:
    case Fact::CcPlayersUnitCount:
        return world != nullptr && Players(turn, a[0], [&](int p) {
            const Counts& c = static_cast<Fact>(e.id) == Fact::CcPlayersUnitCount ? world->all[p] : world->seen[p];
            return Compare(c.unit_count, a[1], a[2]);
        });
    case Fact::PlayersUnitTypeCount:
    case Fact::CcPlayersUnitTypeCount:
        return world != nullptr && Players(turn, a[0], [&](int p) {
            const Counts& c = static_cast<Fact>(e.id) == Fact::CcPlayersUnitTypeCount ? world->all[p] : world->seen[p];
            return Compare(Count(turn, c.units, a[1]), a[2], a[3]);
        });
    case Fact::PlayersBuildingCount:
    case Fact::CcPlayersBuildingCount:
        return world != nullptr && Players(turn, a[0], [&](int p) {
            const Counts& c = static_cast<Fact>(e.id) == Fact::CcPlayersBuildingCount ? world->all[p] : world->seen[p];
            return Compare(c.building_total, a[1], a[2]);
        });
    case Fact::PlayersBuildingTypeCount:
    case Fact::CcPlayersBuildingTypeCount:
        return world != nullptr && Players(turn, a[0], [&](int p) {
            const Counts& c = static_cast<Fact>(e.id) == Fact::CcPlayersBuildingTypeCount ? world->all[p] : world->seen[p];
            return Compare(Count(turn, c.buildings_total, a[1]), a[2], a[3]);
        });

    // --- The player's own things.
    case Fact::CurrentAge:     return census != nullptr && Compare(census->age, a[0], a[1]);
    case Fact::CurrentAgeTime: return Compare(Seconds(turn.now_ms - mind.age_since_ms), a[0], a[1]);
    case Fact::FoodAmount:     return census != nullptr && Compare(Stock(turn, Core::ResourceType::Food), a[0], a[1]);
    case Fact::WoodAmount:     return census != nullptr && Compare(Stock(turn, Core::ResourceType::Wood), a[0], a[1]);
    case Fact::GoldAmount:     return census != nullptr && Compare(Stock(turn, Core::ResourceType::Gold), a[0], a[1]);
    case Fact::StoneAmount:    return census != nullptr && Compare(Stock(turn, Core::ResourceType::Stone), a[0], a[1]);
    case Fact::Population:     return census != nullptr && Compare(census->population, a[0], a[1]);
    case Fact::HousingHeadroom:
        return census != nullptr && Compare(census->capacity - census->population, a[0], a[1]);
    case Fact::PopulationHeadroom:
        return census != nullptr && Compare(census->population_limit - census->capacity, a[0], a[1]);
    case Fact::CivilianPopulation:
        return census != nullptr && Compare(census->Civilians(), a[0], a[1]);
    case Fact::MilitaryPopulation:
        return self != nullptr && Compare(self->military, a[0], a[1]);
    case Fact::SoldierCount:
        return census != nullptr && Compare(static_cast<int>(census->soldiers.size()), a[0], a[1]);
    case Fact::WarboatCount:
        return census != nullptr && Compare(static_cast<int>(census->boats.size()), a[0], a[1]);
    case Fact::AttackSoldierCount: return Compare(AttackSoldiers(turn), a[0], a[1]);
    case Fact::AttackWarboatCount: return Compare(AttackBoats(turn), a[0], a[1]);
    case Fact::DefendSoldierCount:
        return census != nullptr &&
               Compare(static_cast<int>(census->military.size() - census->boats.size()) - AttackSoldiers(turn), a[0], a[1]);
    case Fact::DefendWarboatCount:
        return census != nullptr && Compare(static_cast<int>(census->boats.size()) - AttackBoats(turn), a[0], a[1]);
    case Fact::IdleFarmCount:  return census != nullptr && Compare(census->idle_farms, a[0], a[1]);
    case Fact::UnitCount:      return world != nullptr && Compare(world->all[turn.player].unit_count, a[0], a[1]);
    case Fact::UnitCountTotal: return world != nullptr && Compare(world->all[turn.player].unit_total, a[0], a[1]);
    case Fact::UnitTypeCount:
        return world != nullptr && Compare(Count(turn, world->all[turn.player].units, a[0]), a[1], a[2]);
    case Fact::UnitTypeCountTotal:
        return world != nullptr && Compare(Count(turn, world->all[turn.player].units_total, a[0]), a[1], a[2]);
    case Fact::BuildingCount:
        return world != nullptr && Compare(world->all[turn.player].building_count, a[0], a[1]);
    case Fact::BuildingCountTotal:
        return world != nullptr && Compare(world->all[turn.player].building_total, a[0], a[1]);
    case Fact::BuildingTypeCount:
        return world != nullptr && Compare(Count(turn, world->all[turn.player].buildings, a[0]), a[1], a[2]);
    case Fact::BuildingTypeCountTotal:
        return world != nullptr && Compare(Count(turn, world->all[turn.player].buildings_total, a[0]), a[1], a[2]);

    case Fact::UnitAvailable:       return UnitCheck(turn, a[0], Check::Available, false);
    case Fact::CanAffordUnit:       return UnitCheck(turn, a[0], Check::Afford, false);
    case Fact::CanTrain:            return UnitCheck(turn, a[0], Check::Start, false);
    case Fact::CanTrainWithEscrow:  return UnitCheck(turn, a[0], Check::Start, true);
    case Fact::BuildingAvailable:   return BuildingCheck(turn, a[0], Check::Available, false);
    case Fact::CanAffordBuilding:   return BuildingCheck(turn, a[0], Check::Afford, false);
    case Fact::CanBuild:            return BuildingCheck(turn, a[0], Check::Start, false);
    case Fact::CanBuildWithEscrow:  return BuildingCheck(turn, a[0], Check::Start, true);
    case Fact::ResearchAvailable:   return ResearchCheck(turn, a[0], Check::Available, false);
    case Fact::CanAffordResearch:   return ResearchCheck(turn, a[0], Check::Afford, false);
    case Fact::CanResearch:         return ResearchCheck(turn, a[0], Check::Start, false);
    case Fact::CanResearchWithEscrow: return ResearchCheck(turn, a[0], Check::Start, true);
    case Fact::ResearchCompleted: {
        if (!Owned(turn)) return false;
        const int tech = Things::Tech(*turn.registry, turn.player, ThingOf(a[0]), Civ(turn));
        return tech >= 0 && ECS::Techs::Researched(*turn.registry, turn.player, tech);
    }

    case Fact::DropsiteMinDistance:  return Compare(DropsiteMinDistance(turn, a[0]), a[1], a[2]);
    case Fact::SheepAndForageTooFar: return SheepAndForageTooFar(turn);
    case Fact::ResourceFound:        return ResourceFound(turn, a[0]);
    case Fact::EnemyBuildingsInTown: return EnemyBuildingsInTown(turn);
    case Fact::TownUnderAttack:
        return turn.brain != nullptr && turn.brain->town_struck_ms >= 0 &&
               turn.now_ms - turn.brain->town_struck_ms <= kUnderAttackMs;
    // Relics are not tracked.
    case Fact::EnemyCapturedRelics:  return false;

    // The Market: a lot of a commodity bought or sold at the price of the
    // moment, escrow aside.
    case Fact::CanBuyCommodity:
    case Fact::CanSellCommodity: {
        if (turn.registry == nullptr || turn.census == nullptr || OwnMarket(turn) == entt::null) return false;
        const int dat = Core::Rules::DatResource(static_cast<Core::ResourceType>(a[0]));
        if (static_cast<Fact>(e.id) == Fact::CanBuyCommodity) {
            return turn.census->stock[static_cast<int>(Core::ResourceType::Gold)] >=
                   ECS::Market::BuyPrice(*turn.registry, turn.player, dat);
        }
        return turn.census->stock[a[0]] >= ECS::Market::kLot;
    }
    case Fact::CommodityBuyingPrice:
    case Fact::CommoditySellingPrice: {
        if (turn.registry == nullptr) return false;
        const int dat = Core::Rules::DatResource(static_cast<Core::ResourceType>(a[0]));
        const int price = static_cast<Fact>(e.id) == Fact::CommodityBuyingPrice
                              ? ECS::Market::BuyPrice(*turn.registry, turn.player, dat)
                              : ECS::Market::SellPrice(*turn.registry, turn.player, dat);
        return Compare(price, a[1], a[2]);
    }
    // Walls laid by the rules: none can be built and none stands.
    case Fact::CanAffordCompleteWall:
    case Fact::CanBuildGate:
    case Fact::CanBuildGateWithEscrow:
    case Fact::CanBuildWall:
    case Fact::CanBuildWallWithEscrow: return false;
    case Fact::WallCompletedPercentage:
    case Fact::WallInvisiblePercentage: return Compare(0, a[1], a[2]);
    // The spy is never available.
    case Fact::CanSpy:
    case Fact::CanSpyWithEscrow:      return false;

    case Fact::COUNT: break;
    }
    return false;
}

} // namespace AI::Facts
