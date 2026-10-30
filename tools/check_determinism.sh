#!/usr/bin/env bash
# Determinism check for the simulation: a golden state hash, plus a verdict per
# scenario.
#
# The simulation is deterministic 32.32 fixed point on a fixed 60 Hz tick, so
# running it N ticks from a fixed seed must produce a bit-identical result on
# every machine, every build and every frame rate.
#
# The hash alone cannot tell a unit that walked to its destination from one
# that stopped halfway and stayed put -- both are reproducible. So the run also
# reports how many units reached their orders and whether each scenario
# passed, and the check fails on any of them.
#
# Update GOLDEN only when a simulation change is intended.
#
# Usage: tools/check_determinism.sh
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TICKS=3000
GOLDEN="entities=1970 ticks=3000 hash=469215ad506be71e"

# Build first; stop on failure.
if ! "$ROOT/build.sh" >/dev/null 2>&1; then
    echo "build failed; determinism not checked" >&2
    exit 1
fi

cd "$ROOT/build"
# The run exits non-zero when any scenario fails. Its status is ignored so that
# `set -e` does not stop the script; each verdict line is judged below instead.
OUTPUT="$(./OpenAoK --verify "$TICKS" 2>/dev/null || true)"
ACTUAL="$(echo "$OUTPUT" | grep '^entities=')"
ARRIVAL="$(echo "$OUTPUT" | grep '^arrived=')"
BUILDING="$(echo "$OUTPUT" | grep '^building=')"
PLANNER="$(echo "$OUTPUT" | grep '^planner=')"
GATHERING="$(echo "$OUTPUT" | grep '^gathering=')"
DROPOFF="$(echo "$OUTPUT" | grep '^dropoff=')"
FELLING="$(echo "$OUTPUT" | grep '^felling=')"
SEQUENCE="$(echo "$OUTPUT" | grep '^sequence=')"
COLLAPSE="$(echo "$OUTPUT" | grep '^collapse=')"
SHIPANGLES="$(echo "$OUTPUT" | grep '^shipangles=')"
TURNING="$(echo "$OUTPUT" | grep '^turning=')"
POCKET="$(echo "$OUTPUT" | grep '^pocket=')"
COLOURS="$(echo "$OUTPUT" | grep '^colours=')"
BUTTONS="$(echo "$OUTPUT" | grep '^buttons=')"
SEARCH="$(echo "$OUTPUT" | grep '^search=')"
HUNT="$(echo "$OUTPUT" | grep '^hunt=')"
ANSWER="$(echo "$OUTPUT" | grep '^answer=')"
WOLVES="$(echo "$OUTPUT" | grep '^wolves=')"
LOBBY="$(echo "$OUTPUT" | grep '^lobby=')"
HERDING="$(echo "$OUTPUT" | grep '^herding=')"
CROWD="$(echo "$OUTPUT" | grep '^crowd=')"
SPREAD="$(echo "$OUTPUT" | grep '^spread=')"
NOTICES="$(echo "$OUTPUT" | grep '^notices=')"
FOUNDATIONS="$(echo "$OUTPUT" | grep '^foundations=')"
FOG="$(echo "$OUTPUT" | grep '^fog=')"
FLAG="$(echo "$OUTPUT" | grep '^flag=')"
SPIES="$(echo "$OUTPUT" | grep '^spies=')"
CAPTURE="$(echo "$OUTPUT" | grep '^capture=')"
PRODUCTION="$(echo "$OUTPUT" | grep '^production=')"
TECHS="$(echo "$OUTPUT" | grep '^techs=')"
COMBAT="$(echo "$OUTPUT" | grep '^combat=')"
VISION="$(echo "$OUTPUT" | grep '^vision=')"
GARRISON="$(echo "$OUTPUT" | grep '^garrison=')"
ANIMALS="$(echo "$OUTPUT" | grep '^animals=')"
DUTIES="$(echo "$OUTPUT" | grep '^duties=')"
MONKS="$(echo "$OUTPUT" | grep '^monks=')"
MAPS="$(echo "$OUTPUT" | grep '^maps=')"
AI="$(echo "$OUTPUT" | grep '^ai=')"
SCRIPT="$(echo "$OUTPUT" | grep '^script=')"
PACE="$(echo "$OUTPUT" | grep '^pace=')"
SEVEN="$(echo "$OUTPUT" | grep '^seven=')"
CROSSING="$(echo "$OUTPUT" | grep '^crossing=')"
NET="$(echo "$OUTPUT" | grep '^net=')"
VICTORY="$(echo "$OUTPUT" | grep '^victory=')"
WATER="$(echo "$OUTPUT" | grep '^water=')"
FISHING="$(echo "$OUTPUT" | grep '^fishing=')"
NAVY="$(echo "$OUTPUT" | grep '^navy=')"
LANDING="$(echo "$OUTPUT" | grep '^landing=')"
MARKET="$(echo "$OUTPUT" | grep '^market=')"
TRADE="$(echo "$OUTPUT" | grep '^trade=')"
TRIBUTE="$(echo "$OUTPUT" | grep '^tribute=')"
DIPLOMACY="$(echo "$OUTPUT" | grep '^diplomacy=')"
CHAT="$(echo "$OUTPUT" | grep '^chat=')"
SCORE="$(echo "$OUTPUT" | grep '^score=')"
SAVE="$(echo "$OUTPUT" | grep '^save=')"
RECORDING="$(echo "$OUTPUT" | grep '^recording=')"
WALLS="$(echo "$OUTPUT" | grep '^walls=')"
REPAIR="$(echo "$OUTPUT" | grep '^repair=')"
PACKING="$(echo "$OUTPUT" | grep '^packing=')"
CONVERTED="$(echo "$OUTPUT" | grep '^converted=')"
MODES="$(echo "$OUTPUT" | grep '^modes=')"
CHEATS="$(echo "$OUTPUT" | grep '^cheats=')"
SCENARIO="$(echo "$OUTPUT" | grep '^scenario=')"
WAYPOINTS="$(echo "$OUTPUT" | grep '^waypoints=')"
EDITOR="$(echo "$OUTPUT" | grep '^editor=')"

STATUS=0

# No floating point in simulation code: floats differ across CPUs and
# compilers, which breaks lockstep, and the hash cannot see that until two
# machines disagree. Comment lines are skipped; fractional data is converted to
# fixed point once, at load, in ConfigManager.
FLOATS="$(grep -rnE '\b(float|double)\b|ToFloat\(\)' \
    "$ROOT/src/ecs" "$ROOT/src/pathfinding" "$ROOT/src/physics" "$ROOT/src/ai" \
    | grep -vE '^[^:]+:[0-9]+:\s*//' || true)"   # no match is the good case
if [ -z "$FLOATS" ]; then
    echo "fixed point OK: no float in simulation code"
else
    echo "FLOATING POINT IN SIMULATION CODE" >&2
    echo "$FLOATS" | sed 's/^/  /' >&2
    STATUS=1
fi
if [ "$ACTUAL" = "$GOLDEN" ]; then
    echo "determinism OK: $ACTUAL"
else
    echo "DETERMINISM CHANGED" >&2
    echo "  expected: $GOLDEN" >&2
    echo "  actual:   $ACTUAL" >&2
    STATUS=1
fi

# "arrived=3/3" and nothing less.
COUNTS="${ARRIVAL#arrived=}"
COUNTS="${COUNTS%% *}"
if [ -n "$COUNTS" ] && [ "${COUNTS%%/*}" = "${COUNTS##*/}" ]; then
    echo "movement OK: $ARRIVAL"
else
    echo "UNITS FAILED TO REACH THEIR ORDERS" >&2
    echo "  $ARRIVAL" >&2
    STATUS=1
fi

# The approach planner on its own, away from any villager using it.
if [ "$PLANNER" = "planner=ok" ]; then
    echo "planner OK"
else
    echo "APPROACH PLANNER FAILED" >&2
    echo "  $PLANNER" >&2
    STATUS=1
fi

# The whole gathering round trip: reach the resource, take from it, carry the
# load to a drop site, hand it over.
if [ "$GATHERING" = "gathering=ok" ]; then
    echo "gathering OK"
else
    echo "GATHERING FAILED" >&2
    echo "  $GATHERING" >&2
    STATUS=1
fi

# Drop-off by order, and the load banked on going into the Town Center.
if [ "$DROPOFF" = "dropoff=ok" ]; then
    echo "dropoff OK"
else
    echo "DROPOFF FAILED" >&2
    echo "  $DROPOFF" >&2
    STATUS=1
fi

# A tree felled with a stroke before its wood is cut, then lying as its felled
# graphic.
# Graphics run their dat sequences: a standing scout rests between plays of its
# idle, and a packed trebuchet standing keeps its wheels still.
if [ "$SEQUENCE" = "sequence=ok" ]; then
    echo "sequence OK"
else
    echo "GRAPHIC SEQUENCES FAILED" >&2
    echo "  $SEQUENCE" >&2
    STATUS=1
fi

# A destroyed building falls in its dat dying graphic over its rubble.
if [ "$COLLAPSE" = "collapse=ok" ]; then
    echo "collapse OK"
else
    echo "BUILDING COLLAPSE FAILED" >&2
    echo "  $COLLAPSE" >&2
    STATUS=1
fi

# A ship shows the nearest of its graphic's sixteen angles, a soldier of eight.
if [ "$SHIPANGLES" = "shipangles=ok" ]; then
    echo "shipangles OK"
else
    echo "SHIP ANGLES FAILED" >&2
    echo "  $SHIPANGLES" >&2
    STATUS=1
fi

# A ship or a trebuchet turns to its target an angle at a time before it shoots.
if [ "$TURNING" = "turning=ok" ]; then
    echo "turning OK"
else
    echo "TURNING FAILED" >&2
    echo "  $TURNING" >&2
    STATUS=1
fi

# A computer player never plays in a person's colour.
if [ "$COLOURS" = "colours=ok" ]; then
    echo "colours OK"
else
    echo "COLOURS FAILED" >&2
    echo "  $COLOURS" >&2
    STATUS=1
fi

# The command buttons and their hotkeys: Convert, Heal, Unload, Set Gather
# Point, Pack and Unpack.
if [ "$BUTTONS" = "buttons=ok" ]; then
    echo "buttons OK"
else
    echo "BUTTONS FAILED" >&2
    echo "  $BUTTONS" >&2
    STATUS=1
fi

# A wolf goes for villagers and soldiers, never a building, a ram, a Monk or
# a Trade Cart; a soldier goes for a wolf only when the computer plays it.
if [ "$SEARCH" = "search=ok" ]; then
    echo "search OK"
else
    echo "SEARCH FAILED" >&2
    echo "  $SEARCH" >&2
    STATUS=1
fi

# A wolf waits for people to come near, nearer at the easier difficulties.
if [ "$HUNT" = "hunt=ok" ]; then
    echo "hunt OK"
else
    echo "HUNT FAILED" >&2
    echo "  $HUNT" >&2
    STATUS=1
fi

# A villager struck at work fights off a wolf and runs from a soldier, then
# goes back to work; the idle beside it help.
if [ "$ANSWER" = "answer=ok" ]; then
    echo "answer OK"
else
    echo "ANSWER FAILED" >&2
    echo "  $ANSWER" >&2
    STATUS=1
fi

# A scout set on two wolves wounds the one it swings at with every swing.
if [ "$WOLVES" = "wolves=ok" ]; then
    echo "wolves OK"
else
    echo "WOLVES FAILED" >&2
    echo "  $WOLVES" >&2
    STATUS=1
fi

# A network game's lobby: found, joined, readied, chatted in and started.
if [ "$LOBBY" = "lobby=ok" ]; then
    echo "lobby OK"
else
    echo "LOBBY FAILED" >&2
    echo "  $LOBBY" >&2
    STATUS=1
fi

# Villagers sent at an animal no walk reaches give it up.
if [ "$POCKET" = "pocket=ok" ]; then
    echo "pocket OK"
else
    echo "POCKET FAILED" >&2
    echo "  $POCKET" >&2
    STATUS=1
fi

# Villagers sent at a sheep kill it, take its food within the dat's range and
# bring it home.
if [ "$HERDING" = "herding=ok" ]; then
    echo "herding OK"
else
    echo "HERDING FAILED" >&2
    echo "  $HERDING" >&2
    STATUS=1
fi

# A crowd at one sheep works side by side, the rest standing by, none staying
# over another.
if [ "$CROWD" = "crowd=ok" ]; then
    echo "crowd OK"
else
    echo "CROWD FAILED" >&2
    echo "  $CROWD" >&2
    STATUS=1
fi

# Villagers sent together to a point stand about it side by side.
if [ "$SPREAD" = "spread=ok" ]; then
    echo "spread OK"
else
    echo "SPREAD FAILED" >&2
    echo "  $SPREAD" >&2
    STATUS=1
fi

if [ "$FELLING" = "felling=ok" ]; then
    echo "felling OK"
else
    echo "FELLING FAILED" >&2
    echo "  $FELLING" >&2
    STATUS=1
fi

# What the player is notified of: a sheep claimed, a farm run out, a Wonder
# begun, production halted for want of houses, a building site found taken, a
# relic picked up.
if [ "$NOTICES" = "notices=ok" ]; then
    echo "notices OK"
else
    echo "NOTICES FAILED" >&2
    echo "  $NOTICES" >&2
    STATUS=1
fi

# Foundations: ground, sight and visibility until begun; placing; overlaps;
# refunds.
if [ "$FOUNDATIONS" = "foundations=ok" ]; then
    echo "foundations OK"
else
    echo "FOUNDATIONS FAILED" >&2
    echo "  $FOUNDATIONS" >&2
    STATUS=1
fi

# Fog memory: pictures of buildings and map objects out of sight; ground.
if [ "$FOG" = "fog=ok" ]; then
    echo "fog OK"
else
    echo "FOG FAILED" >&2
    echo "  $FOG" >&2
    STATUS=1
fi

# A scenario's flag seeing round itself, drawn with a frame it has.
if [ "$FLAG" = "flag=ok" ]; then
    echo "flag OK"
else
    echo "FLAG FAILED" >&2
    echo "  $FLAG" >&2
    STATUS=1
fi

# Spies' price and the enemy sight it gives; Treason in Regicide.
if [ "$SPIES" = "spies=ok" ]; then
    echo "spies OK"
else
    echo "SPIES FAILED" >&2
    echo "  $SPIES" >&2
    STATUS=1
fi

# Gaia's village taken by the first player to see it.
if [ "$CAPTURE" = "capture=ok" ]; then
    echo "capture OK"
else
    echo "CAPTURE FAILED" >&2
    echo "  $CAPTURE" >&2
    STATUS=1
fi

# Buildings training units: the dat's price paid at queue time, refusals that
# charge nothing, a fully formed unit stepping out beside the building, the
# population limit and the resume when a House stands, and a full refund.
if [ "$PRODUCTION" = "production=ok" ]; then
    echo "production OK"
else
    echo "PRODUCTION FAILED" >&2
    echo "  $PRODUCTION" >&2
    STATUS=1
fi

# Technologies from the dat: civ bonuses and tech trees applied at the start,
# the Feudal Age waiting on two buildings, research paid and timed, buildings
# upgraded in place with their art, and Loom reaching villagers in the field.
if [ "$TECHS" = "techs=ok" ]; then
    echo "techs OK"
else
    echo "TECHNOLOGIES FAILED" >&2
    echo "  $TECHS" >&2
    STATUS=1
fi

# Fighting by the dat's rules: the damage formula, a duel fought to a death and
# a body, arrows that fly before they hurt, a tower shooting by itself, stand
# ground, a hunter killing a deer and gathering it, a boar answering.
if [ "$COMBAT" = "combat=ok" ]; then
    echo "combat OK"
else
    echo "COMBAT FAILED" >&2
    echo "  $COMBAT" >&2
    STATUS=1
fi

# Fog of war per player, as the simulation keeps it.
if [ "$VISION" = "vision=ok" ]; then
    echo "vision OK"
else
    echo "VISION FAILED" >&2
    echo "  $VISION" >&2
    STATUS=1
fi

# Sheltering in buildings: the garrison, its arrows, and the town bell.
if [ "$GARRISON" = "garrison=ok" ]; then
    echo "garrison OK"
else
    echo "GARRISON FAILED" >&2
    echo "  $GARRISON" >&2
    STATUS=1
fi

# Animals: herdables changing hands, prey running, the wild grazing.
if [ "$ANIMALS" = "animals=ok" ]; then
    echo "animals OK"
else
    echo "ANIMALS FAILED" >&2
    echo "  $ANIMALS" >&2
    STATUS=1
fi

# Standing orders: patrol, follow, guard.
if [ "$DUTIES" = "duties=ok" ]; then
    echo "duties OK"
else
    echo "DUTIES FAILED" >&2
    echo "  $DUTIES" >&2
    STATUS=1
fi

# Monks: conversion, faith, healing.
if [ "$MONKS" = "monks=ok" ]; then
    echo "monks OK"
else
    echo "MONKS FAILED" >&2
    echo "  $MONKS" >&2
    STATUS=1
fi

# The land pass: islands, continents, beaches, fish.
if [ "$MAPS" = "maps=ok" ]; then
    echo "maps OK"
else
    echo "MAPS FAILED" >&2
    echo "  $MAPS" >&2
    STATUS=1
fi

# The computer players' rule language: the reader, passes, the CD's AI
# files under every setting, the words bound to the dat, facts in a world.
if [ "$SCRIPT" = "script=ok" ]; then
    echo "script OK"
else
    echo "AI SCRIPTS FAILED" >&2
    echo "  $SCRIPT" >&2
    STATUS=1
fi

# The computer players: twenty minutes of a game played unattended.
if [ "$AI" = "ai=ok" ]; then
    echo "ai OK"
else
    echo "AI FAILED" >&2
    echo "  $AI" >&2
    STATUS=1
fi

# Each difficulty's pace: hard and moderate in the Feudal Age by their
# thresholds, easy and easiest never before the human and soon after.
if [ "$PACE" = "pace=ok" ]; then
    echo "pace OK"
else
    echo "AI PACE FAILED" >&2
    echo "  $PACE" >&2
    STATUS=1
fi

# Seven computer players at one table.
if [ "$SEVEN" = "seven=ok" ]; then
    echo "seven OK"
else
    echo "SEVEN COMPUTER PLAYERS FAILED" >&2
    echo "  $SEVEN" >&2
    STATUS=1
fi

# A computer player's attack carried across the water by transport.
if [ "$CROSSING" = "crossing=ok" ]; then
    echo "crossing OK"
else
    echo "AI CROSSING FAILED" >&2
    echo "  $CROSSING" >&2
    STATUS=1
fi

# Lockstep: two machines over a socket, in step, and caught when not.
if [ "$NET" = "net=ok" ]; then
    echo "net OK"
else
    echo "NET FAILED" >&2
    echo "  $NET" >&2
    STATUS=1
fi

# How a game ends: conquest, a Wonder, the relics.
if [ "$VICTORY" = "victory=ok" ]; then
    echo "victory OK"
else
    echo "VICTORY FAILED" >&2
    echo "  $VICTORY" >&2
    STATUS=1
fi

# Water as its own ground: the sea's layer and regions, a ship round an
# island, and later the dock and its ships.
if [ "$WATER" = "water=ok" ]; then
    echo "water OK"
else
    echo "WATER FAILED" >&2
    echo "  $WATER" >&2
    STATUS=1
fi

# A fishing ship's round trip from a Dock on the shore, fish traps, and the
# computer keeping a fishing fleet on Islands.
if [ "$FISHING" = "fishing=ok" ]; then
    echo "fishing OK"
else
    echo "FISHING FAILED" >&2
    echo "  $FISHING" >&2
    STATUS=1
fi

# The warships and the Dock's technologies.
if [ "$NAVY" = "navy=ok" ]; then
    echo "navy OK"
else
    echo "NAVY FAILED" >&2
    echo "  $NAVY" >&2
    STATUS=1
fi

# A landing on Islands: a transport boarded under the dat's garrison rules,
# its load put ashore on the other island, and conquest.
if [ "$LANDING" = "landing=ok" ]; then
    echo "landing OK"
else
    echo "LANDING FAILED" >&2
    echo "  $LANDING" >&2
    STATUS=1
fi

# The Market: prices that move with each trade, and refusals.
if [ "$MARKET" = "market=ok" ]; then
    echo "market OK"
else
    echo "MARKET FAILED" >&2
    echo "  $MARKET" >&2
    STATUS=1
fi

# Trade carts and cogs: round trips and what each one pays.
if [ "$TRADE" = "trade=ok" ]; then
    echo "trade OK"
else
    echo "TRADE FAILED" >&2
    echo "  $TRADE" >&2
    STATUS=1
fi

# Tribute: the fee, Coinage and Banking, and who may be given it.
if [ "$TRIBUTE" = "tribute=ok" ]; then
    echo "tribute OK"
else
    echo "TRIBUTE FAILED" >&2
    echo "  $TRIBUTE" >&2
    STATUS=1
fi

# Diplomacy: stances, targeting that follows them, shared sight, allied victory.
if [ "$DIPLOMACY" = "diplomacy=ok" ]; then
    echo "diplomacy OK"
else
    echo "DIPLOMACY FAILED" >&2
    echo "  $DIPLOMACY" >&2
    STATUS=1
fi

# Chat, taunts and flares, carried as orders.
if [ "$CHAT" = "chat=ok" ]; then
    echo "chat OK"
else
    echo "CHAT FAILED" >&2
    echo "  $CHAT" >&2
    STATUS=1
fi

# The score: each element's figure as its event happens, the Achievements'
# statistics, and a save carrying them.
if [ "$SCORE" = "score=ok" ]; then
    echo "score OK"
else
    echo "SCORE FAILED" >&2
    echo "  $SCORE" >&2
    STATUS=1
fi

# A saved game, loaded, runs on as the game that never stopped; damage refused.
if [ "$SAVE" = "save=ok" ]; then
    echo "save OK"
else
    echo "SAVE FAILED" >&2
    echo "  $SAVE" >&2
    STATUS=1
fi

# A recorded game, played back from its file, is the game recorded.
if [ "$RECORDING" = "recording=ok" ]; then
    echo "recording OK"
else
    echo "RECORDING FAILED" >&2
    echo "  $RECORDING" >&2
    STATUS=1
fi

# Walls and gates: a dragged line laid at the dat's price with the piece each
# segment's neighbours choose, a tower joining a wall, one villager raising a
# whole line, a Gate turning to its wall, open to its own side and shut to the
# enemy, and locked, to all.
if [ "$WALLS" = "walls=ok" ]; then
    echo "walls OK"
else
    echo "WALLS FAILED" >&2
    echo "  $WALLS" >&2
    STATUS=1
fi

# Repairs: the repairer's rate, a crew's, a quarter for a trebuchet, half the
# cost over the hit points, stopping when the wood runs out, and refusals.
if [ "$REPAIR" = "repair=ok" ]; then
    echo "repair OK"
else
    echo "REPAIR FAILED" >&2
    echo "  $REPAIR" >&2
    STATUS=1
fi

# Trebuchets packing and setting up, and sent at a building out of range.
if [ "$PACKING" = "packing=ok" ]; then
    echo "packing OK"
else
    echo "PACKING FAILED" >&2
    echo "  $PACKING" >&2
    STATUS=1
fi

# A converted unit keeping its maker's technology.
if [ "$CONVERTED" = "converted=ok" ]; then
    echo "converted OK"
else
    echo "CONVERSION FAILED" >&2
    echo "  $CONVERTED" >&2
    STATUS=1
fi

# The lobby's games and options: Regicide, Death Match, the population limit,
# the map revealed, the starting age, All Techs.
if [ "$MODES" = "modes=ok" ]; then
    echo "modes OK"
else
    echo "GAME MODES FAILED" >&2
    echo "  $MODES" >&2
    STATUS=1
fi

# Cheat codes, typed in chat.
if [ "$CHEATS" = "cheats=ok" ]; then
    echo "cheats OK"
else
    echo "CHEATS FAILED" >&2
    echo "  $CHEATS" >&2
    STATUS=1
fi

# The CD's scenarios: read whole, built as laid out, their triggers run.
if [ "$SCENARIO" = "scenario=ok" ]; then
    echo "scenario OK"
else
    echo "SCENARIOS FAILED" >&2
    echo "  $SCENARIO" >&2
    STATUS=1
fi

# Waypoints laid with Shift: walked in order, forgotten on another order.
if [ "$WAYPOINTS" = "waypoints=ok" ]; then
    echo "waypoints OK"
else
    echo "WAYPOINTS FAILED" >&2
    echo "  $WAYPOINTS" >&2
    STATUS=1
fi

# The scenario writer: the disc's 32 scenarios written back as they were read.
if [ "$EDITOR" = "editor=ok" ]; then
    echo "editor OK"
else
    echo "SCENARIO WRITER FAILED" >&2
    echo "  $EDITOR" >&2
    STATUS=1
fi

# Construction and demolition, which no state hash can see.
if [ "$BUILDING" = "building=ok" ]; then
    echo "building OK"
else
    echo "BUILD OR DELETE FAILED" >&2
    echo "  $BUILDING" >&2
    STATUS=1
fi

exit $STATUS
