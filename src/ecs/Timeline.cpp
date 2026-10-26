#include "ecs/Timeline.h"

#include <algorithm>

#include "ecs/Players.h"
#include "ecs/Score.h"

namespace ECS::Timeline {

namespace {

namespace S = Score::Res;
using Core::Fixed32;

Table* TableOf(entt::registry& registry) { return registry.ctx().find<Table>(); }

// A resource's whole part, truncated toward zero.
int32_t Whole(const Players::Player& player, int dat_resource) {
    const Fixed32 v = Players::Resource(player, dat_resource);
    return v.value >= 0 ? v.ToInt() : -((Fixed32(0) - v).ToInt());
}

// The reading: the two populations as 16-bit numbers, both zero should their
// sum be below zero.
void Read(Record& record, const Players::Player& player) {
    const int32_t civilian = Whole(player, S::CivilianPopulation);
    const int32_t military = Whole(player, S::MilitaryPopulation);
    Sample sample;
    if (Players::Resource(player, S::CivilianPopulation) +
            Players::Resource(player, S::MilitaryPopulation) >= Fixed32(0)) {
        sample.civilian = static_cast<int16_t>(civilian);
        sample.military = static_cast<int16_t>(military);
    }
    record.samples.push_back(sample);
}

// A battle kept pushes out the weaker of the two battles before it, or goes
// itself. The new one is at the head of the list.
void KeepThreeBattles(Record& record) {
    int battles = 0;
    for (const Event& e : record.events) battles += e.kind == kBattle ? 1 : 0;
    record.events.front().c = 1;
    if (battles <= kBattlesKept) return;
    const Fixed32 weight = record.events.front().b;
    Fixed32 widest(0);
    int weakest = -1;
    int seen = 0;
    for (int i = 0; i < static_cast<int>(record.events.size()) && seen < kBattlesKept; ++i) {
        const Event& e = record.events[static_cast<size_t>(i)];
        if (e.kind != kBattle) continue;
        ++seen;
        if (e.b < weight && weight - e.b > widest) {
            widest = weight - e.b;
            weakest = i;
        }
    }
    record.events.erase(record.events.begin() + (weakest >= 0 ? weakest : 0));
}

// The enemy a battle was against: the player whose units and buildings this
// one destroyed the most value of since it began, divided by what it owned at
// the last reading -- the first of equals, nobody if none. Owning nothing,
// the enemy is the first that lost anything.
int32_t Enemy(const Record& record, const Players::Player& player) {
    const int64_t owned = static_cast<int64_t>(record.last.unit_value_owned) +
                          record.last.building_value_owned;
    int32_t enemy = 0;
    int64_t most = 0;
    for (int j = 0; j < 8; ++j) {
        const int64_t lost = static_cast<int64_t>(Whole(player, S::KillValueFirst + j)) -
                             record.last.kill_value[j] +
                             Whole(player, S::RazingValueFirst + j) -
                             record.last.razing_value_of[j];
        if (lost <= 0) continue;
        if (owned <= 0) {
            if (enemy == 0) enemy = j + 1;
            continue;
        }
        if (lost > most) {
            most = lost;
            enemy = j + 1;
        }
    }
    return enemy;
}

// After the reading: the battle, then the counters remembered.
void Fight(entt::registry& registry, Core::PlayerId id, Record& record,
           const Players::Player& player, int32_t time_ms) {
    Last& last = record.last;
    const int32_t killed = Whole(player, S::MilitaryValue) - last.military_value;
    const int32_t razed = Whole(player, S::RazingValue) - last.razing_value;
    const int32_t lost = Whole(player, S::ValueLost) - last.value_lost;
    const int32_t buildings = Whole(player, S::BuildingValueLost) - last.building_value_lost;
    const int64_t owned = static_cast<int64_t>(last.unit_value_owned) + last.building_value_owned;
    // Destroyed over owned, 0.12 or more.
    bool fighting = false;
    Fixed32 share(0);
    if (owned > 0) {
        fighting = 25 * (static_cast<int64_t>(killed) + razed) >= 3 * owned;
        share = Fixed32(static_cast<int>(static_cast<int64_t>(killed) + razed + lost + buildings)) /
                Fixed32(static_cast<int>(owned));
    }
    if (fighting) {
        record.fighting = true;
        record.weight = record.weight + share;
    } else if (record.fighting) {
        // A battle that weighed 0.15 or more.
        if (record.weight * Fixed32(20) >= Fixed32(3)) {
            Note(registry, id, kBattle, time_ms, Enemy(record, player), record.weight);
        }
        record.fighting = false;
        record.weight = Fixed32(0);
    }
    last.razings = Whole(player, S::Razings);
    last.razing_value = Whole(player, S::RazingValue);
    last.buildings_lost = Whole(player, S::BuildingsLost);
    last.building_value_lost = Whole(player, S::BuildingValueLost);
    last.units_killed = Whole(player, S::UnitsKilled);
    last.military_value = Whole(player, S::MilitaryValue);
    last.units_lost = Whole(player, S::UnitsLost);
    last.value_lost = Whole(player, S::ValueLost);
    last.unit_value_owned = Whole(player, S::UnitValueOwned);
    last.building_value_owned = Whole(player, S::BuildingValueOwned);
    // Against each enemy, only between battles: a battle is weighed from the
    // reading before it began.
    if (!record.fighting) {
        for (int j = 0; j < 8; ++j) {
            last.kill_value[j] = Whole(player, S::KillValueFirst + j);
            last.razing_value_of[j] = Whole(player, S::RazingValueFirst + j);
        }
    }
}

} // namespace

void Setup(entt::registry& registry) { registry.ctx().insert_or_assign(Table{}); }

void Start(entt::registry& registry) {
    Table* table = TableOf(registry);
    if (table == nullptr) return;
    for (int p = 1; p < Core::kMaxPlayers; ++p) {
        if (const Players::Player* player = Players::Find(registry, static_cast<Core::PlayerId>(p))) {
            Read(table->records[p], *player);
        }
    }
}

void Second(entt::registry& registry) {
    Table* table = TableOf(registry);
    const auto* score = registry.ctx().find<Score::Table>();
    if (table == nullptr || score == nullptr) return;
    for (int p = 1; p < Core::kMaxPlayers; ++p) {
        const auto id = static_cast<Core::PlayerId>(p);
        const Players::Player* player = Players::Find(registry, id);
        if (player == nullptr) continue;
        Record& record = table->records[p];
        if (++record.seconds < kSampleSeconds) continue;
        record.seconds -= kSampleSeconds;
        Read(record, *player);
        Fight(registry, id, record, *player, score->clock_ms);
    }
}

void Note(entt::registry& registry, Core::PlayerId player, Kind kind, int32_t time_ms, int32_t a,
          Core::Fixed32 b, int32_t c) {
    Table* table = TableOf(registry);
    if (table == nullptr || player == Core::PLAYER_GAIA || player >= Core::kMaxPlayers) return;
    Record& record = table->records[player];
    if (record.samples.empty()) return;
    Event e;
    e.kind = kind;
    e.sample = static_cast<int32_t>(record.samples.size());
    e.time_ms = time_ms;
    e.a = a;
    e.b = b;
    e.c = c;
    record.events.insert(record.events.begin(), e);
    if (kind == kBattle) KeepThreeBattles(record);
}

void Defeated(entt::registry& registry, Core::PlayerId player, int32_t time_ms) {
    if (player == Core::PLAYER_GAIA || player >= Core::kMaxPlayers) return;
    // Who killed the most: each other player's count of player-N units killed,
    // N being that other player's own seat rather than the defeated one's. When
    // none counts anything, the event names player 1 with nothing.
    int32_t killer = 1;
    int32_t most = 0;
    for (int q = 1; q < Core::kMaxPlayers && q <= 8; ++q) {
        if (q == player) continue;
        const Players::Player* other = Players::Find(registry, static_cast<Core::PlayerId>(q));
        if (other == nullptr) continue;
        const int32_t kills = Whole(*other, S::KilledFirst + q - 1);
        if (kills > most) {
            most = kills;
            killer = q;
        }
    }
    Note(registry, player, kDefeated, time_ms, killer, Fixed32(most));
}

const Record* Of(const entt::registry& registry, Core::PlayerId player) {
    const Table* table = registry.ctx().find<Table>();
    if (table == nullptr || player >= Core::kMaxPlayers) return nullptr;
    return &table->records[player];
}

} // namespace ECS::Timeline
