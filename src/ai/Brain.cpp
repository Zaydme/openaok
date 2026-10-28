#include "ai/Brain.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <iostream>

#include "ai/Census.h"
#include "ai/Executor.h"
#include "ai/Facts.h"
#include "ai/Interpreter.h"
#include "ecs/Chat.h"
#include "ecs/Orders.h"
#include "ecs/Techs.h"
#include "ecs/Triggers.h"
#include "hal/GraphicCache.h"
#include "scenario/Scenario.h"

namespace AI {

namespace {

// How often a brain runs a rules pass and its executor, and the stagger
// between seats so they do not all think on the same tick.
constexpr int32_t kThinkMs = 1000;
// The name a scenario's own script is compiled under.
constexpr const char* kScenarioScript = "scenario";
constexpr const char* kMissingCampaignFile = "File open failed: scenario ";
constexpr int32_t kStaggerMs = 125;

// The AI files ("data\load\<name>.per") by gamedata.drs id, from 60001:
// 60026 is "randomgame", the computer player of a random map game, 60027 the
// constants it loads first, 60001 the rules it loads last.
constexpr const char* kFiles[] = {
    "ai petersen rules",       "petersen deathmatch",      "petersen upgrades",
    "petersen fishboat",       "petersen dip liar",        "petersen warboat",
    "petersen supplement",     "petersen difficulty loads", "petersen full tech",
    "petersen map loads",      "petersen civ loads",       "petersen tower",
    "petersen diplomacy",      "petersen castle",          "petersen resign",
    "petersen wonder",         "petersen market",          "petersen dip boomer",
    "petersen gather",         "petersen dip feeder",      "petersen groups",
    "petersen dip insult",     "petersen dip bully",       "petersen warboat island",
    "petersen rush",           "randomgame",               "petersen constants",
};
constexpr uint32_t kFirstFile = 60001;

std::string Lower(const std::string& s) {
    std::string out = s;
    for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

std::string Plain(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (std::isalpha(static_cast<unsigned char>(c))) out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    return out;
}

void Think(entt::registry& registry, Physics::Grid& grid, Brains& table, Brain& brain) {
    Census census = Take(registry, brain.player);
    // A player with nothing left thinks no more.
    if (census.villagers.empty() && census.military.empty() && census.buildings.empty() &&
        census.fishing_ships.empty() && census.scout == entt::null) {
        return;
    }
    Mind& mind = brain.mind;
    // Where the player stands toward each seat is the game's diplomacy, the
    // rules' own set-stance included.
    for (int p = 0; p < Core::kMaxPlayers; ++p) {
        mind.stance[static_cast<size_t>(p)] = static_cast<uint8_t>(
            p == brain.player ? Lang::Stance::Ally
                              : Facts::StanceIn(registry, brain.player, static_cast<Core::PlayerId>(p)));
    }
    // A scenario's "AI Script Goal" effects, each an event the rules detect
    // (event-detected trigger N).
    for (int id = 0; id < Lang::kEvents; ++id) {
        if (ECS::Triggers::TakeGoal(registry, brain.player, id)) mind.events.set(static_cast<size_t>(id));
    }
    // A taunt a person types (a chat line that is a number) reaches the
    // rules; the computer players' own are handed over as they are said.
    if (const ECS::Chat::Log* chat = ECS::Chat::Read(registry)) {
        const auto* setup = registry.ctx().find<Core::GameSetup>();
        uint64_t latest = brain.chat_tick;
        for (const ECS::Chat::Line& line : chat->lines) {
            if (line.tick <= brain.chat_tick) continue;
            latest = std::max(latest, line.tick);
            if (line.from == brain.player || !line.Heard(brain.player) || line.from >= Core::kMaxPlayers) continue;
            if (setup != nullptr && setup->players[line.from].computer) continue;
            int taunt = -1;
            const auto r = std::from_chars(line.text.data(), line.text.data() + line.text.size(), taunt);
            if (r.ec == std::errc() && r.ptr == line.text.data() + line.text.size() && taunt >= 0 && taunt < 256) {
                mind.taunts[line.from].set(static_cast<size_t>(taunt));
            }
        }
        brain.chat_tick = latest;
    }
    bool allied[Core::kMaxPlayers] = {};
    for (int p = 0; p < Core::kMaxPlayers; ++p) {
        allied[p] = mind.stance[static_cast<size_t>(p)] == static_cast<uint8_t>(Lang::Stance::Ally);
    }
    Take(registry, brain.player, allied, table.age_since.data(), table.world);
    for (const Brain& b : table.brains) table.world.seats[b.player].resigned = b.resigned;

    // Escrow: its share of whatever has come in since the last pass.
    for (int r = 0; r < 4; ++r) {
        const size_t k = static_cast<size_t>(r);
        if (mind.stock_seen) {
            const int income = census.stock[r] - mind.last_stock[k];
            if (income > 0) mind.escrow[k] += income * mind.escrow_percent[k] / 100;
        }
        mind.escrow[k] = std::clamp(mind.escrow[k], 0, census.stock[r]);
    }
    if (census.age != mind.age) {
        mind.age = census.age;
        mind.age_since_ms = table.now_ms;
    }

    Turn turn;
    turn.registry = &registry;
    turn.grid = &grid;
    turn.brains = &table;
    turn.brain = &brain;
    turn.program = &brain.program;
    turn.mind = &mind;
    turn.world = &table.world;
    turn.census = &census;
    turn.setup = registry.ctx().find<Core::GameSetup>();
    turn.player = brain.player;
    turn.now_ms = table.now_ms;
    Pass(turn);
    Executor::Run(turn);

    Executor::Refresh(turn);
    for (int r = 0; r < 4; ++r) mind.last_stock[static_cast<size_t>(r)] = census.stock[r];
    mind.stock_seen = true;
}

} // namespace

uint32_t FileId(const std::string& name) {
    const std::string want = Lower(name);
    for (uint32_t i = 0; i < sizeof(kFiles) / sizeof(kFiles[0]); ++i) {
        if (want == kFiles[i]) return kFirstFile + i;
    }
    return 0;
}

std::vector<std::string> Symbols(const Core::GameSetup& setup, Core::PlayerId seat) {
    std::vector<std::string> out;
    static const char* kDifficulty[] = {"DIFFICULTY-HARDEST", "DIFFICULTY-HARD", "DIFFICULTY-MODERATE",
                                        "DIFFICULTY-EASY", "DIFFICULTY-EASIEST"};
    out.emplace_back(kDifficulty[std::clamp(setup.difficulty, 0, 4)]);
    // The map, by its script's name: "Black Forest" is BLACK-FOREST-MAP.
    const std::string map = Plain(setup.map_script);
    for (int i = 0; i < Lang::kMapTypes; ++i) {
        const std::string type(Lang::MapTypeName(i));
        if (Plain(type) != map) continue;
        std::string symbol;
        for (char c : type) symbol.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
        out.push_back(symbol + "-MAP");
    }
    for (const Core::MapSize& size : Core::kMapSizes) {
        if (size.tiles != setup.map_size) continue;
        std::string symbol;
        for (const char* c = size.name; *c; ++c) symbol.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(*c))));
        out.push_back(symbol + "-MAP");
    }
    out.push_back("POPULATION-CAP-" + std::to_string(setup.population_limit));
    static const char* kAges[] = {"DARK-AGE-START", "FEUDAL-AGE-START", "CASTLE-AGE-START",
                                  "IMPERIAL-AGE-START", "POST-IMPERIAL-AGE-START"};
    out.emplace_back(kAges[std::clamp(setup.starting_age, 0, 4)]);
    if (setup.death_match) {
        out.emplace_back("DEATH-MATCH");
    } else {
        switch (setup.resources) {
        case Core::StartingResources::Medium: out.emplace_back("MEDIUM-RESOURCES-START"); break;
        case Core::StartingResources::High:   out.emplace_back("HIGH-RESOURCES-START"); break;
        default:                              out.emplace_back("LOW-RESOURCES-START"); break;
        }
    }
    if (setup.regicide) out.emplace_back("REGICIDE");
    out.emplace_back(setup.victory == Core::VictoryKind::Conquest ? "VICTORY-CONQUEST" : "VICTORY-STANDARD");
    if (setup.lock_teams) out.emplace_back("TEAMS-LOCKED");
    out.emplace_back(Lang::CivSymbol(setup.players[seat].civ));
    return out;
}

Script::Program Load(const Core::GameSetup& setup, Core::PlayerId seat, Core::Random& rng,
                     const std::function<bool(uint32_t, std::string&)>& read) {
    Script::Environment env;
    env.symbols = Symbols(setup, seat);
    env.constants = {{"my-player-number", seat}, {"my-civ", setup.players[seat].civ}};
    env.read = [&](const std::string& name, std::string& text) {
        const uint32_t id = FileId(name);
        return id != 0 && read(id, text);
    };
    env.percent = [&]() { return static_cast<int>(rng.NextInt(100)); };
    return Script::Compile("randomgame", env);
}

void UseScenario(entt::registry& registry, const Core::GameSetup& setup) {
    Brains* table = registry.ctx().find<Brains>();
    const Scenario::File* file = registry.ctx().find<Scenario::File>();
    if (table == nullptr || file == nullptr) return;
    for (Brain& brain : table->brains) {
        const std::string& script = file->players[brain.player].ai_script;
        if (script.empty()) continue;
        // The scenario's script is the master file; what it loads comes from
        // the game's own, as the random map's does.
        Script::Environment env;
        env.symbols = Symbols(setup, brain.player);
        env.constants = {{"my-player-number", brain.player}, {"my-civ", setup.players[brain.player].civ}};
        env.read = [&](const std::string& name, std::string& text) {
            if (name == kScenarioScript) {
                text = script;
                return true;
            }
            const uint32_t id = FileId(name);
            return id != 0 && HAL::GraphicCache::Get().ReadGameDataText(id, text);
        };
        env.percent = [&]() { return static_cast<int>(brain.rng.NextInt(100)); };
        brain.program = Script::Compile(kScenarioScript, env);
        // The campaigns' scripts load helper files named "scenario ..."
        // ("scenario difficulty", "scenario market", ...) that the game data
        // does not contain; those errors are dropped and the rest plays.
        auto& errors = brain.program.errors;
        errors.erase(std::remove_if(errors.begin(), errors.end(),
                                    [](const std::string& e) {
                                        return e.find(kMissingCampaignFile) != std::string::npos;
                                    }),
                     errors.end());
        if (!brain.program.errors.empty()) {
            std::cerr << "[AI] Player " << int{brain.player} << " (" << file->players[brain.player].ai_name
                      << "): " << brain.program.errors.size()
                      << " script errors, the first: " << brain.program.errors.front() << std::endl;
        }
        Reset(brain.mind, brain.program.rules.size());
    }
}

void Setup(entt::registry& registry, const Core::GameSetup& setup) {
    Brains table;
    const auto read = [](uint32_t id, std::string& out) {
        return HAL::GraphicCache::Get().ReadGameDataText(id, out);
    };
    for (int seat = 1; seat < Core::kMaxPlayers; ++seat) {
        if (!setup.players[seat].active || !setup.players[seat].computer) continue;
        Brain brain;
        brain.player = static_cast<Core::PlayerId>(seat);
        brain.rng = Core::Random((static_cast<uint64_t>(setup.seed) << 8) ^ (0xA1u + seat));
        brain.think_ms = kStaggerMs * seat;
        brain.program = Load(setup, brain.player, brain.rng, read);
        if (!brain.program.errors.empty()) {
            std::cerr << "[AI] Player " << seat << ": " << brain.program.errors.size()
                      << " script errors, the first: " << brain.program.errors.front() << std::endl;
        }
        Reset(brain.mind, brain.program.rules.size());
        // Where each player stands at the start: allied to its team, an
        // enemy to the rest. The scripts change it when teams are not locked.
        for (int p = 0; p < Core::kMaxPlayers; ++p) {
            const bool team = setup.players[p].team != 0 && setup.players[p].team == setup.players[seat].team;
            brain.mind.stance[static_cast<size_t>(p)] = static_cast<uint8_t>(
                p == seat || team ? Lang::Stance::Ally : Lang::Stance::Enemy);
        }
        table.brains.push_back(std::move(brain));
    }
    registry.ctx().insert_or_assign(std::move(table));
}

void Give(entt::registry& registry, Physics::Grid& grid, const Core::Command& command) {
    ECS::Orders::Execute(registry, grid, command);
}

void Update(entt::registry& registry, Physics::Grid& grid, int32_t dt_ms) {
    Brains* table = registry.ctx().find<Brains>();
    if (table == nullptr || table->brains.empty()) return;
    table->now_ms += dt_ms;
    for (int p = 1; p < Core::kMaxPlayers; ++p) {
        const int age = ECS::Techs::Age(registry, static_cast<Core::PlayerId>(p));
        if (age != table->ages[static_cast<size_t>(p)]) {
            table->ages[static_cast<size_t>(p)] = age;
            table->age_since[static_cast<size_t>(p)] = table->now_ms;
        }
    }
    for (Brain& brain : table->brains) {
        brain.think_ms -= dt_ms;
        if (brain.think_ms > 0) continue;
        brain.think_ms += kThinkMs;
        if (brain.resigned) continue;
        Think(registry, grid, *table, brain);
    }
}

} // namespace AI
