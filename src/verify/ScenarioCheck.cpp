#include "verify/Harness.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

#include "ai/Brain.h"
#include "core/CommandQueue.h"
#include "core/ConfigManager.h"
#include "core/GameSetup.h"
#include "core/Strings.h"
#include "ecs/Diplomacy.h"
#include "ecs/Digest.h"
#include "ecs/Mortality.h"
#include "ecs/Players.h"
#include "ecs/Simulation.h"
#include "ecs/Techs.h"
#include "ecs/Triggers.h"
#include "ecs/Units.h"
#include "ecs/Victory.h"
#include "ecs/components/Identity.h"
#include "ecs/components/Owner.h"
#include "ecs/components/Path.h"
#include "ecs/components/Trader.h"
#include "ecs/components/Transform.h"
#include "save/Recording.h"
#include "save/Save.h"
#include "scenario/Campaign.h"
#include "scenario/Scenario.h"

namespace Verify {

namespace {

namespace C = ECS::Components;
using Core::Fixed32;

constexpr int kTicksPerSecond = 1000 / 16;
const char* const kDemo = "../original_game_1999/GOODIES/DEMO.SCN";
const char* const kJoan = "../assets/original/Campaign/cam1.cpn";
const char* const kCampaigns[] = {
    "../assets/original/Campaign/cam1.cpn", "../assets/original/Campaign/cam2.cpn",
    "../assets/original/Campaign/cam3.cpn", "../assets/original/Campaign/cam4.cpn",
    "../assets/original/Campaign/cam8.cpn",
};

// The first failure, which is what the result line reports.
struct Verdict {
    std::string why;
    bool ok() const { return why.empty(); }
    void Fail(const std::string& reason) {
        if (why.empty()) why = reason;
    }
    void Expect(bool holds, const std::string& reason) {
        if (!holds) Fail(reason);
    }
};

void Run(ECS::Simulation& sim, int ticks) {
    for (int t = 0; t < ticks; ++t) sim.Update(16);
}

// A player's living things standing in a tile rectangle of the grid's.
int CountIn(const entt::registry& reg, Core::PlayerId player, const Scenario::Area& area) {
    int n = 0;
    auto view = reg.view<const C::Identity, const C::Owner, const C::Transform>();
    for (auto e : view) {
        if (view.get<const C::Owner>(e).player_id != player) continue;
        const auto& at = view.get<const C::Transform>(e).position;
        if (area.Contains(Physics::TileOf(at.x), Physics::TileOf(at.y))) ++n;
    }
    return n;
}

bool ShownLine(const entt::registry& reg, ECS::Triggers::Show kind, int string_id, const char* sound,
               int trigger) {
    const auto* display = ECS::Triggers::ReadDisplay(reg);
    if (display == nullptr) return false;
    for (const auto& line : display->lines) {
        if (line.kind == kind && line.string_id == string_id && line.sound == sound &&
            line.trigger == trigger && line.text == Core::Strings::Get(string_id) && !line.text.empty()) {
            return true;
        }
    }
    return false;
}

bool Fired(const entt::registry& reg, size_t trigger) {
    const auto* state = ECS::Triggers::Read(reg);
    return state != nullptr && trigger < state->runs.size() && state->runs[trigger].fired;
}
bool On(const entt::registry& reg, size_t trigger) {
    const auto* state = ECS::Triggers::Read(reg);
    return state != nullptr && trigger < state->runs.size() && state->runs[trigger].on;
}

// The scenario's seats with nobody at the computer: the triggers alone move
// anything, so what is checked is theirs.
Core::GameSetup Quiet(Core::GameSetup setup) {
    for (auto& seat : setup.players) seat.computer = false;
    return setup;
}

// Every scenario on the disc read to its last byte, and its triggers,
// conditions and effects counted against totals taken independently from the
// same files.
void CheckFiles(Verdict& v) {
    int files = 0, triggers = 0, conditions = 0, effects = 0;
    std::string unsupported;
    auto tally = [&](const Scenario::File& f, const std::string& name) {
        ++files;
        v.Expect(f.version == "1.18" && f.player_data_version == 120 && f.trigger_version == 16,
                 name + " is not version 1.18 / 1.20 / 1.6");
        for (const auto& t : f.triggers) {
            ++triggers;
            for (const auto& c : t.conditions) {
                ++conditions;
                if (!ECS::Triggers::ConditionSupported(c.type)) unsupported = "condition " + std::to_string(c.type);
            }
            for (const auto& e : t.effects) {
                ++effects;
                if (!ECS::Triggers::EffectSupported(e.type)) unsupported = "effect " + std::to_string(e.type);
            }
        }
    };
    Scenario::File file;
    std::string error;
    if (!Scenario::Load(kDemo, -1, file, &error)) {
        v.Fail("DEMO.SCN unread: " + error);
        return;
    }
    tally(file, "DEMO.SCN");
    for (const char* path : kCampaigns) {
        Scenario::Campaign campaign;
        if (!Scenario::LoadCampaign(path, campaign, &error)) {
            v.Fail(std::string(path) + " unread: " + error);
            return;
        }
        for (size_t i = 0; i < campaign.scenarios.size(); ++i) {
            if (!Scenario::Load(path, static_cast<int>(i), file, &error)) {
                v.Fail(campaign.scenarios[i].file + " unread: " + error);
                return;
            }
            tally(file, campaign.scenarios[i].file);
        }
    }
    v.Expect(files == 32, "not 32 scenarios on the disc: " + std::to_string(files));
    v.Expect(triggers == 576 && conditions == 570 && effects == 1202,
             "the triggers counted " + std::to_string(triggers) + "/" + std::to_string(conditions) + "/" +
                 std::to_string(effects) + ", not 576/570/1202");
    v.Expect(unsupported.empty(), "a " + unsupported + " the disc uses is not carried out");
}

// DEMO.SCN as the file states it.
void CheckDemoFile(Verdict& v, const Scenario::File& f) {
    v.Expect(f.width == 168 && f.height == 168, "DEMO.SCN is not 168 by 168");
    const int civs[] = {0, 2, 9, 2, 5, 4};
    const size_t units[] = {4352, 665, 253, 6, 265, 89, 0, 0, 0};
    const int ages[] = {0, 4, 3, 0, 3, 2};
    for (int p = 1; p <= 8; ++p) {
        v.Expect(f.players[p].active == (p <= 5), "DEMO.SCN's player " + std::to_string(p) + " active wrongly");
        v.Expect(f.players[p].human == (p == 1), "DEMO.SCN's player " + std::to_string(p) + " human wrongly");
        if (p <= 5) {
            v.Expect(f.players[p].civ == civs[p], "DEMO.SCN's player " + std::to_string(p) + " civ wrong");
            v.Expect(f.players[p].starting_age == ages[p], "DEMO.SCN's player " + std::to_string(p) + " age wrong");
        }
    }
    for (int p = 0; p < Core::kMaxPlayers; ++p) {
        v.Expect(f.players[p].units.size() == units[p],
                 "DEMO.SCN's player " + std::to_string(p) + " has " + std::to_string(f.players[p].units.size()) +
                     " units, not " + std::to_string(units[p]));
    }
    const auto& japan = f.players[4];
    v.Expect(japan.food == 650 && japan.wood == 400 && japan.gold == 223 && japan.stone == 120,
             "DEMO.SCN's player 4 stockpiles wrong");
    v.Expect(f.players[1].stance[4] == 3 && f.players[4].stance[1] == 0, "DEMO.SCN's stances wrong");
    int owner = -1;
    const Scenario::Unit* market = f.FindUnit(10391, &owner);
    v.Expect(market != nullptr && owner == 5 && market->unit_id == 84 && market->x == Fixed32(128) &&
                 market->y == Fixed32(65),
             "DEMO.SCN's unit 10391 is not player 5's Market at (128, 65)");
    const Scenario::Unit* farm = f.FindUnit(7556, &owner);
    const Fixed32 half = Fixed32(1) / Fixed32(2);
    v.Expect(farm != nullptr && owner == 1 && farm->unit_id == 50 && farm->x == Fixed32(93) + half &&
                 farm->y == Fixed32(102) + half,
             "DEMO.SCN's unit 7556 is not player 1's Farm at (93.5, 102.5)");
    v.Expect(f.triggers.size() == 6, "DEMO.SCN has not 6 triggers");
    if (f.triggers.size() == 6) {
        const auto& warn = f.triggers[1];
        v.Expect(warn.conditions.size() == 1 && warn.conditions[0].type == Scenario::ConditionType::ObjectsInArea &&
                     warn.conditions[0].player == 1 && warn.conditions[0].amount == 1 &&
                     warn.conditions[0].area.x0 == 65 && warn.conditions[0].area.y0 == 0 &&
                     warn.conditions[0].area.x1 == 81 && warn.conditions[0].area.y1 == 23,
                 "DEMO.SCN's \"Warning from p4\" condition misread");
        v.Expect(warn.effects.size() == 1 && warn.effects[0].type == Scenario::EffectType::DisplayInstructions &&
                     warn.effects[0].string_id == 47018 && warn.effects[0].sound == "d1a" &&
                     warn.effects[0].display_time == 10,
                 "DEMO.SCN's \"Warning from p4\" effect misread");
        v.Expect(warn.name == "Warning from p4", "DEMO.SCN's trigger 1 is not \"Warning from p4\"");
    }
}

// The campaign files and what LANGUAGE.DLL calls them.
void CheckCampaigns(Verdict& v) {
    Scenario::Campaign joan;
    std::string error;
    if (!Scenario::LoadCampaign(kJoan, joan, &error)) {
        v.Fail("cam1.cpn unread: " + error);
        return;
    }
    v.Expect(joan.name == "cam1" && joan.scenarios.size() == 6 && joan.scenarios[0].name == "Joan_scn1" &&
                 joan.scenarios[0].file == "Joan_scn1.scn" && joan.scenarios[5].file == "Joan_scn6.scn",
             "cam1.cpn's contents misread");
    const Scenario::CampaignStrings s = Scenario::StringsOf(joan);
    v.Expect(s.list == 34928 && Core::Strings::Get(s.name) == "Joan of Arc" &&
                 Core::Strings::Get(Scenario::ScenarioString(joan, 0)) == "1. An Unlikely Messiah" &&
                 Core::Strings::Get(Scenario::ScenarioString(joan, 5)) == "6. A Perfect Martyr",
             "cam1's LANGUAGE.DLL strings wrong");
    Scenario::Campaign wallace;
    if (!Scenario::LoadCampaign(kCampaigns[4], wallace, &error)) {
        v.Fail("cam8.cpn unread: " + error);
        return;
    }
    v.Expect(wallace.scenarios.size() == 7 && Core::Strings::Get(Scenario::StringsOf(wallace).name) == "William Wallace" &&
                 Core::Strings::Get(Scenario::ScenarioString(wallace, 6)) == "7. The Battle of Falkirk",
             "cam8's strings wrong");

    Scenario::File f;
    if (!Scenario::Load(kJoan, 0, f, &error)) {
        v.Fail("Joan_scn1 unread: " + error);
        return;
    }
    v.Expect(f.width == 144 && f.height == 144, "Joan_scn1 is not 144 by 144");
    const int civs[] = {0, 2, 2, 2, 2, 2, 1};
    for (int p = 1; p <= 6; ++p) {
        v.Expect(f.players[p].active && f.players[p].civ == civs[p] && f.players[p].human == (p == 1),
                 "Joan_scn1's player " + std::to_string(p) + " misread");
    }
    v.Expect(!f.players[7].active, "Joan_scn1's player 7 active");
    v.Expect(f.camera_x == 141 && f.camera_y == 80, "Joan_scn1's view is not on (141, 80)");
    v.Expect(f.texts[Scenario::kInstructions].string_id == 44706 &&
                 Scenario::Show(f.texts[Scenario::kInstructions]) == Core::Strings::Get(44706) &&
                 f.texts[Scenario::kHints].string_id == 44707,
             "Joan_scn1's instructions misread");
    v.Expect(Scenario::Show(f.players[1].name, f.players[1].name_string) == "Joan's Escort",
             "Joan_scn1's player 1 is not Joan's Escort");
}

// DEMO.SCN played: its world as the file lays it out, and its triggers.
void CheckDemoWorld(Verdict& v, const Scenario::File& f) {
    Core::GameSetup setup = Core::DefaultSetup();
    std::string error;
    if (!Scenario::SetupFor(kDemo, -1, setup, &error)) {
        v.Fail("DEMO.SCN gave no lobby: " + error);
        return;
    }
    v.Expect(!setup.players[1].computer && setup.players[2].computer && setup.players[5].computer &&
                 !setup.players[6].active && setup.players[4].civ == 5 && setup.players[1].starting_age == 4 &&
                 setup.map_size == 168,
             "DEMO.SCN's lobby wrong");
    ECS::Simulation sim;
    sim.Init(Quiet(setup));
    auto& reg = sim.GetRegistry();
    const auto& grid = sim.GetGrid();
    v.Expect(grid.GetWidth() == 168 && grid.GetHeight() == 168, "DEMO.SCN's world is not 168 by 168");

    // The ground: every forest the file has, of whichever kind, is forest.
    // The file is turned onto the grid (ecs/ScenarioWorld): the grid's tile
    // (x, y) is the file's (167 - y, x).
    int forest_file = 0;
    for (uint8_t t : f.terrain) forest_file += (t == 10 || (t >= 18 && t <= 21)) ? 1 : 0;
    int forest_world = 0, matches = 0;
    for (int y = 0; y < grid.GetHeight(); ++y) {
        for (int x = 0; x < grid.GetWidth(); ++x) {
            const auto& tile = grid.GetTile(x, y);
            if (tile.terrain == Core::TerrainType::Forest) ++forest_world;
            const size_t i = static_cast<size_t>(x) * 168u + static_cast<size_t>(167 - y);
            if (tile.elevation == f.elevation[i]) ++matches;
        }
    }
    v.Expect(forest_file > 0 && forest_world == forest_file,
             "DEMO.SCN's forest tiles " + std::to_string(forest_world) + ", the file's " + std::to_string(forest_file));
    v.Expect(matches == 168 * 168, "DEMO.SCN's elevation not the file's");

    // Every unit of the players in the game and Gaia's, placed.
    const auto* state = ECS::Triggers::Read(reg);
    size_t expected = 0;
    for (int p = 0; p <= 5; ++p) expected += f.players[p].units.size();
    v.Expect(state != nullptr && state->placed.size() == expected,
             "DEMO.SCN placed " + std::to_string(state != nullptr ? state->placed.size() : 0) + " of " +
                 std::to_string(expected) + " units");
    auto at = [&](entt::entity e, Fixed32 x, Fixed32 y) {
        return reg.valid(e) && reg.all_of<C::Transform>(e) && reg.get<C::Transform>(e).position.x == x &&
               reg.get<C::Transform>(e).position.y == y;
    };
    // The file's (93.5, 102.5) and (128, 65), turned: (102.5, 168 - 93.5)
    // and (65, 168 - 128), half a tile back.
    const entt::entity farm = ECS::Triggers::EntityOf(reg, 7556);
    v.Expect(at(farm, Fixed32(102), Fixed32(74)) && reg.get<C::Identity>(farm).unit_id == 50 &&
                 reg.get<C::Owner>(farm).player_id == 1,
             "DEMO.SCN's farm 7556 not on its tile, the file's (93, 102) turned to (102, 74)");
    const entt::entity market = ECS::Triggers::EntityOf(reg, 10391);
    v.Expect(at(market, Fixed32(65), Fixed32(40)) && reg.get<C::Owner>(market).player_id == 5,
             "DEMO.SCN's market 10391 not on its tile, the file's (128, 65) turned to (65, 40)");
    // A unit stands where the file has it, turned, half a tile back.
    const Fixed32 half = Fixed32(1) / Fixed32(2);
    int units_checked = 0;
    for (const auto& u : f.players[1].units) {
        const auto* rules = Core::Rules::Unit(u.unit_id);
        if (rules == nullptr || rules->type != Core::Rules::UnitType::Creatable || u.garrisoned_in >= 0) continue;
        const entt::entity e = ECS::Triggers::EntityOf(reg, u.id);
        v.Expect(at(e, u.y - half, Fixed32(168) - u.x - half) && reg.get<C::Identity>(e).unit_id == u.unit_id,
                 "DEMO.SCN's unit " + std::to_string(u.id) + " not where the file put it");
        if (++units_checked == 50) break;
    }
    v.Expect(units_checked == 50, "DEMO.SCN has too few units of player 1 to check");
    const auto* japan = ECS::Players::Find(reg, 4);
    v.Expect(japan != nullptr && japan->civ == 5 &&
                 ECS::Players::Resource(*japan, Core::Rules::Res::Food) == Fixed32(650) &&
                 ECS::Players::Resource(*japan, Core::Rules::Res::Gold) == Fixed32(223),
             "DEMO.SCN's player 4 does not start as the file says");
    v.Expect(ECS::Techs::Age(reg, 2) == 3 && ECS::Techs::Age(reg, 3) == 0 && ECS::Techs::Age(reg, 5) == 2,
             "DEMO.SCN's players not in their ages");
    v.Expect(ECS::Diplomacy::StanceOf(reg, 1, 4) == ECS::Diplomacy::Stance::Enemy &&
                 ECS::Diplomacy::StanceOf(reg, 4, 1) == ECS::Diplomacy::Stance::Ally,
             "DEMO.SCN's stances not the file's");
    v.Expect(!ECS::Techs::Researched(reg, 4, 140), "player 4 has Guard Tower before its trigger");

    // Two seconds: the trigger with no conditions has fired -- player 4 has
    // its Guard Tower, player 1's trade carts are off to player 5's Market --
    // and nobody has gone near Japan.
    Core::CommandQueue::Get().PopAllCommands();
    Run(sim, 2 * kTicksPerSecond);
    v.Expect(Fired(reg, 0) && !On(reg, 0), "DEMO.SCN's \"Task p1 Trade units\" did not fire");
    v.Expect(ECS::Techs::Researched(reg, 4, 140), "\"Task p1 Trade units\" did not research Guard Tower");
    int traders = 0;
    auto carts = reg.view<const C::Trader, const C::Owner>();
    for (auto e : carts) traders += carts.get<const C::Owner>(e).player_id == 1 ? 1 : 0;
    v.Expect(traders > 0, "\"Task p1 Trade units\" sent no trade unit to trade");
    v.Expect(On(reg, 1) && On(reg, 2) && !Fired(reg, 1) && !Fired(reg, 2),
             "DEMO.SCN's warnings fired with nobody near Japan");

    // Player 1 walks into the warning's ground, inside the war's too: the
    // file's tile (70, 10), turned.
    const int villager = Core::ConfigManager::Get().GetUnitIdByName("VMBAS");
    ECS::Units::Spawn(reg, villager, 1, {Fixed32(10), Fixed32(97)});
    Run(sim, 2 * kTicksPerSecond);
    v.Expect(Fired(reg, 1) && ShownLine(reg, ECS::Triggers::Show::Instructions, 47018, "d1a", 1),
             "\"Warning from p4\" did not show its instruction when player 1 came near");
    v.Expect(Fired(reg, 2) && ECS::Diplomacy::StanceOf(reg, 4, 1) == ECS::Diplomacy::Stance::Enemy,
             "\"P4 declares war\" did not make player 4 player 1's enemy");
    int marching = 0;
    auto paths = reg.view<const C::Path, const C::Owner, const C::Transform>();
    for (auto e : paths) {
        if (paths.get<const C::Owner>(e).player_id != 4) continue;
        const auto& path = paths.get<const C::Path>(e);
        const Fixed32 dx = path.goal.x - Fixed32(17), dy = path.goal.y - Fixed32(82);
        if (path.has_goal && dx * dx + dy * dy <= Fixed32(4)) ++marching;
    }
    v.Expect(marching > 0, "\"P4 declares war\" sent none of player 4's army to the file's (85, 17), the grid's (17, 82)");
    v.Expect(On(reg, 3) && !Fired(reg, 3), "\"p4 gives up\" fired with nobody on its ground");

    // Saved and loaded, the scenario game runs on as the one never stopped.
    std::vector<uint8_t> bytes;
    ECS::Simulation loaded;
    std::string why;
    if (!Save::Write(sim, bytes) || !Save::Read(loaded, bytes, &why)) {
        v.Fail("a scenario game did not save and load: " + why);
        return;
    }
    const auto* a = ECS::Triggers::Read(reg);
    const auto* b = ECS::Triggers::Read(loaded.GetRegistry());
    bool same = a != nullptr && b != nullptr && a->runs.size() == b->runs.size() && a->placed.size() == b->placed.size();
    for (size_t i = 0; same && i < a->runs.size(); ++i) {
        same = a->runs[i].on == b->runs[i].on && a->runs[i].fired == b->runs[i].fired &&
               a->runs[i].active_ms == b->runs[i].active_ms;
    }
    v.Expect(same && ECS::Triggers::ScenarioOf(loaded.GetRegistry()) != nullptr &&
                 ECS::Triggers::ReadDisplay(loaded.GetRegistry())->lines.size() ==
                     ECS::Triggers::ReadDisplay(reg)->lines.size(),
             "the loaded scenario game's triggers are not the saved ones");
    for (int t = 0; t < 3 * kTicksPerSecond && v.ok(); ++t) {
        sim.Update(16);
        loaded.Update(16);
        if (t % 30 == 0 && ECS::Digest::Of(sim.GetRegistry()) != ECS::Digest::Of(loaded.GetRegistry())) {
            v.Fail("the loaded scenario game drifted from the one never stopped");
        }
    }
}

// Every computer seat the campaigns give a script of its own plays it: the
// scenario's script is what its brain loaded first, and it loads whole.
void CheckScripts(Verdict& v) {
    int scripted = 0;
    for (const char* path : kCampaigns) {
        Scenario::Campaign campaign;
        std::string error;
        if (!Scenario::LoadCampaign(path, campaign, &error)) continue;
        for (size_t i = 0; i < campaign.scenarios.size() && v.ok(); ++i) {
            Core::GameSetup setup = Core::DefaultSetup();
            if (!Scenario::SetupFor(path, static_cast<int>(i), setup, &error)) continue;
            ECS::Simulation sim;
            sim.Init(setup);
            const auto& reg = sim.GetRegistry();
            const Scenario::File* file = ECS::Triggers::ScenarioOf(reg);
            const AI::Brains* brains = reg.ctx().find<AI::Brains>();
            if (file == nullptr || brains == nullptr) {
                v.Fail(campaign.scenarios[i].file + " built no scenario or no brains");
                return;
            }
            for (const AI::Brain& b : brains->brains) {
                if (file->players[b.player].ai_script.empty()) continue;
                ++scripted;
                v.Expect(!b.program.files.empty() && b.program.files.front() == "scenario",
                         campaign.scenarios[i].file + "'s player " + std::to_string(b.player) +
                             " does not play its own script");
                v.Expect(b.program.errors.empty() && !b.program.rules.empty(),
                         campaign.scenarios[i].file + "'s player " + std::to_string(b.player) +
                             "'s script did not load: " +
                             (b.program.errors.empty() ? std::string("no rules") : b.program.errors.front()));
            }
            Core::CommandQueue::Get().PopAllCommands();
        }
    }
    v.Expect(scripted > 0, "no campaign seat has a script of its own");
}

// Joan of Arc's first scenario: the difficulty, a timer, and Joan's death
// lost through two triggers and a Declare Victory.
void CheckJoan(Verdict& v) {
    Core::GameSetup setup = Core::DefaultSetup();
    std::string error;
    if (!Scenario::SetupFor(kJoan, 0, setup, &error)) {
        v.Fail("Joan_scn1 gave no lobby: " + error);
        return;
    }
    // "Easy difficulty"'s first ground, the file's (78, 65)-(81, 69) turned.
    const Scenario::Area camp{65, 65, 69, 62};
    // Moderate keeps the camp; Standard ("Easy") and Easiest both clear it.
    for (int difficulty : {2, 4, 3}) {
        setup.difficulty = difficulty;
        ECS::Simulation sim;
        sim.Init(Quiet(setup));
        auto& reg = sim.GetRegistry();
        const int before = CountIn(reg, 2, camp);
        v.Expect(before > 0, "Joan_scn1 has no Burgundians on the camp's ground");
        v.Expect(!sim.player_starts.empty() && sim.player_starts[0] == std::pair<int, int>(80, 2),
                 "Joan_scn1's game does not open on Joan's escort, the file's (141, 80) turned");
        v.Expect(!ECS::Techs::Researched(reg, 1, 374), "Joan's escort has tech 374 before its trigger");
        Core::CommandQueue::Get().PopAllCommands();
        Run(sim, 2);
        const int after = CountIn(reg, 2, camp);
        if (difficulty == 2) {
            v.Expect(after == before && !Fired(reg, 31), "\"Easy difficulty\" fired on Moderate");
            continue;
        }
        v.Expect(after == 0 && Fired(reg, 31),
                 "\"Easy difficulty\" left the camp's Burgundians on difficulty " + std::to_string(difficulty));
        if (difficulty == 4) continue;

        // "Bert rides in": a timer of 3 seconds, then technologies.
        Run(sim, 2 * kTicksPerSecond);
        v.Expect(!Fired(reg, 18) && !ECS::Techs::Researched(reg, 1, 374), "\"Bert rides in\" fired before its 3 s");
        Run(sim, 2 * kTicksPerSecond);
        v.Expect(Fired(reg, 18) && ECS::Techs::Researched(reg, 1, 374) && ECS::Techs::Researched(reg, 1, 375) &&
                     On(reg, 25),
                 "\"Bert rides in\" did not research 374 and 375 and turn \"Jean rides in\" on");

        // Joan falls: "Joan doesn't make it", then "You lose 2" eight seconds on.
        const entt::entity joan = ECS::Triggers::EntityOf(reg, 6817);
        if (joan == entt::null) {
            v.Fail("Joan_scn1's Joan (6817) is not in the world");
            return;
        }
        ECS::Mortality::Kill(reg, sim.GetGrid(), joan);
        Run(sim, 2 * kTicksPerSecond);
        v.Expect(Fired(reg, 13) && ShownLine(reg, ECS::Triggers::Show::Instructions, 44722, "j1n", 13) &&
                     On(reg, 33),
                 "Joan's death did not fire \"Joan doesn't make it\"");
        const auto* outcome = ECS::Victory::Read(reg);
        v.Expect(outcome != nullptr && !outcome->over, "the game ended before \"You lose 2\"");
        Run(sim, 8 * kTicksPerSecond);
        outcome = ECS::Victory::Read(reg);
        v.Expect(Fired(reg, 33) && outcome != nullptr && outcome->over && outcome->Defeated(1) &&
                     outcome->Won(2) && outcome->Won(6),
                 "\"You lose 2\" did not end the game with players 2 and 6 the winners");
    }
}

// Saladin's first scenario: a chain of triggers ending in a loop that makes a
// camel each time it fires until another turns it off twenty seconds on --
// "make a line of camels", a line only if the loop fires once a second.
void CheckLoop(Verdict& v) {
    Core::GameSetup setup = Core::DefaultSetup();
    std::string error;
    if (!Scenario::SetupFor(kCampaigns[1], 0, setup, &error)) {
        v.Fail("Saladin_scn1 gave no lobby: " + error);
        return;
    }
    ECS::Simulation sim;
    sim.Init(Quiet(setup));
    auto& reg = sim.GetRegistry();
    auto camels = [&]() {
        int n = 0;
        auto view = reg.view<const C::Identity, const C::Owner>();
        for (auto e : view) {
            n += view.get<const C::Identity>(e).unit_id == 329 && view.get<const C::Owner>(e).player_id == 1 ? 1 : 0;
        }
        return n;
    };
    const int before = camels();
    // Player 1's soldiers on the ground of "Egypt betrays you", which turns
    // "Egypt rejoins p1" on, and on that one's, which starts the timer of
    // "New units from Egypt", which turns the camels on and their stop: the
    // file's tiles (10, 80) and (100, 55), turned.
    const int knight = Core::ConfigManager::Get().GetUnitIdByName("KNGHT");
    ECS::Units::Spawn(reg, knight, 1, {Fixed32(80), Fixed32(133)});
    ECS::Units::Spawn(reg, knight, 1, {Fixed32(55), Fixed32(43)});
    Core::CommandQueue::Get().PopAllCommands();
    Run(sim, 3 * kTicksPerSecond);
    v.Expect(Fired(reg, 7) && Fired(reg, 0) && On(reg, 1) && !Fired(reg, 1),
             "Saladin_scn1's \"Egypt betrays you\" did not turn on \"Egypt rejoins p1\" and its timer");
    Run(sim, 5 * kTicksPerSecond);
    v.Expect(Fired(reg, 1) && On(reg, 5) && On(reg, 6), "\"New units from Egypt\" did not start the camels");
    Run(sim, 25 * kTicksPerSecond);
    const int made = camels() - before;
    v.Expect(Fired(reg, 6) && !On(reg, 5), "\"stops p1 camels\" did not stop them");
    v.Expect(made >= 20 && made <= 21,
             "the camel loop made " + std::to_string(made) + " camels in its 20 seconds, not one a second");
}

// Genghis Khan's second scenario's 218 cliff pieces join in the world
// (CliffsJoin), which they do only when the file is turned onto the grid's
// frame the way the dat's offsets are.
void CheckCliffs(Verdict& v) {
    Core::GameSetup setup = Core::DefaultSetup();
    if (!Scenario::SetupFor(kCampaigns[2], 1, setup)) {
        v.Fail("Mongols_scn2 gave no lobby");
        return;
    }
    ECS::Simulation sim;
    sim.Init(Quiet(setup));
    int pieces = 0;
    auto all = sim.GetRegistry().view<const C::Identity>();
    for (auto e : all) {
        const int unit = all.get<const C::Identity>(e).unit_id;
        pieces += unit >= 264 && unit <= 273 ? 1 : 0;
    }
    v.Expect(pieces == 218, "Mongols_scn2 placed " + std::to_string(pieces) + " of its 218 cliff pieces");
    if (const char* wrong = CliffsJoin(sim)) v.Fail(std::string("Mongols_scn2's cliffs: ") + wrong);
    Core::CommandQueue::Get().PopAllCommands();
}

// The learning campaign's second scenario has one player: no one to conquer,
// so standing alone wins nothing -- its triggers end it.
void CheckAlone(Verdict& v) {
    Core::GameSetup setup = Core::DefaultSetup();
    if (!Scenario::SetupFor(kCampaigns[4], 1, setup)) {
        v.Fail("Wallace_scn2 gave no lobby");
        return;
    }
    v.Expect(setup.players[1].active && !setup.players[2].active, "Wallace_scn2 is not a game for one");
    ECS::Simulation sim;
    sim.Init(Quiet(setup));
    Core::CommandQueue::Get().PopAllCommands();
    Run(sim, 3 * kTicksPerSecond);
    const auto* outcome = ECS::Victory::Read(sim.GetRegistry());
    v.Expect(outcome != nullptr && !outcome->over, "a scenario for one player was won by conquest at once");
}

// DEMO.SCN recorded with a selection and an order, and played back from the
// file: the same game, the selection reaching the triggers in the replay.
void CheckRecorded(Verdict& v) {
    Core::GameSetup setup = Core::DefaultSetup();
    if (!Scenario::SetupFor(kDemo, -1, setup)) {
        v.Fail("DEMO.SCN gave no lobby to record");
        return;
    }
    setup = Quiet(setup);
    Save::Recording recording;
    recording.setup = setup;
    ECS::Simulation played;
    played.Init(setup);
    auto& reg = played.GetRegistry();
    entt::entity farm = ECS::Triggers::EntityOf(reg, 7556);
    entt::entity unit = entt::null;
    for (const auto& u : ECS::Triggers::ScenarioOf(reg)->players[1].units) {
        const auto* rules = Core::Rules::Unit(u.unit_id);
        if (rules != nullptr && rules->type == Core::Rules::UnitType::Creatable && u.garrisoned_in < 0) {
            unit = ECS::Triggers::EntityOf(reg, u.id);
            break;
        }
    }
    auto& queue = Core::CommandQueue::Get();
    queue.PopAllCommands();
    for (int t = 0; t < 5 * kTicksPerSecond; ++t) {
        if (t == 20) queue.PushCommand(Core::SelectionCommand{{farm}, 1});
        if (t == 40 && unit != entt::null) {
            const auto& at = reg.get<C::Transform>(unit).position;
            queue.PushCommand(Core::MoveCommand{unit, at.x + Fixed32(3), at.y, 1});
        }
        Save::TakeTurn(recording, played.Tick() + 1);
        played.Update(16);
        Save::Witness(recording, played);
    }
    const auto path = std::filesystem::temp_directory_path() / "openaok_verify_scenario.mgl";
    Save::Recording read;
    if (!Save::WriteRecording(recording, path.string()) || !Save::ReadRecording(path.string(), read)) {
        std::filesystem::remove(path);
        v.Fail("a recorded scenario game did not survive its file");
        return;
    }
    std::filesystem::remove(path);
    v.Expect(read.setup.scenario == kDemo && read.setup.players[4].starting_age == 3,
             "the recording's lobby lost its scenario");
    ECS::Simulation replay;
    replay.Init(read.setup);
    size_t turn = 0, check = 0;
    bool same = true;
    for (int t = 0; t < 5 * kTicksPerSecond; ++t) {
        Save::Replay(read, turn, replay.Tick() + 1);
        replay.Update(16);
        same = same && Save::StillTheGame(read, check, replay);
    }
    queue.PopAllCommands();
    const auto* state = ECS::Triggers::Read(replay.GetRegistry());
    v.Expect(same && ECS::Digest::Of(replay.GetRegistry()) == ECS::Digest::Of(reg),
             "the recorded scenario game played back is not the game recorded");
    v.Expect(state != nullptr && state->selected[1].size() == 1 && state->selected[1][0] == farm,
             "the recorded selection did not reach the replay's triggers");
}

// The progress record, on a file of its own.
void CheckProgress(Verdict& v) {
    const auto path = std::filesystem::temp_directory_path() / "openaok_verify_campaigns.txt";
    std::filesystem::remove(path);
    Scenario::Progress::Book book = Scenario::Progress::Load(path.string());
    v.Expect(book.records.empty() && Scenario::Progress::Unlocked(book, "Verify", "cam1") == 1 &&
                 Scenario::Progress::Playable(book, "Verify", "cam1", 0) &&
                 !Scenario::Progress::Playable(book, "Verify", "cam1", 1),
             "a new player's campaign does not open on its first scenario alone");
    Scenario::Progress::MarkWon(book, "Verify", "cam1", 0, 6);
    v.Expect(Scenario::Progress::Unlocked(book, "Verify", "cam1") == 2 &&
                 Scenario::Progress::Won(book, "Verify", "cam1", 0) &&
                 !Scenario::Progress::Won(book, "Verify", "cam1", 1) &&
                 Scenario::Progress::Unlocked(book, "Other", "cam1") == 1,
             "winning the first scenario did not open the second, and only for its player");
    Scenario::Progress::MarkWon(book, "Verify", "cam1", 5, 6);
    if (!Scenario::Progress::Save(book, path.string())) v.Fail("the progress record was not written");
    const Scenario::Progress::Book back = Scenario::Progress::Load(path.string());
    v.Expect(back.records.size() == 1 && back.records[0].unlocked == 6 && back.records[0].won == 0x21u,
             "the progress record did not come back as written");
    std::filesystem::remove(path);
}

// AOE_SCENARIO_SWEEP=SECONDS: every scenario on the disc built and played
// that long with the computer at its seats, as the game would -- for working
// on the scenarios, not part of the gate. One line each: units placed of the
// file's, triggers fired, how the game stands.
void Sweep(int seconds) {
    std::vector<std::pair<std::string, int>> all{{kDemo, -1}};
    for (const char* path : kCampaigns) {
        Scenario::Campaign campaign;
        if (!Scenario::LoadCampaign(path, campaign)) continue;
        for (size_t i = 0; i < campaign.scenarios.size(); ++i) all.emplace_back(path, static_cast<int>(i));
    }
    for (const auto& [path, entry] : all) {
        Scenario::File file;
        Core::GameSetup setup = Core::DefaultSetup();
        if (!Scenario::Load(path, entry, file) || !Scenario::SetupFor(path, entry, setup)) {
            std::printf("sweep %s:%d unread\n", path.c_str(), entry);
            continue;
        }
        ECS::Simulation sim;
        sim.Init(setup);
        size_t expected = file.players[0].units.size();
        for (int p = 1; p <= Scenario::kPlayers; ++p) {
            if (file.players[p].active) expected += file.players[p].units.size();
        }
        const auto* state = ECS::Triggers::Read(sim.GetRegistry());
        const size_t placed = state != nullptr ? state->placed.size() : 0;
        Core::CommandQueue::Get().PopAllCommands();
        Run(sim, 3 * kTicksPerSecond);
        const auto* early = ECS::Victory::Read(sim.GetRegistry());
        const unsigned defeated_early = early != nullptr ? early->defeated : 0u;
        Run(sim, seconds * kTicksPerSecond);
        int fired = 0;
        std::string names;
        const auto& runs = ECS::Triggers::Read(sim.GetRegistry())->runs;
        for (size_t i = 0; i < runs.size(); ++i) {
            if (!runs[i].fired) continue;
            ++fired;
            names += " [" + file.triggers[i].name + "]";
        }
        const auto* outcome = ECS::Victory::Read(sim.GetRegistry());
        std::printf("sweep %-20s placed %zu/%zu, defeated at 3 s 0x%x, %s, fired %d/%zu:%s\n",
                    entry < 0 ? "DEMO.SCN" : file.original_name.c_str(), placed, expected, defeated_early,
                    outcome != nullptr && outcome->over ? "over" : "playing", fired, file.triggers.size(),
                    names.c_str());
    }
}

} // namespace

bool CheckScenario() {
    if (const char* sweep = std::getenv("AOE_SCENARIO_SWEEP")) Sweep(std::atoi(sweep));
    Verdict v;
    CheckFiles(v);
    Scenario::File demo;
    std::string error;
    if (v.ok() && !Scenario::Load(kDemo, -1, demo, &error)) v.Fail("DEMO.SCN unread: " + error);
    if (v.ok()) CheckDemoFile(v, demo);
    if (v.ok()) CheckCampaigns(v);
    if (v.ok()) CheckDemoWorld(v, demo);
    if (v.ok()) CheckJoan(v);
    if (v.ok()) CheckScripts(v);
    if (v.ok()) CheckLoop(v);
    if (v.ok()) CheckCliffs(v);
    if (v.ok()) CheckAlone(v);
    if (v.ok()) CheckRecorded(v);
    if (v.ok()) CheckProgress(v);
    Core::CommandQueue::Get().PopAllCommands();
    std::printf("scenario=%s\n", v.ok() ? "ok" : v.why.c_str());
    return v.ok();
}

} // namespace Verify
