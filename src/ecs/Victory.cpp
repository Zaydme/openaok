#include "ecs/Victory.h"

#include <algorithm>
#include <vector>

#include "core/ConfigManager.h"
#include "core/Rules.h"
#include "ecs/Buildings.h"
#include "ecs/Combat.h"
#include "ecs/Diplomacy.h"
#include "ecs/Events.h"
#include "ecs/Mortality.h"
#include "ecs/Score.h"
#include "ecs/BuildingGeometry.h"
#include "ecs/Timeline.h"
#include "ecs/Vision.h"
#include "ecs/components/Building.h"
#include "ecs/components/Health.h"
#include "ecs/components/Garrison.h"
#include "ecs/components/Identity.h"
#include "ecs/components/Owner.h"
#include "ecs/components/Relic.h"
#include "ecs/components/Transform.h"

namespace ECS::Victory {

namespace {

namespace R = Core::Rules;

// A year of game time.
constexpr int32_t kYearMs = 3000;
// How often standing is taken.
constexpr int32_t kCheckMs = 1000;
// What does not keep a player alive, by dat class: trade carts and cogs,
// transport and fishing ships, farms, fish traps, gates and walls (the
// lobby's LANGUAGE.DLL 30568). Relics are Gaia's.
constexpr int kExempt[] = {
    27,   // wall
    39,   // gate
    2,    // trade cog
    19,   // trade cart
    20,   // transport ship
    21,   // fishing ship
    49,   // farm, fish trap
};

bool Exempt(int cls) {
    for (int c : kExempt) {
        if (c == cls) return true;
    }
    return false;
}

// Takes everything `player` has off the map, in a fixed order so every
// machine does the same: its buildings come down and its units die.
void Eliminate(entt::registry& registry, Physics::Grid& grid, Core::PlayerId player) {
    std::vector<entt::entity> owned;
    auto view = registry.view<Components::Owner>();
    for (auto e : view) {
        if (view.get<Components::Owner>(e).player_id == player) owned.push_back(e);
    }
    std::sort(owned.begin(), owned.end());
    for (auto e : owned) {
        if (!registry.valid(e)) continue;
        if (registry.all_of<Components::Building>(e)) {
            Buildings::Destroy(registry, grid, e);
        } else if (const auto* health = registry.try_get<Components::Health>(e);
                   health != nullptr && health->current_hp > 0) {
            Mortality::Kill(registry, grid, e);
        }
    }
}

// Whether two players win together: allied each way, both with allied
// victory chosen -- team-mates from the lobby, or allies made in play.
bool OneSide(const entt::registry& registry, Core::PlayerId a, Core::PlayerId b) {
    return Diplomacy::SharesVictory(registry, a, b);
}

Core::PlayerId OwnerOf(const entt::registry& registry, entt::entity e) {
    const auto* owner = registry.try_get<Components::Owner>(e);
    return owner != nullptr ? owner->player_id : Core::PLAYER_GAIA;
}

// Who still has something standing: a living unit or building, the exempt
// ones aside -- a unit sheltering in a building or aboard a transport among
// them, out of the world as it is.
uint16_t Standing(const entt::registry& registry) {
    uint16_t alive = 0;
    auto all = registry.view<const Components::Identity, const Components::Owner>();
    for (auto e : all) {
        const Core::PlayerId p = all.get<const Components::Owner>(e).player_id;
        if (p == Core::PLAYER_GAIA || ((alive >> p) & 1u)) continue;
        const auto* aboard = registry.try_get<Components::Garrisoned>(e);
        if (aboard != nullptr) {
            if (!registry.valid(aboard->building) || !Combat::Alive(registry, aboard->building)) continue;
        } else if (!Combat::Alive(registry, e)) {
            continue;
        }
        const auto* rules = R::Unit(all.get<const Components::Identity>(e).unit_id);
        if (rules != nullptr && Exempt(rules->cls)) continue;
        alive |= static_cast<uint16_t>(1u << p);
    }
    return alive;
}

// The player whose finished Wonder has stood longest, or none.
Core::PlayerId WonderHolder(const entt::registry& registry, const State& state) {
    if (state.wonder_id < 0) return 0;
    auto buildings = registry.view<const Components::Building, const Components::Identity>();
    for (auto b : buildings) {
        if (buildings.get<const Components::Identity>(b).unit_id != state.wonder_id) continue;
        if (buildings.get<const Components::Building>(b).state != Components::BuildingState::Complete) continue;
        if (!Combat::Alive(registry, b)) continue;
        const Core::PlayerId p = OwnerOf(registry, b);
        if (p != Core::PLAYER_GAIA && !state.outcome.Defeated(p)) return p;
    }
    return 0;
}

// The player whose side holds every relic on the map in its monasteries, or
// none.
Core::PlayerId RelicHolder(const entt::registry& registry, const State& state) {
    if (state.relic_id < 0) return 0;
    int total = 0;
    auto relics = registry.view<const Components::Identity>();
    for (auto e : relics) {
        if (relics.get<const Components::Identity>(e).unit_id == state.relic_id) ++total;
    }
    if (total == 0) return 0;
    int held = 0;
    Core::PlayerId holder = 0;
    auto shrines = registry.view<const Components::RelicShrine>();
    for (auto s : shrines) {
        const auto& shrine = shrines.get<const Components::RelicShrine>(s);
        if (shrine.relics.empty()) continue;
        const Core::PlayerId p = OwnerOf(registry, s);
        if (holder != 0 && !OneSide(registry, holder, p)) return 0;
        if (holder == 0) holder = p;
        held += static_cast<int>(shrine.relics.size());
    }
    return held == total ? holder : 0;
}

// Counts down a Wonder or the relics: started when someone takes it, reset
// when it changes hands or is lost. True once it has run out.
bool CountDown(Core::PlayerId holder, Core::PlayerId& counted, int32_t& left, int years, int32_t dt) {
    if (holder == 0) {
        counted = 0;
        left = 0;
        return false;
    }
    if (holder != counted) {
        counted = holder;
        left = years * kYearMs;
    }
    left -= dt;
    return left <= 0;
}

// Out of the game other than by resigning: the bit, the news, and the event
// on the player's history.
void Out(entt::registry& registry, State& state, Core::PlayerId player) {
    state.outcome.defeated |= static_cast<uint16_t>(1u << player);
    Events::Emit(registry, {.kind = Events::Kind::Defeated, .player = player});
    const auto* score = registry.ctx().find<Score::Table>();
    Timeline::Defeated(registry, player, score != nullptr ? score->clock_ms : 0);
}

void Win(entt::registry& registry, State& state, Core::PlayerId side_of) {
    state.outcome.over = true;
    for (int p = 1; p < Core::kMaxPlayers; ++p) {
        if (!state.setup.players[p].active) continue;
        const auto player = static_cast<Core::PlayerId>(p);
        if (OneSide(registry, side_of, player) && !state.outcome.Defeated(player)) {
            state.outcome.winners |= static_cast<uint16_t>(1u << p);
            Events::Emit(registry, {.kind = Events::Kind::Won, .player = player});
        } else if (!state.outcome.Defeated(player)) {
            Out(registry, state, player);
        }
    }
}

} // namespace

void Setup(entt::registry& registry, const Core::GameSetup& setup) {
    State state;
    state.setup = setup;
    state.check_ms = kCheckMs;
    auto& cfg = Core::ConfigManager::Get();
    state.wonder_id = cfg.GetUnitIdByName("WNDR");
    state.relic_id = cfg.GetUnitIdByName("RELIC");
    state.king_id = cfg.GetUnitIdByName("KINGX");
    registry.ctx().insert_or_assign(state);
}

bool HasKing(const entt::registry& registry, Core::PlayerId player) {
    const State* state = registry.ctx().find<State>();
    if (state == nullptr || state->king_id < 0) return false;
    auto kings = registry.view<const Components::Identity, const Components::Owner>();
    for (auto k : kings) {
        if (kings.get<const Components::Identity>(k).unit_id != state->king_id) continue;
        if (kings.get<const Components::Owner>(k).player_id != player) continue;
        if (const auto* inside = registry.try_get<Components::Garrisoned>(k)) {
            if (registry.valid(inside->building) && Combat::Alive(registry, inside->building)) return true;
            continue;
        }
        if (Combat::Alive(registry, k)) return true;
    }
    return false;
}

void Begun(entt::registry& registry, entt::entity site) {
    const State* state = registry.ctx().find<State>();
    const auto* identity = registry.try_get<Components::Identity>(site);
    const auto* at = registry.try_get<Components::Transform>(site);
    if (state == nullptr || identity == nullptr || at == nullptr || identity->unit_id != state->wonder_id) {
        return;
    }
    Buildings::ForEachFootprintTile(identity->unit_id, Physics::TileOf(at->position.x),
                                    Physics::TileOf(at->position.y), [&](int x, int y) {
        for (Core::PlayerId player = 1; player < Core::kMaxPlayers; ++player) {
            Vision::Explore(registry, player, x, y, 0);
        }
    });
}

void Win(entt::registry& registry, Core::PlayerId player) {
    State* state = registry.ctx().find<State>();
    if (state == nullptr || state->outcome.over || player == Core::PLAYER_GAIA ||
        player >= Core::kMaxPlayers || state->outcome.Defeated(player)) {
        return;
    }
    Win(registry, *state, player);
}

void Update(entt::registry& registry, Physics::Grid& grid, int32_t dt_ms) {
    State* state = registry.ctx().find<State>();
    if (state == nullptr || state->outcome.over) return;

    // Regicide: a player without its King is out, and everything it has goes.
    if (state->setup.regicide) {
        for (int p = 1; p < Core::kMaxPlayers; ++p) {
            const auto player = static_cast<Core::PlayerId>(p);
            if (!state->setup.players[p].active || state->outcome.Defeated(player)) continue;
            if (HasKing(registry, player)) continue;
            Out(registry, *state, player);
            Eliminate(registry, grid, player);
        }
    }

    // Wonders and relics count every tick; standing is taken once a second.
    if (state->setup.victory == Core::VictoryKind::Standard) {
        const Core::PlayerId wonder = WonderHolder(registry, *state);
        if (CountDown(wonder, state->outcome.wonder_holder, state->outcome.wonder_ms,
                      state->setup.wonder_years, dt_ms)) {
            Win(registry, *state, wonder);
            return;
        }
        const Core::PlayerId relics = RelicHolder(registry, *state);
        if (CountDown(relics, state->outcome.relic_holder, state->outcome.relic_ms,
                      state->setup.relic_years, dt_ms)) {
            Win(registry, *state, relics);
            return;
        }
    }

    state->check_ms -= dt_ms;
    if (state->check_ms > 0) return;
    state->check_ms = kCheckMs;
    const uint16_t alive = Standing(registry);
    std::vector<Core::PlayerId> left;
    for (int p = 1; p < Core::kMaxPlayers; ++p) {
        if (!state->setup.players[p].active) continue;
        const auto player = static_cast<Core::PlayerId>(p);
        if (state->outcome.Defeated(player)) continue;
        if (!((alive >> p) & 1u)) {
            Out(registry, *state, player);
            continue;
        }
        left.push_back(player);
    }
    // One side left -- every player still standing winning with every other,
    // allied each way with allied victory -- has won. None at all: nobody has.
    if (left.empty()) {
        state->outcome.over = true;
        return;
    }
    bool one_side = true;
    for (size_t i = 0; i < left.size() && one_side; ++i) {
        for (size_t j = i + 1; j < left.size() && one_side; ++j) {
            one_side = OneSide(registry, left[i], left[j]);
        }
    }
    // A game one player began alone is not won by being the last standing;
    // its triggers end it.
    int seated = 0;
    for (int p = 1; p < Core::kMaxPlayers; ++p) seated += state->setup.players[p].active ? 1 : 0;
    if (one_side && seated > 1) Win(registry, *state, left.back());
}

void Resign(entt::registry& registry, Physics::Grid& grid, Core::PlayerId player) {
    State* state = registry.ctx().find<State>();
    if (state == nullptr || player == Core::PLAYER_GAIA || player >= Core::kMaxPlayers) return;
    if (state->outcome.Defeated(player)) return;
    state->outcome.defeated |= static_cast<uint16_t>(1u << player);
    Events::Emit(registry, {.kind = Events::Kind::Defeated, .player = player});
    // Its buildings come down and its units die.
    Eliminate(registry, grid, player);
}

void Declare(entt::registry& registry, uint16_t winners) {
    State* state = registry.ctx().find<State>();
    if (state == nullptr || state->outcome.over || winners == 0) return;
    state->outcome.over = true;
    for (int p = 1; p < Core::kMaxPlayers; ++p) {
        if (!state->setup.players[p].active) continue;
        const auto player = static_cast<Core::PlayerId>(p);
        if (state->outcome.Defeated(player)) continue;
        bool wins = false;
        for (int q = 1; q < Core::kMaxPlayers && !wins; ++q) {
            wins = ((winners >> q) & 1u) != 0 && OneSide(registry, static_cast<Core::PlayerId>(q), player);
        }
        if (wins) {
            state->outcome.winners |= static_cast<uint16_t>(1u << p);
            Events::Emit(registry, {.kind = Events::Kind::Won, .player = player});
        } else {
            Out(registry, *state, player);
        }
    }
}

const Outcome* Read(const entt::registry& registry) {
    const State* state = registry.ctx().find<State>();
    return state != nullptr ? &state->outcome : nullptr;
}

} // namespace ECS::Victory
