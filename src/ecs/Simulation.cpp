#include "Simulation.h"

#include <algorithm>
#include <iostream>

#include "ai/Brain.h"
#include "core/CommandQueue.h"
#include "core/ConfigManager.h"
#include "core/MapGenerator.h"
#include "core/RMSParser.h"
#include "core/Random.h"
#include "core/Rules.h"
#include "core/Types.h"
#include "ecs/Allegiance.h"
#include "ecs/Fog.h"
#include "ecs/Animals.h"
#include "ecs/Buildings.h"
#include "ecs/Duties.h"
#include "ecs/Monks.h"
#include "ecs/Relics.h"
#include "ecs/Spies.h"
#include "ecs/Events.h"
#include "ecs/Chat.h"
#include "ecs/Diplomacy.h"
#include "ecs/Flares.h"
#include "ecs/Garrisons.h"
#include "ecs/Market.h"
#include "ecs/Orders.h"
#include "ecs/Packing.h"
#include "ecs/Players.h"
#include "ecs/Production.h"
#include "ecs/Score.h"
#include "ecs/Techs.h"
#include "ecs/Trade.h"
#include "ecs/Triggers.h"
#include "ecs/Units.h"
#include "ecs/Victory.h"
#include "ecs/Vision.h"
#include "ecs/Walls.h"
#include "ecs/BuildingGeometry.h"
#include "ecs/components/Identity.h"
#include "ecs/components/Sight.h"
#include "ecs/components/Transform.h"
#include "pathfinding/ApproachPlanner.h"
#include "hal/GraphicCache.h"
#include "ecs/Combat.h"
#include "ecs/Missiles.h"
#include "ecs/Mortality.h"
#include "systems/AnimationSystem.h"
#include "ecs/Navigation.h"
#include "systems/MovementSystem.h"
#include "systems/WorkerSystem.h"

namespace ECS {

namespace {

// Which way each starting villager stands from the Town Center: spread round
// it rather than bunched at one door, the first three on the faces a player
// sees. Further villagers -- a civ bonus can give more -- cycle round again.
constexpr int kStartBearings = 4;
constexpr int kStartBearingX[kStartBearings] = {-1, 1, 1, -1};
constexpr int kStartBearingY[kStartBearings] = { 1, 1, -1, -1};

// Where a Regicide game's Castle goes: the nearest spot it may stand on, from
// this many tiles out from the Town Center's middle -- clear of the town's
// own doorstep -- to this many.
constexpr int kCastleRing = 7;
constexpr int kCastleReach = 16;

// How far out from the Town Center's wall a starting unit stands.
const Core::Fixed32 kStartStandoff = Core::Fixed32(3) / Core::Fixed32(2);
const Core::Fixed32 kStartClearance = Core::Fixed32(55) / Core::Fixed32(100);

// A player's starting units: the Town Center, its villagers and the scout.
void SpawnStartingUnits(entt::registry& registry, Physics::Grid& grid,
                        const Core::GameSetup& setup, Core::PlayerId player,
                        int cx, int cy) {
    auto& cfg = Core::ConfigManager::Get();
    const entt::entity tc = Buildings::Place(
        registry, grid, cfg.GetUnitIdByName(setup.starting_town_center), player,
        cx, cy, true);

    const Players::Player* seat = Players::Find(registry, player);
    const int villagers = seat != nullptr
        ? Players::Resource(*seat, Core::Rules::Res::StartingVillagers).ToInt() : 3;
    // Where each stands is asked of the approach planner, from a bearing round
    // the Town Center: a point beside its wall, at whatever sub-tile offset the
    // ground allows, clear of the villagers already placed.
    const auto ground = Buildings::FootprintBounds(
        registry.get<Components::Transform>(tc).position,
        registry.get<Components::Identity>(tc).unit_id);
    std::vector<Components::Vector2D> placed;
    for (int i = 0; i < villagers; ++i) {
        // Starting villagers alternate men and women; each is a separate base
        // unit with its own graphics.
        const int unit_id = cfg.GetUnitIdByName((i % 2 == 0) ? "VMBAS" : "VFBAS");
        const int b = i % kStartBearings;
        Pathfinding::ApproachRequest ask;
        ask.target = ground;
        ask.from = {Core::Fixed32(cx + kStartBearingX[b] * 8) + Core::Fixed32(i),
                    Core::Fixed32(cy + kStartBearingY[b] * 8)};
        ask.standoff = kStartStandoff;
        ask.clearance = kStartClearance;
        ask.taken = placed.empty() ? nullptr : placed.data();
        ask.taken_count = placed.size();
        const auto spot = Pathfinding::FindStandingSpot(grid, ask);
        if (!spot.found) continue;
        placed.push_back(spot.spot);
        Units::Spawn(registry, unit_id, player, spot.spot);
    }

    if (setup.starting_scout != nullptr) {
        Components::Vector2D at{};
        if (Buildings::Doorstep(registry, grid, tc, at)) {
            Units::Spawn(registry, cfg.GetUnitIdByName(setup.starting_scout), player, at);
        }
    }

    // Regicide: every player also starts with a King and a Castle; the
    // villagers are the civ's own starting number.
    if (setup.regicide) {
        const int castle = cfg.GetUnitIdByName(setup.regicide_castle);
        // The nearest spot to the Town Center a Castle may stand on, ring by
        // ring from a little way out, in a fixed order.
        bool built = false;
        for (int r = kCastleRing; r <= kCastleReach && !built; ++r) {
            for (int dy = -r; dy <= r && !built; ++dy) {
                for (int dx = -r; dx <= r && !built; ++dx) {
                    if (std::abs(dx) != r && std::abs(dy) != r) continue;
                    if (!Buildings::CanPlace(grid, castle, cx + dx, cy + dy)) continue;
                    Buildings::Place(registry, grid, castle, player, cx + dx, cy + dy, true);
                    built = true;
                }
            }
        }
        Components::Vector2D at{};
        if (Buildings::Doorstep(registry, grid, tc, at)) {
            Units::Spawn(registry, cfg.GetUnitIdByName(setup.regicide_king), player, at);
        }
    }
}

} // namespace

void Simulation::Init(const Core::GameSetup& game) {
    setup = game;
    // A random map game's computer players take the colours nobody chose; a
    // scenario's seats keep the file's.
    if (setup.scenario.empty()) Core::SettleColours(setup);
    // The lobby, for whoever needs its choices later -- the interface prints
    // the players' names from it. Never part of the world's digest.
    registry.ctx().insert_or_assign(setup);

    // The simulation's own random numbers, kept in the registry as world state
    // so any system can roll without a hidden global -- and so every machine
    // in a lockstep game rolls the same, from the same seed. Offset from the
    // map seed so drawing from one never shifts the other.
    registry.ctx().emplace<Core::Random>(
        static_cast<uint64_t>(setup.seed) ^ 0x9E3779B97F4A7C15ull);
    registry.ctx().insert_or_assign(
        Combat::HuntRandom{Core::Random(static_cast<uint64_t>(setup.seed) ^ 0xC2B2AE3D27D4EB4Full)});

    // Every seat's table, then the technologies each starts with -- its civ's
    // tech tree and bonuses, and the starting age -- before anything is placed,
    // so the starting units are made from the rules the player actually has.
    Players::Setup(registry, setup);
    Events::Setup(registry);
    Market::Setup(registry);
    Diplomacy::Setup(registry, setup);
    Chat::Setup(registry);
    Flares::Setup(registry);
    Fog::Setup(registry);
    AI::Setup(registry, setup);
    Victory::Setup(registry, setup);
    // Before the starting age is researched and anything placed: the score
    // counts both (ecs/Score).
    Score::Setup(registry);
    for (int seat = 1; seat < Core::kMaxPlayers; ++seat) {
        if (setup.players[seat].active) {
            // A scenario's seat has an age of its own.
            const int own = setup.players[seat].starting_age;
            Techs::Start(registry, static_cast<Core::PlayerId>(seat),
                         own >= 0 ? std::min(own, 3) : setup.starting_age,
                         setup.all_techs);
        }
    }
    // Spies is not to be had in Regicide or Death Match.
    Spies::Setup(registry, setup);
    Diplomacy::ApplyTeamBonuses(registry, setup);

    // A scenario's world is its file's, not a script's.
    if (!setup.scenario.empty()) {
        InitScenario();
        AI::UseScenario(registry, setup);
        // The history's first reading, of the scenario's own units.
        Score::Start(registry);
        return;
    }

    std::vector<Core::PlayerId> seats;
    for (int seat = 1; seat < Core::kMaxPlayers; ++seat) {
        if (setup.players[seat].active) seats.push_back(static_cast<Core::PlayerId>(seat));
    }

    Core::MapGenConfig map_cfg;
    // The lobby's map size, and the label the script sees for it.
    grid.Resize(setup.map_size, setup.map_size);
    map_cfg.width = setup.map_size;
    map_cfg.height = setup.map_size;
    std::vector<std::string> labels;
    for (const Core::MapSize& size : Core::kMapSizes) {
        if (size.tiles == setup.map_size) labels.emplace_back(size.script_label);
    }
    map_cfg.num_players = static_cast<int>(seats.size());
    for (Core::PlayerId seat : seats) {
        map_cfg.players.push_back(seat);
        map_cfg.teams.push_back(setup.players[seat].team);
    }
    map_cfg.forest_density = 0.35f;
    map_cfg.water_density = 0.15f;

    // Maps come from the random map scripts held in gamedata.drs; the
    // built-in generator stands in when the archive is missing.
    auto& cfg = Core::ConfigManager::Get();
    auto& archives = HAL::GraphicCache::Get();
    std::string script_text;
    const uint32_t script_id = cfg.RmsScriptId(setup.map_script);
    if (script_id != 0 && archives.ReadGameDataText(script_id, script_text)) {
        const Core::RMSScript script = Core::RMSParser::Parse(
            script_text, setup.seed,
            [](uint32_t file_id, std::string& out) {
                return HAL::GraphicCache::Get().ReadGameDataText(file_id, out);
            },
            setup.map_script, labels);
        player_starts = Core::MapGenerator::GenerateFromScript(
            grid, registry, setup.seed, script, map_cfg);
    } else {
        std::cerr << "[Simulation] Map script \"" << setup.map_script
                  << "\" unavailable; using the built-in generator." << std::endl;
        player_starts = Core::MapGenerator::GenerateProcedural(
            grid, registry, setup.seed, map_cfg);
    }

    Vision::Setup(registry, grid.GetWidth(), grid.GetHeight(), setup.reveal);
    // Whose gates open for whom: to begin with, each player's team-mates.
    Walls::SetAllies(registry, grid);
    for (size_t i = 0; i < player_starts.size() && i < seats.size(); ++i) {
        SpawnStartingUnits(registry, grid, setup, seats[i],
                           player_starts[i].first, player_starts[i].second);
    }

    // Allied Town Centers are visible from the start: each team-mate's start
    // is explored as far as its Town Center sees.
    for (size_t i = 0; i < player_starts.size() && i < seats.size(); ++i) {
        const Core::TeamId team = setup.players[seats[i]].team;
        if (team == 0) continue;
        const auto* tc = Players::UnitOf(registry, seats[i],
                                         cfg.GetUnitIdByName(setup.starting_town_center));
        const int sight = tc != nullptr ? tc->los.ToInt() : 0;
        for (size_t j = 0; j < seats.size(); ++j) {
            if (j == i || setup.players[seats[j]].team != team) continue;
            Vision::Explore(registry, seats[j], player_starts[i].first,
                            player_starts[i].second, sight);
        }
    }
    CalculateVision();
    // The history's first reading, of the starting units (ecs/Timeline).
    Score::Start(registry);
    std::cout << "[Simulation] World initialized." << std::endl;
}

void Simulation::Update(int32_t dt_ms) {
    current_tick++;
    Events::BeginTick(registry);
    Score::Advance(registry, dt_ms);
    // Where everyone stands, before anything plans a route or takes a step.
    Navigation::Survey(registry, grid);

    for (const auto& command : Core::CommandQueue::Get().PopAllCommands()) {
        Orders::Execute(registry, grid, command);
    }
    // The computer players decide, through the same orders.
    AI::Update(registry, grid, dt_ms);
    // Whose gates open for whom, as the stances now stand.
    Walls::SetAllies(registry, grid);

    // Cache previous positions for render interpolation.
    auto t_view = registry.view<Components::Transform>();
    for (auto entity : t_view) {
        auto& transform = t_view.get<Components::Transform>(entity);
        transform.previous_position = transform.position;
    }

    // Units at the end of a walk with waypoints left set off for the next.
    Orders::WalkOnToWaypoints(registry, grid);
    // Siege engines setting up or packing, and packed ones closing on a
    // target, before anything moves.
    Packing::Update(registry, grid, dt_ms);
    Systems::MovementSystem::Update(registry, grid, dt_ms);
    Systems::WorkerSystem::Update(registry, grid, dt_ms);
    Trade::Update(registry, grid, dt_ms);
    Garrisons::Update(registry, grid, dt_ms);
    Duties::Update(registry, grid, dt_ms);
    Monks::Update(registry, grid, dt_ms);
    Relics::Update(registry, grid, dt_ms);
    Animals::Update(registry, grid, dt_ms);
    Combat::Update(registry, grid, dt_ms);
    Missiles::Update(registry, grid, dt_ms);
    Mortality::Update(registry, grid, dt_ms);
    Buildings::Update(registry, grid);
    Walls::Update(registry, grid);
    // Spies' price follows the villagers it counts, before anything queues.
    Spies::Update(registry);
    Production::Update(registry, grid, dt_ms);
    Systems::AnimationSystem::Update(registry, dt_ms);
    // A scenario's triggers, before the victory they can declare is looked at;
    // nothing in a random map game.
    Triggers::Update(registry, grid, dt_ms);
    Victory::Update(registry, grid, dt_ms);

    CalculateVision();
    // What each player stops seeing, it remembers as it was.
    Fog::Update(registry);
    // What of Gaia's the players now see, the first of them takes.
    Allegiance::Capture(registry, grid);
    // Last, after everything else that tick.
    Score::Update(registry);
}

void Simulation::CalculateVision() {
    Vision::Update(registry);
}

} // namespace ECS
