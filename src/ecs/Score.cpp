#include "ecs/Score.h"

#include <algorithm>
#include <array>

#include "core/Rules.h"
#include "ecs/Diplomacy.h"
#include "ecs/Timeline.h"
#include "ecs/Victory.h"
#include "ecs/Vision.h"
#include "ecs/components/Building.h"
#include "ecs/components/Identity.h"
#include "ecs/components/Mortal.h"
#include "ecs/components/Owner.h"
#include "ecs/components/PartOf.h"
#include "ecs/components/Projectile.h"

namespace ECS::Score {

namespace {

namespace R = Core::Rules;

// The units the score singles out by id.
constexpr int kCastle = 82;
constexpr int kWonder = 276;
// Walls are never razed for the score.
constexpr int kWalls[] = {72, 117, 155};
// What a razed "building" counts as a unit killed: the trebuchets and the
// two unique siege engines, packed and unpacked.
constexpr int kSiegeBuildings[] = {42, 331, 682, 730, 683, 729};
// The technologies whose completion is an age.
constexpr int kFeudalAge = 101;
constexpr int kCastleAge = 102;
constexpr int kImperialAge = 103;
// The dat's "research cost modifier": the Chinese discount (effects 349-351).
constexpr int kResearchCostModifier = 85;

// Which dat classes count as civilian (r37) and which as military (r40).
constexpr int kCivilianClasses[] = {2, 4, 19, 20, 21};
constexpr int kMilitaryClasses[] = {0,  6,  12, 13, 22, 23, 24, 26, 28, 35, 36,
                                    44, 45, 46, 47, 50, 51, 53, 54, 55, 56, 57};

template <size_t N>
bool OneOf(const int (&list)[N], int value) {
    return std::find(std::begin(list), std::end(list), value) != std::end(list);
}

// Per dat class: 1 civilian, 2 military.
constexpr int kClassTableSize = 256;
const std::array<uint8_t, kClassTableSize>& ClassKinds() {
    static const std::array<uint8_t, kClassTableSize> kinds = [] {
        std::array<uint8_t, kClassTableSize> k{};
        for (const int c : kCivilianClasses) k[static_cast<size_t>(c)] |= 1;
        for (const int c : kMilitaryClasses) k[static_cast<size_t>(c)] |= 2;
        return k;
    }();
    return kinds;
}

// A resource's whole part, truncated toward zero.
int32_t Trunc(Core::Fixed32 v) {
    return v.value >= 0 ? v.ToInt() : -((Core::Fixed32(0) - v).ToInt());
}

int32_t Whole(const Players::Player& player, int dat_resource) {
    return Trunc(Players::Resource(player, dat_resource));
}

// One scoring element: truncated resource over the divisor, never below zero,
// times the multiplier.
int32_t Ratio(int32_t whole, int32_t divisor, int32_t multiplier) {
    const int32_t q = divisor != 0 ? whole / divisor : 0;
    return std::max(0, q) * multiplier;
}

// trunc(amount x m), m being the dat's single-precision float. The float is
// rebuilt exactly, in integers, as the nearest 24-bit mantissa over a power
// of two to the modifier's thousandths, and the exact product truncated:
// 0.9 is 15099494 / 2^24, a hair under, so 50 x 0.9 comes to 44, not 45.
int TimesFloat(int amount, Core::Fixed32 m) {
    const int64_t per_mille = (m * Core::Fixed32(1000) + Core::Fixed32(1) / Core::Fixed32(2)).ToInt();
    if (per_mille <= 0) return 0;
    int shift = 0;
    while ((per_mille << shift) < (int64_t{1} << 23) * 1000 && shift < 40) ++shift;
    const int64_t scaled = per_mille << shift;
    int64_t mantissa = scaled / 1000;
    const int64_t rest = scaled % 1000;
    if (rest * 2 > 1000 || (rest * 2 == 1000 && (mantissa & 1) != 0)) ++mantissa;   // to nearest, ties even
    const __int128_t product = static_cast<__int128_t>(amount) * mantissa;
    const __int128_t whole = (product >= 0 ? product : -product) >> shift;
    return static_cast<int>(product >= 0 ? whole : -whole);
}

void Add(Players::Player* player, int dat_resource, int amount) {
    if (player != nullptr) Players::AddResource(*player, dat_resource, Core::Fixed32(amount));
}

// Takes `amount` off a resource and keeps it from going below zero (r98,
// r164 and r165).
void Take(Players::Player* player, int dat_resource, int amount) {
    if (player == nullptr) return;
    Players::AddResource(*player, dat_resource, Core::Fixed32(-amount));
    if (Players::Resource(*player, dat_resource).value < 0) {
        Players::SetResource(*player, dat_resource, Core::Fixed32(0));
    }
}

// A per-player slot (r69.., r110.., ...) for players 1 to 8; none for Gaia.
void AddFor(Players::Player* player, int first, int other, int amount) {
    if (other >= 1 && other <= 8) Add(player, first + other - 1, amount);
}

Core::PlayerId OwnerOf(const entt::registry& registry, entt::entity e) {
    const auto* owner = registry.try_get<Components::Owner>(e);
    return owner != nullptr ? owner->player_id : Core::PLAYER_GAIA;
}

Table* TableOf(entt::registry& registry) {
    return registry.ctx().find<Table>();
}

// The percent of the map each player has explored (explored tiles over all
// tiles, times 100), or 100 for all when the lobby reveals the map.
void CountExplored(entt::registry& registry) {
    const auto* map = registry.ctx().find<Vision::Map>();
    if (map == nullptr || map->width <= 0 || map->height <= 0) return;
    const int tiles = map->width * map->height;
    int explored[Core::kMaxPlayers] = {};
    if (map->reveal == Core::Reveal::Normal) {
        for (uint16_t bits : map->explored) {
            for (int p = 1; p < Core::kMaxPlayers; ++p) explored[p] += (bits >> p) & 1;
        }
    }
    for (int p = 1; p < Core::kMaxPlayers; ++p) {
        Players::Player* player = Players::Find(registry, static_cast<Core::PlayerId>(p));
        if (player == nullptr) continue;
        // Exact in 32.32: a whole percent comes out whole, so truncating it
        // gives the floor.
        const Core::Fixed32 percent =
            map->reveal != Core::Reveal::Normal
                ? Core::Fixed32(100)
                : Core::Fixed32(explored[p] * 100) / Core::Fixed32(tiles);
        Players::SetResource(*player, Res::MapExplored, percent);
    }
}

// Every civilian and military unit in the world, by owner, counted from what
// exists. A unit still dying exists, as does one sheltering in a building.
void CountPopulation(entt::registry& registry) {
    int civilian[Core::kMaxPlayers] = {};
    int military[Core::kMaxPlayers] = {};
    const auto& kinds = ClassKinds();
    auto units = registry.view<const Components::Identity, const Components::Owner>();
    for (auto e : units) {
        // Gaia's -- the trees, mines and animals -- are no one's population.
        const Core::PlayerId owner = units.get<const Components::Owner>(e).player_id;
        if (owner == Core::PLAYER_GAIA || owner >= Core::kMaxPlayers) continue;
        if (registry.any_of<Components::Corpse, Components::Projectile>(e)) continue;
        const R::UnitRules* rules =
            Players::UnitOf(registry, owner, units.get<const Components::Identity>(e).unit_id);
        if (rules == nullptr || rules->cls < 0 || rules->cls >= kClassTableSize) continue;
        const uint8_t kind = kinds[static_cast<size_t>(rules->cls)];
        if (kind & 1) ++civilian[owner];
        if (kind & 2) ++military[owner];
    }
    for (int p = 1; p < Core::kMaxPlayers; ++p) {
        Players::Player* player = Players::Find(registry, static_cast<Core::PlayerId>(p));
        if (player == nullptr) continue;
        Players::SetResource(*player, Res::CivilianPopulation, Core::Fixed32(civilian[p]));
        Players::SetResource(*player, Res::MilitaryPopulation, Core::Fixed32(military[p]));
    }
}

void Fill(Sheet& s, const Players::Player& p) {
    // The seven scoring elements.
    s.military = Ratio(Whole(p, Res::MilitaryValue), 5, 1);
    s.economy = Ratio(Whole(p, R::Res::Food), 10, 1) + Ratio(Whole(p, R::Res::Wood), 10, 1) +
                Ratio(Whole(p, R::Res::Stone), 10, 1) + Ratio(Whole(p, R::Res::Gold), 10, 1) +
                Ratio(Whole(p, Res::ValueOwned), 5, 1) +
                Ratio(Whole(p, Res::TributeGiven) - Whole(p, Res::TributeReceived), 10, 1);
    s.technology = Ratio(Whole(p, Res::ResearchValue), 5, 1) +
                   Ratio(Whole(p, Res::MapExplored), 1, 10);
    s.society = Ratio(Whole(p, Res::MonumentValue), 5, 1);
    s.total = s.military + s.economy + s.technology + s.society;

    s.units_killed = Whole(p, Res::UnitsKilled);
    s.units_lost = Whole(p, Res::UnitsLost);
    s.buildings_razed = Whole(p, Res::Razings);
    s.buildings_lost = Whole(p, Res::BuildingsLost);
    s.units_converted = Whole(p, Res::Conversions);
    // The largest army and the villager high keep the most they have seen.
    s.largest_army = std::max(s.largest_army, Whole(p, Res::MilitaryPopulation));

    s.food_collected = Whole(p, Res::FoodCollected);
    s.wood_collected = Whole(p, Res::FoodCollected + 1);
    s.stone_collected = Whole(p, Res::FoodCollected + 2);
    s.gold_collected = Whole(p, Res::FoodCollected + 3);
    s.trade_profit = Whole(p, Res::TradeIncome);
    s.tribute_sent = Whole(p, Res::TributeSent);
    s.tribute_received = Whole(p, Res::TributeReceived);

    s.map_explored = Whole(p, Res::MapExplored);
    s.research_count = Whole(p, Res::TechnologyCount);

    s.wonders = Whole(p, Res::WondersBuilt);
    s.castles = Whole(p, Res::CastlesBuilt);
    s.relics_captured = Whole(p, Res::RelicsCaptured);
    s.relic_gold = Whole(p, Res::RelicIncome);
    s.villager_high = std::max(s.villager_high, Whole(p, Res::CivilianPopulation));
    s.survived = Whole(p, Res::SurvivalToFinish) != 0 ? 1 : 0;
}

} // namespace

void Setup(entt::registry& registry) {
    registry.ctx().insert_or_assign(Table{});
    Timeline::Setup(registry);
    for (int p = 1; p < Core::kMaxPlayers; ++p) {
        if (Players::Player* player = Players::Find(registry, static_cast<Core::PlayerId>(p))) {
            Players::SetResource(*player, Res::SurvivalToFinish, Core::Fixed32(1));
        }
    }
}

void Start(entt::registry& registry) {
    // The populations as the units placed make them, then the history's
    // first reading.
    CountPopulation(registry);
    Timeline::Start(registry);
}

void Advance(entt::registry& registry, int32_t dt_ms) {
    if (Table* table = TableOf(registry)) {
        table->clock_ms += dt_ms;
        table->explore_ms += dt_ms;
    }
}

void Update(entt::registry& registry) {
    Table* table = TableOf(registry);
    if (table == nullptr) return;
    CountPopulation(registry);
    // Once a game second: the history, every 25th, then the map explored.
    if (table->explore_ms >= 1000) {
        table->explore_ms %= 1000;
        Timeline::Second(registry);
        CountExplored(registry);
    }
    const Victory::Outcome* outcome = Victory::Read(registry);
    for (int p = 1; p < Core::kMaxPlayers; ++p) {
        const auto id = static_cast<Core::PlayerId>(p);
        Players::Player* player = Players::Find(registry, id);
        if (player == nullptr) continue;
        // A player out of the game did not survive to the finish.
        if (outcome != nullptr && outcome->Defeated(id)) {
            Players::SetResource(*player, Res::SurvivalToFinish, Core::Fixed32(0));
        }
        Fill(table->sheets[p], *player);
    }
}

const Sheet* Of(const entt::registry& registry, Core::PlayerId player) {
    const auto* table = registry.ctx().find<Table>();
    if (table == nullptr || player >= Core::kMaxPlayers ||
        Players::Find(registry, player) == nullptr) {
        return nullptr;
    }
    return &table->sheets[player];
}

int32_t TeamAverage(const entt::registry& registry, Core::PlayerId player) {
    const Sheet* own = Of(registry, player);
    if (own == nullptr) return 0;
    int64_t sum = own->total;
    int64_t count = 1;
    // With allied victory set, averaged with each ally that also has it set;
    // the stance both ways must be "ally".
    if (Diplomacy::AlliedVictory(registry, player)) {
        for (int q = 1; q < Core::kMaxPlayers; ++q) {
            const auto other = static_cast<Core::PlayerId>(q);
            if (other == player) continue;
            const Sheet* theirs = Of(registry, other);
            if (theirs == nullptr) continue;
            if (Diplomacy::StanceOf(registry, player, other) != Diplomacy::Stance::Ally ||
                Diplomacy::StanceOf(registry, other, player) != Diplomacy::Stance::Ally ||
                !Diplomacy::AlliedVictory(registry, other)) {
                continue;
            }
            sum += theirs->total;
            ++count;
        }
    }
    return static_cast<int32_t>(sum / count);
}

int Value(const entt::registry& registry, Core::PlayerId owner, int unit_id) {
    const R::UnitRules* rules = Players::UnitOf(registry, owner, unit_id);
    if (rules == nullptr) return 0;
    int value = 0;
    for (int i = 0; i < rules->cost_count && i < R::kMaxCosts; ++i) {
        if (rules->costs[i].resource >= 0 && rules->costs[i].paid) value += rules->costs[i].amount;
    }
    return value;
}

void UnitCreated(entt::registry& registry, Core::PlayerId owner, int unit_id) {
    Players::Player* player = Players::Find(registry, owner);
    const R::UnitRules* rules = Players::UnitOf(registry, owner, unit_id);
    // Only what is not a building; a building counts when finished.
    if (player == nullptr || rules == nullptr || rules->type == R::UnitType::Building) return;
    const int value = Value(registry, owner, unit_id);
    Add(player, Res::ValueOwned, value);
    Add(player, Res::UnitValueOwned, value);
}

void BuildingFinished(entt::registry& registry, Core::PlayerId owner, int unit_id) {
    Players::Player* player = Players::Find(registry, owner);
    const R::UnitRules* rules = Players::UnitOf(registry, owner, unit_id);
    if (player == nullptr || rules == nullptr) return;
    const int value = Value(registry, owner, unit_id);
    if (unit_id == kCastle || unit_id == kWonder) {
        Add(player, unit_id == kCastle ? Res::CastlesBuilt : Res::WondersBuilt, 1);
        Add(player, Res::MonumentValue, value);
        // On the history, the owner noted.
        if (const Table* table = TableOf(registry)) {
            Timeline::Note(registry, owner,
                           unit_id == kCastle ? Timeline::kCastleBuilt : Timeline::kWonderBuilt,
                           table->clock_ms, owner);
        }
        return;
    }
    // A building that is replaced by the one it stacks when finished is not
    // counted; the stacked one is. Annex pieces are not buildings here at all.
    if (rules->stack_unit >= 0) return;
    Add(player, Res::ValueOwned, value);
    Add(player, Res::BuildingValueOwned, value);
}

void Removed(entt::registry& registry, entt::entity entity) {
    const auto* identity = registry.try_get<Components::Identity>(entity);
    if (identity == nullptr || registry.all_of<Components::PartOf>(entity)) return;
    const Core::PlayerId owner = OwnerOf(registry, entity);
    Players::Player* player = Players::Find(registry, owner);
    if (player == nullptr) return;
    const int value = Value(registry, owner, identity->unit_id);
    if (const auto* building = registry.try_get<Components::Building>(entity)) {
        // Only from alive to dying: a site never finished was never counted.
        if (building->state != Components::BuildingState::Complete) return;
        Take(player, Res::ValueOwned, value);
        if (identity->unit_id != kCastle && identity->unit_id != kWonder) {
            Take(player, Res::BuildingValueOwned, value);
        }
        return;
    }
    Take(player, Res::ValueOwned, value);
    Take(player, Res::UnitValueOwned, value);
}

void Killed(entt::registry& registry, entt::entity victim, int attacker_player) {
    const auto* identity = registry.try_get<Components::Identity>(victim);
    if (identity == nullptr || registry.all_of<Components::PartOf>(victim)) return;
    const Core::PlayerId owner = OwnerOf(registry, victim);
    if (owner == Core::PLAYER_GAIA) return;
    const int unit_id = identity->unit_id;
    const bool building = registry.all_of<Components::Building>(victim);
    if (building && OneOf(kWalls, unit_id)) return;
    const bool as_unit = !building || OneOf(kSiegeBuildings, unit_id);
    const int value = Value(registry, owner, unit_id);

    Players::Player* loser = Players::Find(registry, owner);
    Players::Player* winner =
        attacker_player >= 0 && attacker_player < Core::kMaxPlayers
            ? Players::Find(registry, static_cast<Core::PlayerId>(attacker_player))
            : nullptr;
    // A Castle or a Wonder brought down goes on its destroyer's history, its
    // owner noted.
    if (building && winner != nullptr && (unit_id == kCastle || unit_id == kWonder)) {
        if (const Table* table = TableOf(registry)) {
            Timeline::Note(registry, static_cast<Core::PlayerId>(attacker_player),
                           unit_id == kCastle ? Timeline::kCastleDestroyed
                                              : Timeline::kWonderDestroyed,
                           table->clock_ms, owner);
        }
    }

    if (as_unit) {
        Add(winner, Res::UnitsKilled, 1);
        AddFor(winner, Res::KilledFirst, owner, 1);
        Add(loser, Res::UnitsLost, 1);
        AddFor(loser, Res::UnitsLostToFirst, attacker_player, 1);
    } else {
        Add(winner, Res::Razings, 1);
        AddFor(winner, Res::RazingsFirst, owner, 1);
        Add(loser, Res::BuildingsLost, 1);
        AddFor(loser, Res::BuildingsLostToFirst, attacker_player, 1);
    }
    Add(winner, Res::MilitaryValue, value);
    Add(loser, Res::ValueLost, value);
    if (building) {
        Add(winner, Res::RazingValue, value);
        Add(loser, Res::BuildingValueLost, value);
    }
    AddFor(winner, as_unit ? Res::KillValueFirst : Res::RazingValueFirst, owner, value);
}

void Converted(entt::registry& registry, entt::entity target, Core::PlayerId converter) {
    const auto* identity = registry.try_get<Components::Identity>(target);
    if (identity == nullptr) return;
    const Core::PlayerId owner = OwnerOf(registry, target);
    const int value = Value(registry, owner, identity->unit_id);
    Players::Player* loser = Players::Find(registry, owner);
    Add(loser, Res::UnitsLost, 1);
    Take(loser, Res::ValueOwned, value);
    Players::Player* winner = Players::Find(registry, converter);
    Add(winner, Res::Conversions, 1);
    Add(winner, Res::MilitaryValue, value);
}

void Researched(entt::registry& registry, Core::PlayerId player_id, int tech_id) {
    Players::Player* player = Players::Find(registry, player_id);
    const R::TechRules* tech = R::Tech(tech_id);
    if (player == nullptr || tech == nullptr ||
        tech_id >= static_cast<int>(player->techs.size())) {
        return;
    }
    // Only a technology researched at a building, from its button.
    if (tech->location >= 0 && tech->button > 0) {
        const Players::TechStatus& status = player->techs[tech_id];
        // The price: each paid slot of the player's own, times the research
        // cost modifier (r85) when that is above nothing -- but never for the
        // three ages.
        const bool age = tech_id >= kFeudalAge && tech_id <= kImperialAge;
        const Core::Fixed32 modifier = Players::Resource(*player, kResearchCostModifier);
        int price = 0;
        for (int i = 0; i < status.cost_count && i < R::kMaxCosts; ++i) {
            if (status.costs[i].resource < 0 || !status.costs[i].paid) continue;
            const int amount = status.costs[i].amount;
            price += !age && modifier.value > 0 ? TimesFloat(amount, modifier) : amount;
        }
        Add(player, Res::TechnologyCount, 1);
        Add(player, Res::ResearchValue, price);
    }
    Table* table = TableOf(registry);
    if (table == nullptr || player_id >= Core::kMaxPlayers) return;
    Sheet& sheet = table->sheets[player_id];
    if (tech_id == kFeudalAge) sheet.feudal_ms = table->clock_ms;
    if (tech_id == kCastleAge) sheet.castle_ms = table->clock_ms;
    if (tech_id == kImperialAge) sheet.imperial_ms = table->clock_ms;
    // And on the history: the ages are events 0 to 2.
    if (tech_id >= kFeudalAge && tech_id <= kImperialAge) {
        Timeline::Note(registry, player_id, static_cast<Timeline::Kind>(tech_id - kFeudalAge),
                       table->clock_ms);
    }
}

void Collected(Players::Player& player, int dat_resource, Core::Fixed32 amount) {
    if (dat_resource < R::Res::Food || dat_resource > R::Res::Gold) return;
    Players::AddResource(player, Res::FoodCollected + dat_resource, amount);
}

void RelicGold(Players::Player& player, Core::Fixed32 amount) {
    Collected(player, R::Res::Gold, amount);
    Players::AddResource(player, Res::RelicIncome, amount);
}

void Tributed(entt::registry& registry, Core::PlayerId from, Core::PlayerId to, int amount) {
    Add(Players::Find(registry, from), Res::TributeGiven, amount);
    Players::Player* recipient = Players::Find(registry, to);
    Add(recipient, Res::TributeReceived, amount);
    AddFor(recipient, Res::TributeFromFirst, from, amount);
}

} // namespace ECS::Score
