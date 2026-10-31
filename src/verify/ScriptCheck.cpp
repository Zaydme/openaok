#include "verify/Harness.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "ai/Brain.h"
#include "ai/Census.h"
#include "ai/Interpreter.h"
#include "ai/Language.h"
#include "ai/Mind.h"
#include "ai/Script.h"
#include "ai/World.h"
#include "core/ConfigManager.h"
#include "core/GameSetup.h"
#include "core/Rules.h"
#include "ecs/Buildings.h"
#include "ecs/Chat.h"
#include "ecs/Diplomacy.h"
#include "ecs/Orders.h"
#include "ecs/Players.h"
#include "ecs/ScenarioWorld.h"
#include "ecs/Simulation.h"
#include "ecs/components/Building.h"
#include "ecs/components/Identity.h"
#include "ecs/components/Owner.h"
#include "ecs/components/ProductionQueue.h"
#include "ecs/components/Transform.h"
#include "hal/GraphicCache.h"

namespace Verify {

namespace {

using Files = std::map<std::string, std::string>;

AI::Script::Program CompileFiles(const Files& files, const std::vector<std::string>& symbols, int roll) {
    AI::Script::Environment env;
    env.symbols = symbols;
    env.read = [&files](const std::string& name, std::string& text) {
        const auto it = files.find(name);
        if (it == files.end()) return false;
        text = it->second;
        return true;
    };
    env.percent = [roll]() { return roll; };
    return AI::Script::Compile("main", env);
}

int ConstantOf(const AI::Script::Program& program, const char* name) {
    int v = -999;
    AI::Script::Constant(program, name, v);
    return v;
}

// A pass with no world: only the rules' own memory, the lobby and the seats.
constexpr int kTicksPerSecond = 1000 / 16;

struct Bench {
    AI::Script::Program program;
    AI::Mind mind;
    AI::World world;
    AI::Brains brains;
    AI::Brain brain;
    Core::GameSetup setup;
    AI::Turn turn;

    explicit Bench(AI::Script::Program p) : program(std::move(p)) {
        AI::Reset(mind, program.rules.size());
        // Seat 1 is the computer player thinking; 2 a human it fights; 3 a
        // computer on its side.
        setup.difficulty = 1;   // hard
        for (int s = 1; s <= 3; ++s) {
            world.seats[s].valid = world.seats[s].in_game = true;
            world.seats[s].computer = s != 2;
            setup.players[s].active = true;
        }
        mind.stance[1] = mind.stance[3] = static_cast<uint8_t>(AI::Lang::Stance::Ally);
        mind.stance[2] = static_cast<uint8_t>(AI::Lang::Stance::Enemy);
        brain.player = 1;
        turn.brains = &brains;
        turn.brain = &brain;
        turn.program = &program;
        turn.mind = &mind;
        turn.world = &world;
        turn.setup = &setup;
        turn.player = 1;
    }
    void Pass(int64_t ms) {
        turn.now_ms = ms;
        AI::Pass(turn);
    }
    int Goal(int g) const { return mind.goals[static_cast<size_t>(g)]; }
};

// The reader: conditional loading nested, constants, load, load-random's
// one die for all its files, and an unknown fact or word refused with its
// error code (ERR2003, ERR2005) while the rest loads.
const char* CheckReader() {
    Files files;
    files["main"] =
        "(defconst seven 7)\n"
        "#load-if-defined ALPHA\n"
        "(defconst branch 1)\n"
        "  #load-if-not-defined BETA\n"
        "  (defconst nested 2)\n"
        "  #else\n"
        "  (defconst nested 3)\n"
        "  #end-if\n"
        "#else\n"
        "(defconst branch 9)\n"
        "(load \"never\")\n"
        "#end-if\n"
        "(load-random 30 \"low\" 50 \"high\" \"fallback\")\n"
        "(defrule (true) => (set-goal 1 seven) (chat-local-to-self \"a; b\") (disable-self))\n"
        "(load \"inner\")\n"
        "(defrule (bogus-fact) => (do-nothing))\n"
        "(defrule (goal 1 not-a-constant) => (do-nothing))\n";
    files["inner"] = "; a comment ( with a parenthesis\n(defrule (goal 1 7) => (set-goal 2 branch))\n";
    files["low"] = "(defconst picked 1)";
    files["high"] = "(defconst picked 2)";
    files["fallback"] = "(defconst picked 3)";
    files["never"] = "(defconst never 1)";

    const AI::Script::Program p = CompileFiles(files, {"ALPHA"}, 45);
    if (ConstantOf(p, "branch") != 1) return "#load-if-defined took the wrong branch";
    if (ConstantOf(p, "nested") != 2) return "a nested #load-if-not-defined took the wrong branch";
    if (ConstantOf(p, "never") != -999) return "a file loaded inside a branch not taken";
    if (ConstantOf(p, "picked") != 2) return "load-random loaded the wrong file for its roll";
    if (p.rules.size() != 2) return "the reader kept the wrong rules";
    int keywords = 0, identifiers = 0;
    for (const std::string& e : p.errors) {
        keywords += e.find("ERR2003") != std::string::npos ? 1 : 0;
        identifiers += e.find("ERR2005") != std::string::npos ? 1 : 0;
    }
    if (keywords != 1 || identifiers != 1 || p.errors.size() != 2) {
        return "an unknown fact or word was not refused, or more was";
    }
    if (p.strings.size() != 1 || p.strings[0] != "a; b") return "a semicolon in a string was read as a comment";
    if (ConstantOf(CompileFiles(files, {"ALPHA"}, 90), "picked") != 3) return "load-random missed its default file";
    if (ConstantOf(CompileFiles(files, {"ALPHA"}, 10), "picked") != 1) return "load-random missed its first file";
    const AI::Script::Program other = CompileFiles(files, {"ALPHA", "BETA"}, 45);
    if (ConstantOf(other, "nested") != 3) return "#else under #load-if-not-defined was not taken";
    if (ConstantOf(CompileFiles(files, {}, 45), "branch") != 9) return "#else was not taken";

    // The first rule's actions, then the second's, in one pass.
    Bench bench(p);
    bench.Pass(0);
    if (bench.Goal(1) != 7 || bench.Goal(2) != 1) return "a rule did not see the goal the rule before it set";
    return nullptr;
}

// Passes: rule order, disable-self, timers, the logic, the defaults, the
// lobby, wildcards and the rule variable they fill.
const char* CheckPasses() {
    Files files;
    files["main"] =
        "(defrule (true) => (set-goal 6 1) (disable-self))\n"
        "(defrule (goal 6 1) => (set-goal 6 2) (set-goal 7 9))\n"
        "(defrule (true) => (enable-timer 1 10) (disable-self))\n"
        "(defrule (timer-triggered 1) => (set-goal 8 1) (disable-timer 1))\n"
        "(defrule (or (goal 10 1) (goal 10 2)) => (set-goal 11 1))\n"
        "(defrule (nor (goal 10 1) (goal 10 2)) => (set-goal 12 1))\n"
        "(defrule (and (goal 10 1) (not (goal 10 2))) => (set-goal 13 1))\n"
        "(defrule (nand (goal 10 1) (goal 10 1)) => (set-goal 14 1))\n"
        "(defrule (xor (goal 10 1) (goal 10 3)) => (set-goal 15 1))\n"
        "(defrule (strategic-number sn-minimum-water-body-size-for-dock == 300) => (set-goal 16 1))\n"
        "(defrule (difficulty <= moderate) (difficulty > hardest) => (set-goal 17 1))\n"
        "(defrule (player-human any-enemy) => (set-stance this-any-enemy neutral))\n"
        "(defrule (player-computer every-ally) => (set-goal 18 1))\n"
        "(defrule (player-human every-ally) => (set-goal 19 1))\n"
        "(defrule (game-time >= 9) => (set-goal 20 1))\n"
        "(defrule (true) => (set-strategic-number sn-camp-max-distance 35))\n"
        "(defrule (strategic-number sn-camp-max-distance > 30) => (set-goal 21 1))\n"
        // Allies call for help with LANGUAGE.DLL 22406, "201": a taunt, which a
        // computer player hears and acknowledges.
        "(defrule (true) => (chat-to-player-using-id every-ally 22406) (disable-self))\n"
        "(defrule (taunt-detected any-computer 218) => (set-goal 22 1) (acknowledge-taunt this-any-computer 218))\n";
    Bench b(CompileFiles(files, {}, 0));
    if (b.program.rules.size() != 19 || !b.program.errors.empty()) return "the pass script did not load";
    AI::Brain ally;
    ally.player = 3;
    b.brains.brains.push_back(ally);
    b.mind.taunts[3].set(218);
    b.Pass(0);
    if (!b.brains.brains.front().mind.taunts[1].test(201)) return "a chat that is a number did not reach an ally as a taunt";
    if (b.Goal(22) != 1 || b.mind.taunts[3].test(218)) return "a taunt was not detected and acknowledged";
    if (b.Goal(6) != 2 || b.Goal(7) != 9) return "rule order: the second rule missed the first's goal";
    if (b.Goal(8) != 0) return "a timer triggered before its time";
    if (b.Goal(11) != 0 || b.Goal(12) != 1 || b.Goal(13) != 0 || b.Goal(14) != 1 || b.Goal(15) != 0) {
        return "the logic is wrong (all goals clear)";
    }
    if (b.Goal(16) != 1) return "a strategic number did not start at the guide's default";
    if (b.Goal(17) != 1) return "the difficulty facts compare the wrong way round";
    if (b.mind.stance[2] != static_cast<uint8_t>(AI::Lang::Stance::Neutral)) {
        return "this-any-enemy did not carry the player any-enemy matched";
    }
    if (b.Goal(18) != 1 || b.Goal(19) != 0) return "every-* wildcards are wrong";
    if (b.Goal(20) != 0) return "game-time is wrong";
    if (b.Goal(21) != 1) return "a strategic number set was not read back";

    b.mind.goals[7] = 0;
    b.mind.goals[10] = 1;
    for (int g = 11; g <= 15; ++g) b.mind.goals[static_cast<size_t>(g)] = 0;
    b.Pass(9500);
    if (b.Goal(6) != 2 || b.Goal(7) != 0) return "a rule ran again after disable-self";
    if (b.Goal(8) != 0) return "a timer triggered before its time";
    if (b.Goal(11) != 1 || b.Goal(12) != 0 || b.Goal(13) != 1 || b.Goal(14) != 0 || b.Goal(15) != 1) {
        return "the logic is wrong (goal 10 set)";
    }
    if (b.Goal(20) != 1) return "game-time is wrong";
    b.Pass(10000);
    if (b.Goal(8) != 1) return "a timer did not trigger at its time";
    b.mind.goals[8] = 0;
    b.Pass(11000);
    if (b.Goal(8) != 0) return "a disabled timer still triggered";
    return nullptr;
}

// Every shipped AI file loads, for every civilisation, every difficulty,
// every map, Death Match and Regicide: no word in them unknown.
const char* CheckOriginal() {
    const auto read = [](uint32_t id, std::string& out) {
        return HAL::GraphicCache::Get().ReadGameDataText(id, out);
    };
    auto load = [&](const Core::GameSetup& setup, const char*& verdict) {
        Core::Random rng(setup.seed);
        const AI::Script::Program p = AI::Load(setup, 1, rng, read);
        if (!p.errors.empty()) {
            std::fprintf(stderr, "[script] %s\n", p.errors.front().c_str());
            verdict = "the original's AI scripts did not load cleanly";
        }
        if (p.files.empty() || p.files.front() != "randomgame") verdict = "the original's AI did not start at randomgame";
        if (p.rules.size() < 200) verdict = "the original's AI loaded too few rules";
        return p;
    };
    const char* verdict = nullptr;
    Core::GameSetup setup = Core::DefaultSetup();
    setup.players[1].computer = true;
    for (int civ = 1; civ <= 13 && verdict == nullptr; ++civ) {
        setup.players[1].civ = civ;
        load(setup, verdict);
    }
    // Every file a load-random can pick -- the diplomacy personalities, at
    // every difficulty, with teams locked and not: each roll of the die.
    for (int d = 0; d <= 4 && verdict == nullptr; ++d) {
        for (int locked = 0; locked <= 1 && verdict == nullptr; ++locked) {
            for (int roll : {0, 12, 22, 27, 40, 95}) {
                Core::GameSetup s = setup;
                s.difficulty = d;
                s.lock_teams = locked == 1;
                AI::Script::Environment env;
                env.symbols = AI::Symbols(s, 1);
                env.read = [&read](const std::string& name, std::string& text) {
                    const uint32_t id = AI::FileId(name);
                    return id != 0 && read(id, text);
                };
                env.percent = [roll]() { return roll; };
                const AI::Script::Program p = AI::Script::Compile("randomgame", env);
                if (!p.errors.empty()) {
                    std::fprintf(stderr, "[script] %s\n", p.errors.front().c_str());
                    verdict = "a file the original's AI loads at random did not load cleanly";
                    break;
                }
            }
        }
    }
    // A seat's scripts are loaded under its own civilisation's symbol, which
    // brings in that civilisation's block of rules.
    if (verdict == nullptr) {
        setup.players[1].civ = 1;
        std::vector<std::string> symbols = AI::Symbols(setup, 1);
        const auto civ = std::find(symbols.begin(), symbols.end(), "BRITON-CIV");
        if (civ == symbols.end()) {
            verdict = "a seat's scripts are not loaded under its civilisation";
        } else {
            auto compile = [&](const std::vector<std::string>& with) {
                AI::Script::Environment env;
                env.symbols = with;
                env.read = [&read](const std::string& name, std::string& text) {
                    const uint32_t id = AI::FileId(name);
                    return id != 0 && read(id, text);
                };
                env.percent = []() { return 0; };
                return AI::Script::Compile("randomgame", env).rules.size();
            };
            const size_t with_civ = compile(symbols);
            symbols.erase(civ);
            if (compile(symbols) >= with_civ) verdict = "the civilisation's symbol loads none of its rules";
        }
    }
    setup.players[1].civ = 1;
    size_t easiest = 0, hardest = 0;
    bool market_easy = false, market_hard = false;
    for (int d = 0; d <= 4 && verdict == nullptr; ++d) {
        setup.difficulty = d;
        const AI::Script::Program p = load(setup, verdict);
        bool market = false;
        for (const std::string& f : p.files) market = market || f == "petersen market";
        if (d == 0) { hardest = p.rules.size(); market_hard = market; }
        if (d == 4) { easiest = p.rules.size(); market_easy = market; }
    }
    // The difficulty decides what is loaded: the market and age rules for the
    // harder levels only.
    if (verdict == nullptr && (!market_hard || market_easy || hardest <= easiest)) {
        verdict = "the difficulty did not decide what the original's AI loads";
    }
    setup.difficulty = 0;
    for (int m = 0; m < AI::Lang::kMapTypes - 1 && verdict == nullptr; ++m) {
        setup.map_script = std::string(AI::Lang::MapTypeName(m));
        const AI::Script::Program p = load(setup, verdict);
        bool fishboat = false;
        for (const std::string& f : p.files) fishboat = fishboat || f == "petersen fishboat";
        const bool water = setup.map_script != "arabia" && setup.map_script != "black-forest" &&
                           setup.map_script != "fortress" && setup.map_script != "gold-rush" &&
                           setup.map_script != "highland";
        if (fishboat != water) verdict = "the map did not decide whether the fishing fleet is loaded";
    }
    setup.map_script = "Arabia";
    setup.death_match = true;
    setup.resources = Core::StartingResources::DeathMatch;
    if (verdict == nullptr) {
        const AI::Script::Program p = load(setup, verdict);
        bool dm = false;
        for (const std::string& f : p.files) dm = dm || f == "petersen deathmatch";
        if (!dm) verdict = "Death Match did not load Petersen's deathmatch rules";
    }
    setup.resources = Core::StartingResources::Standard;
    setup.regicide = true;
    if (verdict == nullptr) load(setup, verdict);
    return verdict;
}

// The rule language's table of words: each binds to the dat entry its name
// says.
const char* CheckSymbols() {
    const auto& rules = Core::Rules::Get();
    const AI::Lang::Thing* things = AI::Lang::Things();
    for (int t = 0; t < AI::Lang::ThingCount(); ++t) {
        const std::string_view want = AI::Lang::ExpectedLabel(t);
        if (want.empty()) continue;
        const AI::Lang::Thing& thing = things[t];
        std::string have;
        if (thing.kind == AI::Lang::ThingKind::Research) {
            if (thing.tech < 0 || thing.tech >= static_cast<int>(rules.tech_text.size())) return "a research word binds to no technology";
            have = rules.tech_text[static_cast<size_t>(thing.tech)].name;
        } else {
            const int id = thing.ids[thing.line ? 0 : thing.member];
            if (id < 0 || id >= static_cast<int>(rules.unit_text.size())) return "a unit word binds to no unit";
            have = rules.unit_text[static_cast<size_t>(id)].label;
        }
        if (have != want) {
            std::fprintf(stderr, "[script] %.*s is %s\n", static_cast<int>(thing.name.size()), thing.name.data(), have.c_str());
            return "a word binds to the wrong dat entry";
        }
    }
    return nullptr;
}

// Facts and actions bound to a real world: a computer player's start.
const char* CheckWorld() {
    Core::GameSetup setup = Core::DefaultSetup();
    setup.players[1].computer = true;
    ECS::Simulation sim;
    sim.Init(setup);
    auto& reg = sim.GetRegistry();
    Files files;
    files["main"] =
        "(defrule (can-train villager) => (set-goal 1 1))\n"
        "(defrule (building-type-count town-center == 1) (building-type-count-total house == 0) => (set-goal 2 1))\n"
        "(defrule (unit-type-count villager == 3) (civilian-population == 3) => (set-goal 3 1))\n"
        "(defrule (military-population == 1) (unit-type-count scout-cavalry-line == 1) => (set-goal 4 1))\n"
        "(defrule (current-age == dark-age) (not (can-research feudal-age)) (can-research ri-loom) => (set-goal 5 1))\n"
        "(defrule (food-amount == 200) (wood-amount == 200) (players-civ 2 frankish) (civ-selected teutonic) => (set-goal 6 1))\n"
        "(defrule (map-type arabia) (map-size tiny) (population-cap == 75) => (set-goal 7 1))\n"
        "(defrule (can-build house) => (build house) (train villager))\n"
        "(defrule (unit-type-count-total villager == 4) (building-type-count-total house == 1) => (set-goal 8 1))\n"
        "(defrule (can-train villager) => (set-goal 9 1))\n"
        "(defrule (true) => (cc-add-resource gold 500) (disable-self))\n"
        "(defrule (gold-amount == 600) => (set-goal 10 1))\n";
    AI::Script::Program program = CompileFiles(files, {}, 0);
    if (!program.errors.empty() || program.rules.size() != 12) return "the world script did not load";
    AI::Brains* brains = reg.ctx().find<AI::Brains>();
    if (brains == nullptr || brains->brains.empty()) return "no brain for the computer seat";
    AI::Brain& brain = brains->brains.front();
    brain.program = program;
    AI::Reset(brain.mind, brain.program.rules.size());
    brain.mind.stance[2] = static_cast<uint8_t>(AI::Lang::Stance::Enemy);
    AI::Census census = AI::Take(reg, 1);
    AI::Take(reg, 1, nullptr, nullptr, brains->world);
    AI::Turn turn;
    turn.registry = &reg;
    turn.grid = &sim.GetGrid();
    turn.brains = brains;
    turn.brain = &brain;
    turn.program = &brain.program;
    turn.mind = &brain.mind;
    turn.world = &brains->world;
    turn.census = &census;
    turn.setup = &setup;
    turn.player = 1;
    AI::Pass(turn);
    const auto& g = brain.mind.goals;
    if (g[1] != 1) return "can-train villager is false at the start";
    if (g[2] != 1) return "the building counts are wrong at the start";
    if (g[3] != 1) return "the villager counts are wrong at the start";
    if (g[4] != 1) return "the scout is not the military population";
    if (g[5] != 1) return "the research facts are wrong at the start";
    if (g[6] != 1) return "the stock or civilisation facts are wrong";
    if (g[7] != 1) return "the lobby facts are wrong";
    if (g[8] != 1) return "build and train did not show in the totals of the same pass";
    if (g[9] != 0) return "can-train villager held with the Town Center busy";
    if (g[10] != 1) return "cc-add-resource gave the computer player nothing";
    // The same order for a human seat is refused.
    ECS::Orders::Execute(reg, sim.GetGrid(), Core::ComputerResourceCommand{2, 500, 2});
    if (const ECS::Players::Player* human = ECS::Players::Find(reg, 2)) {
        if (ECS::Players::Stock(*human, Core::ResourceType::Gold) != 100) return "a human seat was given the computer's cheat";
    }
    // And in the world: a house foundation, a villager queued.
    int houses = 0, queued = 0;
    for (auto e : reg.view<ECS::Components::Building, ECS::Components::Identity, ECS::Components::Owner>()) {
        if (reg.get<ECS::Components::Owner>(e).player_id != 1) continue;
        if (reg.get<ECS::Components::Identity>(e).unit_id == 70) ++houses;
        if (const auto* q = reg.try_get<ECS::Components::ProductionQueue>(e)) queued += q->count;
    }
    if (houses != 1 || queued != 1) return "build and train gave no orders";
    return nullptr;
}

// A building raised from a button unit of its own: in the Castle Age a Town
// Center is built from RTWC3X, which is placed as the Town Center.
const char* CheckTownCenter() {
    Core::GameSetup setup = Core::DefaultSetup();
    setup.players[1].computer = true;
    setup.starting_age = 2;
    ECS::Simulation sim;
    sim.Init(setup);
    auto& reg = sim.GetRegistry();
    if (ECS::Players::Player* seat = ECS::Players::Find(reg, 1)) {
        ECS::Players::AddStock(*seat, Core::ResourceType::Wood, 1000);
        ECS::Players::AddStock(*seat, Core::ResourceType::Stone, 1000);
    }
    Files files;
    files["main"] =
        "(defrule (can-build town-center) (building-type-count-total town-center == 1) => (build town-center) (set-goal 2 1))\n"
        "(defrule (building-type-count-total town-center == 2) => (set-goal 1 1))\n";
    AI::Brains* brains = reg.ctx().find<AI::Brains>();
    if (brains == nullptr || brains->brains.empty()) return "no brain for the computer seat";
    AI::Brain& brain = brains->brains.front();
    brain.program = CompileFiles(files, {}, 0);
    AI::Reset(brain.mind, brain.program.rules.size());
    brain.mind.stance[2] = static_cast<uint8_t>(AI::Lang::Stance::Enemy);
    for (int t = 0; t < 3 * 1000 / 16 && brain.mind.goals[2] == 0; ++t) sim.Update(16);
    if (brain.mind.goals[2] != 1) return "a Castle Age computer player could not build a Town Center";
    // Counted as a Town Center in the very pass that placed it.
    if (brain.mind.goals[1] != 1) return "a Town Center going up was not counted as one";
    return nullptr;
}

// The rules talk to the game: set-stance is an order to its diplomacy (and
// refused while teams are locked), what they say reaches the people it is
// said to, and a taunt a person types reaches the rules.
const char* CheckTalk() {
    for (const bool locked : {false, true}) {
        Core::GameSetup setup = Core::DefaultSetup();
        setup.players[1].computer = true;
        setup.lock_teams = locked;
        ECS::Simulation sim;
        sim.Init(setup);
        auto& reg = sim.GetRegistry();
        Files files;
        files["main"] =
            "(defrule (true) => (set-stance 2 ally) (chat-to-all \"well met\") (disable-self))\n"
            "(defrule (stance-toward 2 ally) => (set-goal 1 1))\n"
            "(defrule (taunt-detected 2 7) => (set-goal 2 1))\n"
            "(defrule (current-score > 100) (players-score 2 > 100) => (set-goal 3 1))\n";
        AI::Brains* brains = reg.ctx().find<AI::Brains>();
        if (brains == nullptr || brains->brains.empty()) return "no brain for the computer seat";
        AI::Brain& brain = brains->brains.front();
        brain.program = CompileFiles(files, {}, 0);
        if (!brain.program.errors.empty()) return "the talking script did not load";
        AI::Reset(brain.mind, brain.program.rules.size());
        for (int t = 0; t < 2 * kTicksPerSecond; ++t) sim.Update(16);
        const bool ally = ECS::Diplomacy::StanceOf(reg, 1, 2) == ECS::Diplomacy::Stance::Ally;
        if (locked) {
            if (ally) return "set-stance changed a stance with teams locked";
            if (brain.mind.goals[1] != 0) return "the rules saw a stance the lock refused";
            continue;
        }
        if (brain.mind.goals[3] != 1) return "the score facts did not read the game's score";
        if (!ally) return "set-stance did not reach the game's diplomacy";
        if (brain.mind.goals[1] != 1) return "the rules did not see their own stance";
        bool heard = false;
        if (const ECS::Chat::Log* chat = ECS::Chat::Read(reg)) {
            for (const ECS::Chat::Line& line : chat->lines) {
                heard |= line.from == 1 && line.text == "well met" && line.Heard(2);
            }
        }
        if (!heard) return "what the rules said did not reach the human player";
        if (brain.mind.goals[2] != 0) return "a taunt was detected before anyone typed it";
        ECS::Orders::Execute(reg, sim.GetGrid(), Core::ChatCommand{0, 0, "7", 2});
        for (int t = 0; t < 2 * kTicksPerSecond; ++t) sim.Update(16);
        if (brain.mind.goals[2] != 1) return "a taunt a person typed did not reach the rules";
    }
    return nullptr;
}

// The Market and tribute through the rules: the price facts read the
// Market's prices, a lot sold through a finished Market of the player's,
// tribute given as a player gives it, and tribute received remembered until
// the rules clear the memory.
const char* CheckTrading() {
    Core::GameSetup setup = Core::DefaultSetup();
    setup.players[1].computer = true;
    setup.starting_age = 1;
    setup.lock_teams = false;   // tribute to anyone, not allies only
    ECS::Simulation sim;
    sim.Init(setup);
    auto& reg = sim.GetRegistry();
    auto& grid = sim.GetGrid();
    const int market = Core::ConfigManager::Get().GetUnitIdByName("MRKT");
    // A finished Market for each side, near its Town Center.
    for (Core::PlayerId p : {Core::PlayerId{1}, Core::PlayerId{2}}) {
        const entt::entity tc = [&] {
            for (auto e : reg.view<ECS::Components::Building, ECS::Components::Owner>()) {
                if (reg.get<ECS::Components::Owner>(e).player_id == p) return e;
            }
            return entt::entity{entt::null};
        }();
        if (tc == entt::null) return "no Town Center to build beside";
        const auto& at = reg.get<ECS::Components::Transform>(tc).position;
        bool placed = false;
        for (int r = 5; r < 14 && !placed; ++r) {
            for (int dx = -r; dx <= r && !placed; ++dx) {
                const int x = at.x.ToInt() + dx, y = at.y.ToInt() + r;
                if (ECS::Buildings::CanPlace(grid, market, x, y)) {
                    ECS::Buildings::Place(reg, grid, market, p, x, y, true);
                    placed = true;
                }
            }
        }
        if (!placed) return "no room for a Market";
    }
    Files files;
    files["main"] =
        "(defrule (commodity-selling-price wood == 70) (commodity-buying-price stone == 169) => (set-goal 1 1) (disable-self))\n"
        "(defrule (can-sell-commodity wood) (goal 2 0) => (sell-commodity wood) (set-goal 2 1))\n"
        "(defrule (goal 3 0) => (tribute-to-player 2 gold 50) (set-goal 3 1))\n"
        "(defrule (players-tribute 2 wood >= 100) => (set-goal 4 1))\n"
        "(defrule (players-tribute-memory 2 wood >= 100) => (set-goal 5 1) (clear-tribute-memory 2 wood))\n"
        "(defrule (goal 5 1) (players-tribute-memory 2 wood == 0) (players-tribute 2 wood == 100) => (set-goal 6 1))\n";
    AI::Brains* brains = reg.ctx().find<AI::Brains>();
    if (brains == nullptr || brains->brains.empty()) return "no brain for the computer seat";
    AI::Brain& brain = brains->brains.front();
    brain.program = CompileFiles(files, {}, 0);
    if (!brain.program.errors.empty()) return "the trading script did not load";
    AI::Reset(brain.mind, brain.program.rules.size());
    const ECS::Players::Player* computer = ECS::Players::Find(reg, 1);
    const ECS::Players::Player* human = ECS::Players::Find(reg, 2);
    if (computer == nullptr || human == nullptr) return "no players";
    const int wood = ECS::Players::Stock(*computer, Core::ResourceType::Wood);
    const int their_gold = ECS::Players::Stock(*human, Core::ResourceType::Gold);
    for (int t = 0; t < 2 * kTicksPerSecond; ++t) sim.Update(16);
    const auto& g = brain.mind.goals;
    if (g[1] != 1) return "the price facts did not read the Market's opening prices";
    if (ECS::Players::Stock(*computer, Core::ResourceType::Wood) != wood - 100) {
        return "sell-commodity did not sell a lot of wood";
    }
    if (ECS::Players::Stock(*human, Core::ResourceType::Gold) != their_gold + 50) {
        return "tribute-to-player did not give the tribute";
    }
    if (g[4] != 0) return "tribute was remembered before any was given";
    ECS::Orders::Execute(reg, grid, Core::TributeCommand{1, Core::Rules::Res::Wood, 100, 2});
    for (int t = 0; t < 3 * kTicksPerSecond; ++t) sim.Update(16);
    if (g[4] != 1) return "players-tribute did not see the tribute given";
    if (g[5] != 1) return "players-tribute-memory did not see the tribute given";
    if (g[6] != 1) return "clear-tribute-memory did not clear only the memory";
    return nullptr;
}

// Explore groups past the scout: sn-number-explore-groups 2 takes up to
// sn-maximum-explore-group-size idle soldiers into a group, which goes out
// together while the rest stay home; asked for one group again, the scout's
// alone, the soldiers are given back.
const char* CheckExploring() {
    Core::GameSetup setup = Core::DefaultSetup();
    setup.players[1].computer = true;
    ECS::Simulation sim;
    sim.Init(setup);
    auto& reg = sim.GetRegistry();
    auto& grid = sim.GetGrid();
    const entt::entity tc = [&] {
        for (auto e : reg.view<ECS::Components::Building, ECS::Components::Owner>()) {
            if (reg.get<ECS::Components::Owner>(e).player_id == 1) return e;
        }
        return entt::entity{entt::null};
    }();
    if (tc == entt::null) return "no Town Center to stand beside";
    const auto& at = reg.get<ECS::Components::Transform>(tc).position;
    const int spearman = Core::ConfigManager::Get().GetUnitIdByName("SPRMN");
    std::vector<entt::entity> spears;
    for (int r = 4; r < 12 && spears.size() < 3; ++r) {
        for (int dx = -r; dx <= r && spears.size() < 3; dx += 2) {
            const int x = at.x.ToInt() + dx, y = at.y.ToInt() + r;
            if (!grid.IsWalkable(x, y)) continue;
            const entt::entity e = ECS::ScenarioWorld::Place(
                reg, grid, spearman, 1,
                {Core::Fixed32(x) + Core::Fixed32(1) / Core::Fixed32(2), Core::Fixed32(y) + Core::Fixed32(1) / Core::Fixed32(2)},
                {}, 0);
            if (e != entt::null) spears.push_back(e);
        }
    }
    if (spears.size() != 3) return "no room for three spearmen";
    Files files;
    files["main"] =
        "(defrule (true) => (set-strategic-number sn-number-explore-groups 2)"
        " (set-strategic-number sn-minimum-explore-group-size 2)"
        " (set-strategic-number sn-maximum-explore-group-size 2) (disable-self))\n";
    AI::Brains* brains = reg.ctx().find<AI::Brains>();
    if (brains == nullptr || brains->brains.empty()) return "no brain for the computer seat";
    AI::Brain& brain = brains->brains.front();
    brain.program = CompileFiles(files, {}, 0);
    if (!brain.program.errors.empty()) return "the exploring script did not load";
    AI::Reset(brain.mind, brain.program.rules.size());
    for (int t = 0; t < 3 * kTicksPerSecond; ++t) sim.Update(16);
    if (brain.explore_groups.size() != 1) return "sn-number-explore-groups 2 gave no explore group past the scout";
    const std::vector<entt::entity>& group = brain.explore_groups.front();
    if (group.size() != 2) return "the explore group did not take sn-maximum-explore-group-size soldiers";
    int out = 0;
    for (entt::entity e : spears) {
        const bool in = std::find(group.begin(), group.end(), e) != group.end();
        const auto& p = reg.get<ECS::Components::Transform>(e).position;
        const bool moved = p.x != reg.get<ECS::Components::Transform>(e).previous_position.x ||
                           p.y != reg.get<ECS::Components::Transform>(e).previous_position.y;
        if (in && !moved) return "a soldier of the explore group did not go out";
        if (!in && moved) return "a soldier left out of the explore group went out";
        out += moved ? 1 : 0;
    }
    if (out != 2) return "the explore group did not go out";
    brain.mind.sn[static_cast<size_t>(AI::Lang::SN::NumberExploreGroups)] = 1;
    for (int t = 0; t < kTicksPerSecond; ++t) sim.Update(16);
    if (!brain.explore_groups.empty()) return "the soldiers were kept exploring when only the scout's group was asked for";
    return nullptr;
}

} // namespace

bool CheckScript() {
    const char* verdict = CheckReader();
    if (verdict == nullptr) verdict = CheckPasses();
    if (verdict == nullptr) verdict = CheckSymbols();
    if (verdict == nullptr) verdict = CheckOriginal();
    if (verdict == nullptr) verdict = CheckWorld();
    if (verdict == nullptr) verdict = CheckTownCenter();
    if (verdict == nullptr) verdict = CheckTalk();
    if (verdict == nullptr) verdict = CheckTrading();
    if (verdict == nullptr) verdict = CheckExploring();
    std::printf("script=%s\n", verdict == nullptr ? "ok" : verdict);
    return verdict == nullptr;
}

} // namespace Verify
