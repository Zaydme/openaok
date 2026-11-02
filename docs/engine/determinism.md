# Fixed point and determinism

A game must come out the same wherever it runs. In a network game every
player's machine runs the whole world at once and only the orders travel, so
two machines given the same start and the same orders on the same ticks have
to end up with the same world, bit for bit. A recorded game is the lobby and
the orders alone, and playing it back has to give the game that was recorded.

OpenAoK makes the simulation deterministic in four ways: all of its arithmetic
is integer fixed point, every random number comes from a generator seeded by
the lobby and kept in the world, everything is visited in a defined order, and
a self-check proves on every change that a fixed game still comes out the same.
This page covers each, and the world digest that compares two machines.

## Why not floating point

The same `float` sum can come out differently on two machines: compilers
reorder and fuse operations and may keep intermediate values in wider
registers, and the standard library's `sin`, `atan2` and `pow` differ between
platforms in their last bits. In a lockstep game one unit that ends a step a
billionth of a tile further on is enough: it reaches a tile a tick earlier, a
route turns another way, and the two worlds part for good.

Integer arithmetic gives the same bits on every CPU, compiler and platform, so
every figure the simulation keeps is an integer.

## Core::Fixed32

`Core::Fixed32` (`src/core/FixedPoint.h`) is a 32.32 fixed-point number: one
64-bit integer whose upper 32 bits are the whole part and lower 32 bits the
fraction, so `ONE` is 2<sup>32</sup> and the smallest step is 1/4,294,967,296.
Positions, distances, speeds, resource stockpiles and the dat's fractional
figures are all held this way.

```cpp
class Fixed32 {
public:
    int64_t value;
    static constexpr int FRACTIONAL_BITS = 32;
    static constexpr int64_t ONE = 1LL << FRACTIONAL_BITS;

    constexpr Fixed32() : value(0) {}
    constexpr explicit Fixed32(int64_t raw_val, bool /*is_raw*/) : value(raw_val) {}
    constexpr Fixed32(int i) : value(static_cast<int64_t>(i) << FRACTIONAL_BITS) {}
    // ...
    constexpr Fixed32 operator+(const Fixed32& other) const { return Fixed32(value + other.value, true); }
    constexpr Fixed32 operator-(const Fixed32& other) const { return Fixed32(value - other.value, true); }

    constexpr Fixed32 operator*(const Fixed32& other) const {
        // 128-bit intermediate product, so it cannot overflow.
        __int128_t prod = static_cast<__int128_t>(value) * other.value;
        return Fixed32(static_cast<int64_t>(prod >> FRACTIONAL_BITS), true);
    }

    constexpr Fixed32 operator/(const Fixed32& other) const {
        __int128_t num = static_cast<__int128_t>(value) << FRACTIONAL_BITS;
        return Fixed32(static_cast<int64_t>(num / other.value), true);
    }
    // ...
```

Adding and subtracting are plain integer sums. A product is worked out in
128 bits and shifted back down; a quotient shifts the dividend up in 128 bits
before dividing. Comparisons compare the raw values. `ToInt` shifts the
fraction away, which rounds down. A fraction is written as integers, as in
`Core::Fixed32(3) / Core::Fixed32(2)` for one and a half.

Two members are fenced off. `ToFloat` is for rendering only (the gate below
fails on it in simulation code), and the constructor from a `float` is for
loading static data only, never during a tick.

### Fixed-point helpers

`Core::FixedMath` (`src/core/FixedMath.h`) holds the operations a game needs
beyond arithmetic: `Abs`, `Min`, `Max`, `Clamp`, distances (`Distance`,
`ManhattanDistance`, `ChebyshevDistance`), `NormalizeAndScale`, and headings
in 256ths of a turn worked out from a table of tangents
(`WorldVectorToHeading`). The square root is exact:

```cpp
    static Fixed32 Sqrt(Fixed32 a) {
        if (a.value <= 0) return Fixed32(0);

        // In 32.32 fixed point, sqrt(a * 2^32) = sqrt(a) * 2^16: the integer
        // square root of the raw value, shifted left by 16. The hardware's
        // root comes near it; whole-number steps make it the exact floor, the
        // same on every machine.
        constexpr int64_t kLargestRoot = 3037000499;   // of the largest int64_t
        const int64_t val = a.value;
        int64_t result = std::min(static_cast<int64_t>(std::sqrt(static_cast<double>(val))), kLargestRoot);
        while (result * result > val) --result;
        while (result < kLargestRoot && (result + 1) * (result + 1) <= val) ++result;

        // result = sqrt(a_real) * 2^16; shift by 16 more for 32.32.
        return Fixed32(result << 16, true);
    }
```

Whatever first guess a machine's hardware gives, the integer steps after it
land on the one exact answer.

### Fractions from the data

The extracted tables state fractions as decimals (a unit's speed, a work rate,
a range). They are turned into fixed point once, while loading, by rounding to
the nearest thousandth and dividing two integers:

```cpp
// The file states fractions to thousandths; so does the conversion, rounding
// to the nearest so every platform lands on the same bits.
Core::Fixed32 Thousandths(double value) {
    const long long scaled = std::llround(value * 1000.0);
    return Core::Fixed32(static_cast<int>(scaled)) / Core::Fixed32(1000);
}
```

`Core::ConfigManager::ToFixed` does the same for the figures it loads. From
then on the simulation meets a fraction only as a `Fixed32`.

## The gate keeps floats out

The simulation is the code under `src/ecs` (the world and its tick),
`src/pathfinding` (route search and standing spots), `src/physics` (the tile
grid) and `src/ai` (the computer players, which run inside the tick). None of
it may mention `float`, `double` or `ToFloat()`. Rendering, audio and the
interface use floating point freely: they only read the world (see
[Entities and systems](/engine/simulation)).

`tools/check_determinism.sh`, the check every change must pass, enforces it
with a search over those four directories. Lines that are only a comment are
skipped:

```sh
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
```

Any match is printed with its file and line, and the check fails.

## Randomness from seeded generators

Much of the game is chance: whether an arrow hits, whether a villager comes out
of the Town Center a man or a woman, how long a wolf waits before it looks
round, which branch of a map script's `start_random` block is taken. Every one
of those rolls comes from `Core::Random` (`src/core/Random.h`), an xorshift64*
generator that gives the same sequence from the same seed on every machine:

```cpp
    uint64_t Next() {
        state ^= state >> 12;
        state ^= state << 25;
        state ^= state >> 27;
        return state * 0x2545F4914F6CDD1DULL;
    }
```

Nothing in the tick calls `rand()`, `std::mt19937` or a clock. Each
generator is seeded from the lobby's seed (`GameSetup::seed`), which every
machine in a network game is handed with the setup, and each concern has a
stream of its own so that rolling more in one never shifts the rolls of
another:

| Stream | Seeded with | Rolls for |
|---|---|---|
| The world's `Core::Random`, in the registry's context | `seed ^ 0x9E3779B97F4A7C15` | Accuracy and where a miss lands, a villager's sex, conversions, villagers running from attackers, grazing |
| `Combat::HuntRandom`, in the registry's context | `seed ^ 0xC2B2AE3D27D4EB4F` | How long each wild predator waits between looks for prey |
| Each computer player's `Brain::rng` | `(seed << 8) ^ (0xA1 + seat)` | Its scripts' `load-random`, their random numbers and percentages, and choices such as an attack group's size or where a random walk ends |
| Map generation (`core/MapGenerator`) | the seed itself | Lands, terrain clumps, objects placed by the script |
| Cliffs and hills (`core/Cliffs`, `core/Elevation`) | the seed with a salt of their own | Where cliffs and hills go |
| A map script's `start_random` block (`core/RMSParser`) | the seed with the block's depth | Which branch is taken |

The world's generator is set up in `Simulation::Init`:

```cpp
    registry.ctx().emplace<Core::Random>(
        static_cast<uint64_t>(setup.seed) ^ 0x9E3779B97F4A7C15ull);
    registry.ctx().insert_or_assign(
        Combat::HuntRandom{Core::Random(static_cast<uint64_t>(setup.seed) ^ 0xC2B2AE3D27D4EB4Full)});
```

Because the generators live in the registry's context (the computer players'
in their brains, which are kept there too), they are world state like any
component: a saved game stores their state and a loaded one rolls on exactly
where it stopped.

## A defined order

Two machines that visit the same entities in a different order can come to
different results: whichever of two archers shoots first may take the last hit
point. So everything in the tick happens in an order that every machine
shares.

- The tick's steps run in a fixed sequence (`Simulation::Update`).
- Orders for a tick are carried out in the order they were queued; in a network
  game, every seat's orders for a turn in seat order.
- An EnTT view walks a component's packed storage, whose order follows from
  the sequence of creations and removals; the same game makes the same
  sequence on every machine, so entity ids and storage order agree everywhere.
- Where an outcome depends on which entity goes first, the code collects the
  entities and sorts them by id.

`Victory::Eliminate` is one of the places that does so:

```cpp
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
        // ...
    }
}
```

The route search fixes its own ties the same way: it tries a tile's neighbours
in one order, straights before diagonals, so routes of equal cost come out the
same everywhere:

```cpp
    // Straights before diagonals, in a fixed order, so equal-cost expansions
    // happen identically everywhere.
    static const int dx[8] = { 0,  0,  1, -1,  1, -1,  1, -1};
    static const int dy[8] = {-1,  1,  0,  0, -1, -1,  1,  1};
```

## The world fingerprint

`ECS::Digest::Of` folds the world into one 64-bit number (FNV-1a): every entity
with an `Identity`, in id order, with its id, its unit id, its position's raw
bits, its hit points and its owner; then each player's four stockpiles, its
stance toward every player and whether it has chosen allied victory; then the
Market's base prices. The stockpiles, stances and prices are in it because a
tribute, a trade at the Market or a change of stance moves nothing else.

```cpp
    std::vector<entt::entity> all;
    for (auto e : registry.view<const Components::Identity>()) all.push_back(e);
    std::sort(all.begin(), all.end());
    for (entt::entity e : all) {
        mix(static_cast<uint64_t>(entt::to_integral(e)));
        mix(static_cast<uint64_t>(registry.get<Components::Identity>(e).unit_id));
        if (const auto* t = registry.try_get<Components::Transform>(e)) {
            mix(static_cast<uint64_t>(t->position.x.value));
            mix(static_cast<uint64_t>(t->position.y.value));
        }
        // ...
    }
```

Three things compare digests:

- **A network game.** Each machine takes the digest at the end of every turn
  and sends it with the orders it sends at the start of the next; the first
  turn two machines' digests differ is a desync (see
  [Lockstep multiplayer](/engine/multiplayer)).
- **A recorded game.** Every 256 ticks (`Save::kCheckEvery`) a recording notes
  the digest, and playback (`Save::StillTheGame`) reports the tick at which the
  game being played back parts from the one recorded.
- **The self-check.** `Verify::Fingerprint` (`src/verify/Harness.cpp`) is the
  same digest. The `ai=` scenario plays two computer players against each other
  in two worlds side by side and requires the fingerprints to match minute by
  minute, since in a network game every machine runs its own copy of the
  computer players; the `fishing=` scenario does the same with its twin.

## The determinism scenario

The first scenario of the self-check, `Verify::CheckDeterminism`
(`src/verify/Determinism.cpp`), builds the default lobby's world (seed 42,
Arabia, a map 120 tiles across, two players), keeps every other worker
standing, and sends the rest walking: some straight at the nearest one
standing, the rest twenty tiles across open ground, round whatever is in the
way. It then runs
the world for the number of ticks asked, 16 ms each, and hashes every
`Transform`'s raw position in entity order, sampled every 25 ticks as well as
at the end:

```cpp
    // FNV-1a over every entity's fixed-point position, in entity order, sampled
    // during the run as well as at the end: units reach their destinations well
    // before the last tick, so the final state alone cannot tell two
    // trajectories apart.
    // ...
    constexpr int kSampleEvery = 25;
    for (int t = 0; t < verify_ticks; ++t) {
        headless.Update(16);
        if ((t % kSampleEvery) == 0) sample();
    }
    sample();
```

So the hash covers map generation, the route search, crowding at a goal and
the walk itself. The scenario prints its result as
`entities=N ticks=T hash=H`, where `entities` counts the entities with a place
in the world at the end. The gate runs it for 3000 ticks (48 seconds of game
time) and compares that line with its golden value:

```sh
TICKS=3000
GOLDEN="entities=1970 ticks=3000 hash=221acd07913e73ac"
```

A hash alone cannot tell a unit that walked to its goal from one that stopped
halfway and stayed put, since both are reproducible. So the scenario also
prints `arrived=` with how many walkers reached their goals, and the gate
fails unless all of them did.

`tools/check_determinism.sh` then reads every other scenario's verdict line
and fails if any of them failed. It runs before and after every change. The
golden value changes only when a change to the simulation is meant to change
it, and only after the new hash has been seen twice. How to run the scenarios
one at a time is on [Self-checks](/development/verification).

## Where it lives in the code

- [`src/core/FixedPoint.h`](https://github.com/Zaydme/openaok/blob/main/src/core/FixedPoint.h): `Core::Fixed32`
- [`src/core/FixedMath.h`](https://github.com/Zaydme/openaok/blob/main/src/core/FixedMath.h): square root, distances and headings in fixed point
- [`src/core/Rules.cpp`](https://github.com/Zaydme/openaok/blob/main/src/core/Rules.cpp): the data's fractions turned into fixed point at load
- [`src/core/Random.h`](https://github.com/Zaydme/openaok/blob/main/src/core/Random.h): the seeded xorshift64* generator
- [`src/ecs/Simulation.cpp`](https://github.com/Zaydme/openaok/blob/main/src/ecs/Simulation.cpp): the world's generators seeded from the lobby
- [`src/ecs/Digest.cpp`](https://github.com/Zaydme/openaok/blob/main/src/ecs/Digest.cpp): the world digest
- [`src/verify/Harness.cpp`](https://github.com/Zaydme/openaok/blob/main/src/verify/Harness.cpp): `Verify::Fingerprint` and the list of scenarios
- [`src/verify/Determinism.cpp`](https://github.com/Zaydme/openaok/blob/main/src/verify/Determinism.cpp): the determinism scenario
- [`src/save/Recording.h`](https://github.com/Zaydme/openaok/blob/main/src/save/Recording.h): recorded games and their digest checks
- [`tools/check_determinism.sh`](https://github.com/Zaydme/openaok/blob/main/tools/check_determinism.sh): the gate, its float search and the golden hash
