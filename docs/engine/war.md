# Armies and the navy

In the original game an army is a selection of soldiers sent together. They
march in a formation the player chooses, keep to the pace of the slowest, and
fight by their stance. A soldier can be told to patrol between two points,
to guard another unit or to follow it. Villagers shelter in buildings when the
town bell rings; walls and gates close the town to the enemy and open for its
owner. On the water, a Dock trains fishing ships, warships and transports, and
a transport takes an army aboard and puts it ashore on any coast. Spies show a
player what its enemies see, and a flare shows its allies where to look.

This page covers how OpenAoK plays all of that: formations, stances, standing
orders, garrisons and the town bell, relics, monks and siege engines in brief,
walls and gates, the navy (Docks, fishing ships, warships, transports), Spies
and Treason, and flares. The fighting itself is on [combat](/engine/combat),
routes and the formation layouts on [movement and routes](/engine/pathfinding),
and how computer players use their armies on
[computer players](/engine/ai#the-military).

## Formations

A formation is chosen for a group of soldiers with one of the formation
buttons. That is a `FormCommand`: of the units given, those the player owns
that fight, can move and are not villagers or buildings are locked into a
formation of their own (`Formations::Lock`), out of any they were in. At
least two are needed. The group then forms up where it stands, about its own
middle.

A locked formation goes whole. A move order for any of its members becomes a
formation move of all of them that stand in the world (`Formations::Members`),
and a group move that takes in units of several locked formations takes them
all, in the formation the order names; with only one locked formation among
them, in that formation's own. Each member walks to its own place in the
layout, at the pace of the slowest member, so the group arrives together.
Villagers in a group take no part: they get places side by side about the
point and walk at their own pace. How the places are laid out (line,
staggered, box, flank) is on [formations](/engine/pathfinding#formations).

A formation lasts until it is disbanded (`DisbandCommand`, which breaks up
every formation the given units are in) or another is chosen. A unit that
dies or changes sides leaves its formation (`Formations::Leave`), and a
formation left with one member is no formation at all.

Control groups, made with Ctrl and a number key, are the interface's own:
the number is kept on the unit's `Selectable`, and nothing in the simulation
reads it.

## Stances

Each fighter has one of four stances in its `Combat`: aggressive, defensive,
stand ground and no attack. Every fighter starts aggressive. A `StanceCommand`
changes it, and setting no attack drops whatever fight the unit is in. What
each stance looks for, how far it pursues and whom it comes to help are on
[stances](/engine/combat#stances).

There is no attack-move order. A unit given a move walks where it was sent
and pays no heed to enemies until it gets there (`Path::sent`); a patrol is
the order that fights on its way.

## Patrol, guard and follow

A standing order is one that keeps giving itself until another order replaces
it. Each is a `Duty` component (`ecs/Duties`), given to every unit of the
order the player owns that is out in the world and not a building, after its
current job is dropped.

- **Patrol.** The unit walks from where it stands to the point, then back,
  for ever. Its walks are not `sent`, so its combat looks about it as an idle
  unit's does, and an enemy it finds is fought by its stance. While it
  fights, the patrol waits; once the fight is over it walks on to the end it
  was heading for.
- **Guard.** The unit keeps by its charge. Any hostile fighter within six
  tiles of the guard that has its charge as its target is taken on, the
  nearest first, as an ordered target, so the guard pursues it whatever its
  stance; with the assailant dead, the duty brings the guard back.
- **Follow.** As guard, but the follower never takes up its charge's fights.

Guarding and following end when the charge dies or goes into a building or
aboard a ship. Keeping by the charge works on two distances: a unit standing
still lets its charge get two and a half tiles away before it sets off, and a
unit walking stops once within a tile and a half. On the way it plans again
every half a second, for a spot a tile and a half short of the charge on its
own side:

```cpp
            duty.replan_ms -= dt_ms;
            const auto& there = registry.get<Components::Transform>(duty.charge).position;
            const Core::Fixed32 gap = Core::FixedMath::Distance(at.x - there.x, at.y - there.y);
            if (gap <= kCloseUp && !Walking(registry, unit)) break;
            if (gap <= kKeepBy) {
                registry.remove<Components::Path>(unit);
                break;
            }
            if (duty.replan_ms > 0 && Walking(registry, unit)) break;
            duty.replan_ms = kReplanMs;
            // A spot short of the charge, on the near side.
            const Core::Fixed32 back = kKeepBy / gap;
            walks.emplace_back(unit, Components::Vector2D{there.x + (at.x - there.x) * back,
                                                          there.y + (at.y - there.y) * back});
```

| Constant | Value | Meaning |
|---|---|---|
| `kArrived` | 0.5 tiles | a patrol has reached an end |
| `kKeepBy` | 1.5 tiles | how near a guard or follower keeps to its charge |
| `kCloseUp` | 2.5 tiles | how far it lets the charge get before closing up |
| `kReplanMs` | 500 ms | how often it plans again after a charge on the move |
| `kGuardWatch` | 6 tiles | how far a guard looks for whoever is set on its charge |

`Duties::Update` runs once a tick, after movement and garrisons. Walks and
fights are gathered in the pass over the duties and given after it.

## Garrisons and the town bell

Who may shelter where is the dat's, from both sides: the unit's task list must
have a garrison task naming the building class, and the building's garrison
type says which kinds it takes, its capacity how many. `Garrisons::Accepts`
also wants the building finished, alive and of the unit's own owner, and
counts the units already on their way in, so two sent at once cannot both
take the last place. A garrison type admits four kinds, a bit each: villagers
(a Regicide King among them), foot soldiers, cavalry and monks; `KindOf` says
which of the dat's unit classes make up each, so an archer goes in as a foot
soldier and a cavalry archer as cavalry.

A unit sent in (`GarrisonCommand`) gets a `Boarding` and walks to a spot just
off the wall, chosen by the approach planner; it goes in once within 0.6 of a
tile of the wall, less its own radius (`kEnterReach`). It gives up when no
route is found, or after eight routes (`kMaxAttempts`) have not brought it in.
Going in, it gives up its `Transform` and takes a `Garrisoned`; a villager
going into a building other than a tower or a Castle banks what it carries.
Inside, a unit heals at the building's own garrison heal rate and may add to
its arrows (see [buildings that shoot](/engine/combat#buildings-that-shoot)).

Units come out at the door (`Buildings::Doorstep`):

- **Ungarrison** turns everyone out, and those the town bell sent go back to
  work. One with no room to stand stays in.
- **Back to Work** turns out only the villagers, each one the bell sent back
  to the job it left; the rest stay in.
- **The building falls, or is converted**: everyone is turned out. The units
  inside are not converted with it. A building that falls has already given
  up its ground, so anyone with no room at the door stands on the ground it
  held.

**The town bell** is rung at a Town Center (`TownBellCommand`). Every villager
of its owner within 25 tiles runs to the nearest shelter that takes villagers
and has room; rung again, those it sent come out and go back to work. The
details are on [garrisons and the town bell](/engine/combat#garrisons-and-the-town-bell),
and going back to work on [breaking off and going back](/engine/units#breaking-off-and-going-back).

## Relics, monks and siege engines

**Relics** (`ecs/Relics`). A unit whose tasks pick up the relic's unit (a
Monk) can fetch one; picking it up, it becomes the dat's monk carrying a relic
(`Units::Become`), and the relic leaves the world with it. It carries the
relic to one of its owner's Monasteries and is a Monk again. Each relic held
earns its owner half a gold a second (`kGoldPerRelic`). A carrier that dies
drops the relic where it fell, and a Monastery that falls leaves its relics on
the ground. Holding them all is a victory condition (see
[victory and diplomacy](/engine/victory)).

**Monks** convert and heal. A Monk's attack order is a conversion when it can
convert the target, and a `HealCommand` sets it healing. Faith, the time a
conversion takes and what can be converted are on
[converting and healing](/engine/combat#converting-and-healing).

**Siege engines that pack.** A trebuchet is two of the dat's units, one that
moves and one that shoots. A `PackCommand` packs or unpacks it where it
stands, which takes four seconds (`Packing::kPackMs`); while at it, it takes
neither a move nor an attack order, and Stop leaves it as it was. A packed
trebuchet given an attack order goes to set up within range and opens fire.
See [siege engines that pack](/engine/combat#siege-engines-that-pack).

## Walls and gates

A wall in the original is dragged out from one tile to another and laid as a
line of foundations at once. Each segment shows a run, a post or a diagonal,
chosen by the walls beside it, and a tower set in a wall joins it. A gate is
put over a wall, opens for its owner and its allies, and can be locked.

### Laying a wall

A `WallCommand` names the wall, the two end tiles and the villagers to build
it. The line between the ends (`Walls::Line`) is straight along an axis or a
diagonal, and otherwise the nearest staircase of tiles, each touching the last
by a side or a corner (an integer Bresenham line). `Walls::Lay` places a
foundation on every tile of the line that a wall can stand on and the player
has explored, paying for each in order until the player can pay for no more;
tiles already holding one of the player's segments, or blocked, are passed
over. Each villager is sent
to the segment nearest it, and as each segment is finished its builder takes
up the next foundation in reach (see [worker states](/engine/units#worker-states)),
so a villager given the line raises all of it.

### Which piece a segment shows

The dat marks walls, gates and towers as joining (their adjacent mode): the
buildings beside one choose which angle of its graphic it draws. A wall's
graphic has five, and each segment's is worked out from which of the eight
tiles round it hold one of its owner's joining buildings:

```cpp
int PieceFrom(const Occupancy& occ, int x, int y) {
    const bool n = occ.At(x, y - 1), s = occ.At(x, y + 1);
    const bool w = occ.At(x - 1, y), e = occ.At(x + 1, y);
    const bool ne = occ.At(x + 1, y - 1), sw = occ.At(x - 1, y + 1);
    const bool se = occ.At(x + 1, y + 1), nw = occ.At(x - 1, y - 1);
    // A run: one on each side of it along one line, and nothing touching it
    // across that line.
    if (n && s && !w && !e) return kAlongY;
    if (w && e && !n && !s) return kAlongX;
    const bool orthogonal = n || s || w || e;
    if (!orthogonal && ne && sw && !se && !nw) return kAcross;
    if (!orthogonal && se && nw && !ne && !sw) return kDown;
    return kPost;
}
```

An end, a corner, a junction or a lone segment is a post. A tower or a gate
beside a wall counts as a wall, so a tower set in a gap turns the posts either
side back into runs. Whenever a joining building comes or goes, the pieces
round it are worked out again (`Walls::Refresh`), the foundations' construction
stages too.

### Gates

The Gate button turns to the wall it is put on (`Walls::GateFor`): of the
dat's four gates, the one whose line, from post to post, runs along the most
of the player's wall within two tiles either side of the tile (with no wall
there, the button's own). A gate may go up on open ground or over the
player's own wall; the segments under it are taken down as it is placed
(`Walls::ClearForGate`).

A gate is four tiles along its wall and one across. Its two posts block like
any wall. When it is finished, its two arch tiles become the owner's gate on
the grid (`Walls::GateFinished`): blocked like any building's, except to a
land unit whose route is keyed with a player the gate opens for (see
[passability layers](/engine/pathfinding#passability-layers)). Once a tick,
before anything moves, `Walls::SetAllies` records for each player whom its
gates open for: the players it regards as allies, as the stances now stand.

- **Locked**: a finished gate locked by its owner (`GateCommand`) is shut to
  everyone, its own side included, and any unit standing in it steps out of
  it.
- **Raised**: an unlocked gate is drawn with its portcullis up while a living
  unit it opens for stands within a tile and a half of it (`kOpenReach`). That
  is only the drawing; the tiles were open to that unit all along.

## The navy

### Docks and the sea

The sea is ground of its own: a passability layer whose movers are the ships
(see [passability layers](/engine/pathfinding#passability-layers)). A Dock is
placed by the dat's placement terrains: its middle tile on water or
shallows, with beach straight beside it, so it stands half on the shore. A
ship trained there steps out on the water side, because the door a trained
unit is put at is found on the unit's own layer (`Buildings::Doorstep`).

### Fishing ships

A fishing ship is a worker that works as itself (`Worker::gatherer` is its own
unit id): it gathers at its own rate, by its own gather tasks, which name the
fish and the fish trap but not a tree on the shore, and carries its own load
to its owner's Docks on the same sea. It turns to its fish an angle at a time
before it fishes, and when the fish runs out it looks for the next within its
search radius, as a villager does. It also builds fish traps, which the dat
trains at the fishing ship rather than at the villager. A fish trap is a farm
of the water: one fishing ship works it at a time. The whole cycle is the
villager's (see [worker states](/engine/units#worker-states)).

### Warships

Warships fight as any unit does (see [combat](/engine/combat)), with a few
things that matter at sea:

- A ship turns one angle of its graphic at a time, at its rules' rotation
  speed, and shoots only once it faces its target (`Turning::Toward`).
- One that fights hand to hand reaches only what stands on, or beside, its
  own water: a demolition ship does not set off after a villager inland. It
  is spent in its one blow.
- A ship that shoots reaches anything in range, a villager on the beach
  included.
- Villagers repair ships, at a quarter of the pace they repair a building
  (`Repairs`, `kUnitShare`).

Trade cogs trade between Docks as trade carts do between Markets (see
[gathering and the economy](/engine/economy)).

### Transports

A transport is a ship with room aboard (`Garrisons::IsTransport`: not a
building, with a garrison capacity). Every land unit's task list has a second
garrison task naming the transport's class, so any land unit may board its own
player's transport while there is room; no garrison type applies. Aboard, a
unit has no `Transform`, as in a building. A transport that changes sides
takes those aboard with it; one that sinks takes them down.

**Loading.** A unit sent aboard walks to a spot on its own land beside the
hull. When the ship lies where no such spot is (out at sea, or off another
shore), the unit calls it in: an idle transport sails to the tile of its own
sea, touching the unit's land, nearest the unit (`CallShip`). The unit tries
again every half a second (`kShipRetryMs`), up to 60 times
(`kMaxShipAttempts`), so the ship has time to come in.

**Landing.** An `UnloadCommand` (the Unload button, then a click) gives the
transport a `Landing` for the point. It sails to the tile of its own sea that
touches land, any land, nearest the point, looking up to 30 tiles round it
(`kShoreReach`). The shore need not be its owner's, nor the land the units
came from. Once it has stopped, everyone aboard steps onto the land beside the
hull, each on ground of its own layer, ring by ring out from the hull so a
full load spreads up the beach:

```cpp
        for (int ring = 0; ring < 4 && !landed; ++ring) {
            Pathfinding::ApproachRequest ask;
            ask.target = hull;
            ask.from = registry.get<Components::Transform>(ship).position;
            ask.standoff = Core::Fixed32(4 + 5 * ring) / Core::Fixed32(10);
            ask.clearance = Core::Fixed32(1) / Core::Fixed32(2);
            ask.taken = placed.empty() ? nullptr : placed.data();
            ask.taken_count = placed.size();
            ask.layer = Navigation::LayerOf(registry, grid, unit);
            const auto spot = Pathfinding::FindStandingSpot(grid, ask);
            if (!spot.found) continue;
            registry.remove<Components::Garrisoned>(unit);
            registry.emplace<Components::Transform>(unit, Components::Transform{spot.spot, spot.spot});
            placed.push_back(spot.spot);
            landed = true;
        }
```

Those who find no ground stay aboard, and the landing tries again on the next
ticks, up to eight times, as the first ashore move up the beach. Ungarrisoning
a transport lands its load the same way at the shore nearest where it is.
Another order given to the transport ends the landing and keeps the load
aboard.

```mermaid
flowchart TD
    order["Garrison order:<br/>a land unit sent aboard"] --> accepts{"Garrisons::Accepts:<br/>same owner, a task naming the transport,<br/>room for those on their way"}
    accepts -->|no| refused["Refused"]
    accepts -->|yes| spot{"A spot on the unit's land<br/>beside the hull?"}
    spot -->|yes| walk["Walks up to the hull"]
    spot -->|no| call["CallShip: an idle transport sails<br/>to the shore nearest the unit"]
    call -->|"500 ms on, up to 60 tries"| spot
    walk --> aboard["Aboard: Garrisoned,<br/>no Transform"]
    aboard --> unload["Unload order at a point,<br/>or turned out where it is"]
    unload --> shore["Landing: the tile of its own sea,<br/>touching any land, nearest the point"]
    shore --> sail["Sails there"]
    sail --> off["Each aboard set on its own ground<br/>round the hull, ring by ring"]
    off -->|"some left, up to 8 tries"| off
    off --> done["Landing over"]
```

A computer player whose attack group's target lies across the water boards
its idle transports and lands at the shore nearest the target with the same
orders (see [the military](/engine/ai#the-military)).

## Spies and Treason

**Spies** is a technology (`Techs::Spies` names it). Its price is the dat's
price once for each villager of every other player still in the game that is
not its ally both ways, never fewer than 1 nor more than 150, times the
player's research cost rate (a resource of the player's) where it has one.
`Spies::Update` works it out again every tick, so the price follows the
villagers as they come and go:

```cpp
        villagers = std::clamp(villagers, kFewestVillagers, kMostVillagers);
        Core::Fixed32 rate = Players::Resource(*seat, kResearchRate);
        if (rate.value <= 0) rate = Core::Fixed32(1);
        for (int id : techs) {
            const R::TechRules* tech = R::Tech(id);
            Players::TechStatus& status = seat->techs[static_cast<size_t>(id)];
            status.cost_count = tech->cost_count;
            for (int i = 0; i < tech->cost_count; ++i) {
                status.costs[i] = tech->costs[i];
                const int amount = (Core::Fixed32(tech->costs[i].amount) * Core::Fixed32(villagers) * rate).ToInt();
                status.costs[i].amount = static_cast<int16_t>(std::min(amount, 32767));
            }
        }
```

Researched, its effect sets the player's "reveal enemy" resource, and the
player sees what every player it is not allied with both ways sees now
(`ecs/Vision`): their sight, not the ground they have explored.

**Treason** takes the place of Spies in a Regicide game; Death Match has
neither (`Spies::Setup` disables Spies in both). It is not researched but done
at once at a finished Castle, for 400 gold each time (`kTreasonGold`), as often
as the gold lasts. Every King of a player it does not regard as an ally is
marked, where it stands or at the building it shelters in, with a mark of the
dat's `FLARE4` unit, for the player and those it regards as allies. The mark
shows as a flare does and, for those it is shown to, sees round itself as far
as that unit's sight, for as long as that unit lasts.

## Flares

A flare marks a point for a player's allies: the Signal Allies button by the
minimap, then a click on the map or the minimap. It is an order
(`FlareCommand`) carried in lockstep like chat, and `Flares::Send` writes it to
a log in the world's context. The sender and every player it regards as an
ally see it, decided as the order is carried out; a player out of the game
sends none. The minimap draws it and the sound module plays its sound; nothing
is drawn on the field.

How long a flare shows is the dat's: its `FLARE` unit holds an amount of the
decay-time resource and spends it at its decay rate, and the lifetime is the
one over the other:

```cpp
int32_t Lifetime(int unit_id) {
    const Core::Rules::UnitRules* flare = Core::Rules::Unit(unit_id);
    if (flare == nullptr || flare->decay.value <= 0) return Log{}.lifetime_ms;
    for (int i = 0; i < flare->storage_count; ++i) {
        if (flare->storage[i].resource != kDecayTime) continue;
        return (flare->storage[i].amount * Core::Fixed32(1000) / flare->decay).ToInt();
    }
    return Log{}.lifetime_ms;
}
```

A flare is showing while fewer ticks than its lifetime have passed since it
went up (`Flares::Showing`), counted at 16 ms a tick, so it lasts the same on
every machine. Spent flares are dropped from the log as new ones come.

## Checks

The self-checks (see [verification](/development/verification)) that hold this
behaviour include `formations`, `duties` (a patrol fighting what it meets, a
follower keeping by its charge, a guard turning on its charge's attacker),
`garrison`, `walls` (lines, pieces, gates, locking), `water`, `fishing`,
`navy` (warships at sea), `landing` (a transport carrying a war party across
to another island), `spies` and `chat` (flares):

```sh
cd build && AOE_VERIFY_ONLY=duties,garrison,walls,fishing,navy,landing,spies ./OpenAoK --verify 10
```

## Where it lives in the code

- [`src/ecs/Formations.cpp`](https://github.com/Zaydme/openaok/blob/main/src/ecs/Formations.cpp): formation layouts, locking, disbanding and leaving
- [`src/ecs/Orders.cpp`](https://github.com/Zaydme/openaok/blob/main/src/ecs/Orders.cpp): `Form`, `MarchInFormation`, stances, standing orders, garrison, unload, gate, pack and flare orders
- [`src/ecs/Duties.cpp`](https://github.com/Zaydme/openaok/blob/main/src/ecs/Duties.cpp): patrol, guard and follow
- [`src/ecs/components/Duty.h`](https://github.com/Zaydme/openaok/blob/main/src/ecs/components/Duty.h): a standing order
- [`src/ecs/Garrisons.cpp`](https://github.com/Zaydme/openaok/blob/main/src/ecs/Garrisons.cpp): garrisons, the town bell, transports loading and landing
- [`src/ecs/components/Garrison.h`](https://github.com/Zaydme/openaok/blob/main/src/ecs/components/Garrison.h): `Garrison`, `Boarding`, `Landing`, `Garrisoned`
- [`src/ecs/Relics.cpp`](https://github.com/Zaydme/openaok/blob/main/src/ecs/Relics.cpp): fetching relics, Monasteries holding them, the gold they earn
- [`src/ecs/Packing.cpp`](https://github.com/Zaydme/openaok/blob/main/src/ecs/Packing.cpp): trebuchets packing and setting up
- [`src/ecs/Walls.cpp`](https://github.com/Zaydme/openaok/blob/main/src/ecs/Walls.cpp): wall lines, pieces, gates, locking and whom gates open for
- [`src/ecs/components/Gate.h`](https://github.com/Zaydme/openaok/blob/main/src/ecs/components/Gate.h): a gate's lock and its raised graphic
- [`src/ecs/Buildings.cpp`](https://github.com/Zaydme/openaok/blob/main/src/ecs/Buildings.cpp): Dock placement and the doorstep a trained unit steps out at
- [`src/ecs/Repairs.cpp`](https://github.com/Zaydme/openaok/blob/main/src/ecs/Repairs.cpp): who mends what, ships at a quarter pace
- [`src/ecs/Spies.cpp`](https://github.com/Zaydme/openaok/blob/main/src/ecs/Spies.cpp): Spies' price and Treason
- [`src/ecs/Flares.cpp`](https://github.com/Zaydme/openaok/blob/main/src/ecs/Flares.cpp): flares and their lifetime
- [`src/ecs/Vision.cpp`](https://github.com/Zaydme/openaok/blob/main/src/ecs/Vision.cpp): sight shared through Spies, and what marks see
- [`src/ai/Military.cpp`](https://github.com/Zaydme/openaok/blob/main/src/ai/Military.cpp): a computer player's groups boarding transports
- [`src/verify/DutiesCheck.cpp`](https://github.com/Zaydme/openaok/blob/main/src/verify/DutiesCheck.cpp), [`GarrisonCheck.cpp`](https://github.com/Zaydme/openaok/blob/main/src/verify/GarrisonCheck.cpp), [`WallsCheck.cpp`](https://github.com/Zaydme/openaok/blob/main/src/verify/WallsCheck.cpp), [`FishingCheck.cpp`](https://github.com/Zaydme/openaok/blob/main/src/verify/FishingCheck.cpp), [`NavyCheck.cpp`](https://github.com/Zaydme/openaok/blob/main/src/verify/NavyCheck.cpp), [`LandingCheck.cpp`](https://github.com/Zaydme/openaok/blob/main/src/verify/LandingCheck.cpp), [`SpiesCheck.cpp`](https://github.com/Zaydme/openaok/blob/main/src/verify/SpiesCheck.cpp), [`ChatCheck.cpp`](https://github.com/Zaydme/openaok/blob/main/src/verify/ChatCheck.cpp): the self-checks that hold this behaviour
