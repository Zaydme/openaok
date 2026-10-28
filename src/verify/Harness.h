#pragma once

// The headless self-check behind `--verify`, one translation unit per scenario.
// Each scenario builds its own world from the same fixed seed, so the scenarios
// are independent of their order.
//
// Every scenario prints one machine-readable line. tools/check_determinism.sh
// greps for them, so their wording must not change.
#include <cstdint>

#include <entt/entt.hpp>

namespace ECS {
class Simulation;
}

namespace Verify {

// A digest of the world's state -- every entity's place, hit points, owner and
// identity, in entity order. Two worlds that should be the same, and are not,
// have different fingerprints.
uint64_t Fingerprint(const entt::registry& registry);

// Whether the map generated and the simulation stayed bit-identical over
// `ticks`, and whether every unit given a move order reached it.
// Prints "verify:", "arrived=" and "entities=".
bool CheckDeterminism(int ticks);

// A villager sent at a resource: reach it, take from it, carry the load to a
// drop site, hand it over. Prints "gathering=".
bool CheckGathering();

// A villager sent to drop off at one of the player's buildings: all of its
// load handed over, a part of a unit too, and then it stands; only at a
// finished building of its own that takes the load; going into the Town
// Center banks the load, rounded, and into a tower nothing. Prints
// "dropoff=".
bool CheckDropoff();

// Graphics run their dat sequences: a standing scout rests between plays of
// its idle, and a packed trebuchet standing keeps its wheels still.
bool CheckSequence();

// A lumberjack sent at a standing tree: the felling stroke with no wood cut,
// then the tree lying as its felled graphic, then wood cut from it.
// Prints "felling=".
bool CheckFelling();

// What the interface notifies the player of and the simulation has to tell
// it: a sheep claimed, a farm run out, a Wonder begun and seen by every
// player, production halted for want of houses, research finished at a
// building, Spies known for what it does, a building site found taken, a
// relic picked up. Prints "notices=".
bool CheckNotices();

// Foundations as the original lays them: no ground held, seen only by their
// side and nobody's target until begun; what placing runs into; the first
// begun of overlapping foundations standing and the other refunded; the
// footprint-only sight of a building going up; deleting's refunds; the dat's
// hill rules for the ground a building is placed on; and a dock's water and
// beach. Prints "foundations=".
bool CheckFoundations();

// What a player keeps of what it no longer sees: no building drawn that it
// never saw; a picture of one gone out of its sight, its alone, staying when
// the building is destroyed until its ground is seen; a picture of a mine used
// up unseen; the ground as it was where a farm was laid unseen. Prints "fog=".
bool CheckFog();

// A scenario's flag of a player's: seeing round itself, and drawn with a frame
// its graphic has. Prints "flag=".
bool CheckFlag();

// Spies' price for each enemy villager and the enemy sight it gives; none in a
// Death Match or Regicide game, where Treason marks the enemy Kings instead.
// Prints "spies=".
bool CheckSpies();

// Gaia's village taken by the first player to see it; its wild animals and
// relic carts not. Prints "capture=".
bool CheckCapture();

// The approach planner on its own, away from any worker using it.
// Prints "planner=".
bool CheckApproachPlanner();

// A crew raises a building and it is then demolished, watched throughout.
// Prints "building=".
bool CheckConstruction();

// A Town Centre trains villagers: price, refusals, the step out of the door,
// the population limit, and cancelling. Prints "production=".
bool CheckProduction();

// Technologies from the dat: civ bonuses and tech trees at the start, an age
// waiting on its buildings, research paid and timed, upgrades in place, and
// effects reaching units already in the field. Prints "techs=".
bool CheckTechs();

// Fighting by the dat's rules: the damage formula, reach measured between the
// dat's boxes, a duel to the death, an
// archer's arrows in flight, a tower shooting by itself, stand ground, a
// hunter killing a deer and gathering it, and a boar answering. Prints
// "combat=".
bool CheckCombat();

// Fog of war per player: each sees its own base and not the other's, ground a
// scout passes stays explored, an enemy building found is shown in the fog by
// the picture kept of it and an enemy unit not at all, and "all visible" is.
// Prints "vision=".
bool CheckVision();

// Sheltering in buildings: going in, refusals by kind and room, garrison
// arrows, healing, turning out, the town bell, and a garrison outliving its
// building. Prints "garrison=".
bool CheckGarrison();

// Animals by their task lists: herdables changing hands, prey fleeing, the
// wild grazing. Prints "animals=".
bool CheckAnimals();

// Standing orders: patrol, follow and guard. Prints "duties=".
bool CheckDuties();

// Monks: conversion and its timing, faith, buildings, healing, and facing
// whom they convert. Prints "monks=".
bool CheckMonks();

// The land pass of the random map scripts: islands, continents, beaches,
// starts on land, fish in water, cliffs that join. Prints "maps=".
bool CheckMaps();

// Whether a world's cliffs join: each piece sits at its own place in its 3x3
// cell, and every link is met by the next cell's piece linking back the same
// way, in the dat's frame turned onto the grid. Null when they do, else what is
// wrong. Used by maps= and scenario=.
const char* CliffsJoin(ECS::Simulation& sim);

// The computer playing both seats at its hardest: town, the Feudal Age by
// its threshold, army and a fight, and a twin game identical. Prints "ai=".
bool CheckAI();

// The computer players' rule language on its own: the reader (conditional
// loading, load, load-random, constants, errors), passes (rule order,
// disable-self, timers, logic, defaults, wildcards), every shipped AI file
// loading under every civilisation, difficulty and map, the words bound to the
// dat, and facts and actions bound to a world. Prints "script=".
bool CheckScript();

// Each difficulty's pace, one computer against an idle human seat: hard and
// moderate in the Feudal Age by their thresholds; easy and easiest never
// before the human, and soon after. Prints "pace=".
bool CheckPace();

// Seven computer players and a human at one table: every brain loaded, every
// town grown in three minutes. Prints "seven=".
bool CheckSeven();

// A computer player's attack across the water: on Islands, attack-now with
// nothing of the enemy's to walk to -- the group boards a transport, is
// landed on the enemy's island and strikes there. Prints "crossing=".
bool CheckCrossing();

// Two machines in lockstep over a loopback socket: orders carried out on
// both, worlds identical, tampering caught. Prints "net=".
bool CheckNet();

// The standard victory: conquest, a Wonder, the relics. Prints "victory=".
bool CheckVictory();

// Water as its own ground: the sea's own passability layer and regions, a
// ship routed round an island, ships trained at a dock and launched on the
// water side, a dock placed only where the dat lets one stand. Prints
// "water=".
bool CheckWater();

// A fishing ship trained at a Dock on the shore: to the fish, a load at the
// dat's rate, home to the Dock as food; a fish trap built on the water,
// holding the civ's fish trap food and worked at its own rate. Prints
// "fishing=".
bool CheckFishing();

// The warships and the Dock's technologies: what a Castle Age dock trains, a
// galley sinking a fishing ship, a fire ship burning a galley, a demolition
// ship's detonation and blast, Careening. Prints "navy=".
bool CheckNavy();

// A landing on Islands: a transport takes on a war party under the dat's
// garrison rules, sails it to the other island and puts it ashore, and the
// party's assault ends the game in conquest; a transport sunk with a soldier
// aboard takes him with it. Prints "landing=".
bool CheckLanding();

// The Market: the opening prices, a lot sold and bought at the price on its
// button, the prices moving for every player, refusals that change nothing,
// several lots in one order, the floor, and Guilds. Prints "market=".
bool CheckMarket();

// Trade: who may trade where, a trade cart's round trips between Markets
// paid by the trade formula, the full cart, a partner and a home lost, a
// cart rallied onto a foreign Market, a trade cog between Docks. Prints
// "trade=".
bool CheckTrade();

// Tribute: the Market it needs, the fee on top, Coinage and Banking, the
// refusals, and teams locked to allies only. Prints "tribute=".
bool CheckTribute();

// Diplomacy: the lobby's stances, team bonuses and Lock Teams; stances
// changed in play; targeting that follows them; Cartography's shared sight;
// allied victory. Prints "diplomacy=".
bool CheckDiplomacy();

// Chat and flares as orders: who hears a message, taunts, refusals, Lock
// Teams, and who sees a flare and for how long. Prints "chat=".
bool CheckChat();
// Walls and gates: a dragged line laid as foundations at the dat's price, each
// segment the piece its neighbours choose, a tower joining a wall, one
// villager raising a whole line, a Gate turning to its wall and replacing it,
// going up as its site unit -- that unit's scaffold, shadow and time -- open
// to its owner and ally and shut to the enemy, and locked, to all.
// Prints "walls=".
bool CheckWalls();

// Repairs: a Barracks mended at the repairer's rate and charged half its cost
// over its hit points, a crew faster, a trebuchet at a quarter pace, a repair
// stopping when the wood runs out, no repair of an enemy's or of a ram.
// Prints "repair=".
bool CheckRepair();

// Trebuchets packing and unpacking: setting up still and taking the packing
// time, armed set up and unable to move, packed and moving, a stop leaving
// it as it was, and sent at a building out of range, closing, setting up in
// range and shooting; their icons from the units' sheet; and placed as units
// from a scenario or the editor. Prints "packing=".
bool CheckPacking();

// A converted unit keeps its maker's technology: its maker's Forging, not its
// new owner's Scale Mail but its maker's later one, its maker's line upgrade.
// Prints "converted=".
bool CheckConversion();

// The lobby's games and options: Regicide's start and its defeat, Death
// Match's stockpiles, the population limit, the map revealed, the starting
// age, All Techs. Prints "modes=".
bool CheckModes();

// The cheat codes: recognised from chat, refused without the lobby's leave,
// and each doing what it does. Prints "cheats=".
bool CheckCheats();

// The score: each scoring element's figure at the
// moment its event happens, the Achievements' statistics, the map explored
// once a second, the team figure, and a save carrying it all. Prints
// "score=".
bool CheckScore();

// Runs every scenario. Returns a process exit status: 0 when all passed.
int Run(int ticks);

// A game saved part way through and loaded into a fresh world runs on as the
// one that never stopped, and a damaged save is refused. Prints "save=".
bool CheckSave();

// A recorded game played back from its file is the game recorded at every
// digest it noted. Prints "recording=".
bool CheckRecording();

// The shipped scenarios and campaigns: all 32 read whole, DEMO.SCN's and
// Joan of Arc's first scenario's facts as the files state them, their worlds
// built as the files lay them out, triggers firing on their conditions with
// their effects done, a scenario game saved and loaded, and the campaign
// progress record. Prints "scenario=".
bool CheckScenario();

// Waypoints: three laid at once walked in order and the route gone at its
// end; a move part way along forgetting the rest; a group keeping its
// formation at each point. Prints "waypoints=".
bool CheckWaypoints();

// The scenario writer: every one of the disc's 32 scenarios read, written and
// read again is the same file, field by field and, inflated, byte by byte;
// an editor's changes survive a write; a new file reads. Prints "editor=".
bool CheckEditor();

} // namespace Verify
