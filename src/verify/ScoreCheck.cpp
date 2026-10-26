#include "verify/Harness.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "core/Command.h"
#include "core/CommandQueue.h"
#include "core/ConfigManager.h"
#include "core/GameSetup.h"
#include "core/Rules.h"
#include "ecs/Buildings.h"
#include "ecs/Combat.h"
#include "ecs/Diplomacy.h"
#include "ecs/DropSites.h"
#include "ecs/Mortality.h"
#include "ecs/Players.h"
#include "ecs/Score.h"
#include "ecs/Simulation.h"
#include "ecs/Techs.h"
#include "ecs/Timeline.h"
#include "ecs/Tribute.h"
#include "ecs/Units.h"
#include "ecs/Victory.h"
#include "ecs/Vision.h"
#include "ecs/components/Combat.h"
#include "ecs/components/Health.h"
#include "ecs/components/Identity.h"
#include "ecs/components/Mortal.h"
#include "ecs/components/Owner.h"
#include "ecs/components/Projectile.h"
#include "ecs/components/Worker.h"
#include "save/Save.h"
#include "verify/Shore.h"

namespace Verify {

namespace {

using Core::Fixed32;
namespace R = Core::Rules;
namespace S = ECS::Score::Res;

constexpr Core::PlayerId kUs = 1;
constexpr Core::PlayerId kThem = 2;
constexpr int kTicksPerSecond = 1000 / 16;

// The dat's figures used below: a Town Center 275 wood, a villager 50 food, a
// Scout Cavalry 80 food, a Militia 60 food and 20 gold, a House 30 wood, a
// Market 175 wood, a Monk 100 gold, a Castle 650 stone, a Wonder 1000 each of
// wood, gold and stone; Loom 50 gold, the Feudal Age 500 food. A standard
// start is 200 food, wood and stone and 100 gold, with three villagers.
constexpr int kStartValue = 275 + 3 * 50 + 80;
constexpr int kMilitia = 80;
constexpr int kHouse = 30;

int Res(const ECS::Simulation& sim, Core::PlayerId player, int dat_resource) {
    const auto* seat = ECS::Players::Find(sim.GetRegistry(), player);
    return seat != nullptr ? ECS::Players::Resource(*seat, dat_resource).ToInt() : -1;
}

ECS::Score::Sheet Sheet(const ECS::Simulation& sim, Core::PlayerId player) {
    const ECS::Score::Sheet* s = ECS::Score::Of(sim.GetRegistry(), player);
    return s != nullptr ? *s : ECS::Score::Sheet{};
}

void Run(ECS::Simulation& sim, int ticks) {
    for (int t = 0; t < ticks; ++t) sim.Update(16);
}

bool Clearing(const Physics::Grid& grid, int& x, int& y, int half) {
    for (int radius = 0; radius < 60; ++radius) {
        for (int dy = -radius; dy <= radius; ++dy) {
            for (int dx = -radius; dx <= radius; ++dx) {
                bool clear = true;
                for (int oy = -half; oy <= half && clear; ++oy) {
                    for (int ox = -half; ox <= half && clear; ++ox) {
                        if (!grid.IsWalkable(x + dx + ox, y + dy + oy)) clear = false;
                    }
                }
                if (clear) {
                    x += dx;
                    y += dy;
                    return true;
                }
            }
        }
    }
    return false;
}

// A unit that will not start a fight of its own.
entt::entity Peaceful(ECS::Simulation& sim, const char* name, Core::PlayerId player, int x,
                      int y) {
    auto& reg = sim.GetRegistry();
    const int id = Core::ConfigManager::Get().GetUnitIdByName(name);
    const entt::entity unit = ECS::Units::Spawn(reg, id, player, Shore::At(x, y));
    if (auto* combat = reg.try_get<ECS::Components::Combat>(unit)) {
        combat->stance = ECS::Components::Stance::NoAttack;
    }
    return unit;
}

// A building of `name` for `player` at the nearest place it may stand.
entt::entity Raise(ECS::Simulation& sim, const char* name, Core::PlayerId player, int x, int y,
                   bool finished) {
    const int id = Core::ConfigManager::Get().GetUnitIdByName(name);
    int bx = 0, by = 0;
    if (!Shore::NearestSpot(sim.GetGrid(), id, x, y, 30, bx, by)) return entt::null;
    return ECS::Buildings::Place(sim.GetRegistry(), sim.GetGrid(), id, player, bx, by, finished);
}

// The tiles `player` has explored, counted one by one here.
int ExploredTiles(const ECS::Simulation& sim, Core::PlayerId player) {
    const auto& grid = const_cast<ECS::Simulation&>(sim).GetGrid();
    int explored = 0;
    for (int y = 0; y < grid.GetHeight(); ++y) {
        for (int x = 0; x < grid.GetWidth(); ++x) {
            if (ECS::Vision::Explored(sim.GetRegistry(), player, x, y)) ++explored;
        }
    }
    return explored;
}

// The percent of the map explored: the whole percent below the share.
int ExploredPercent(const ECS::Simulation& sim, Core::PlayerId player) {
    const auto& grid = const_cast<ECS::Simulation&>(sim).GetGrid();
    return ExploredTiles(sim, player) * 100 / (grid.GetWidth() * grid.GetHeight());
}

// The economy by the formula, from the player's resources.
int Economy(const ECS::Simulation& sim, Core::PlayerId p) {
    const int net = (Res(sim, p, S::TributeGiven) - Res(sim, p, S::TributeReceived)) / 10;
    return Res(sim, p, R::Res::Food) / 10 + Res(sim, p, R::Res::Wood) / 10 +
           Res(sim, p, R::Res::Stone) / 10 + Res(sim, p, R::Res::Gold) / 10 +
           Res(sim, p, S::ValueOwned) / 5 + (net > 0 ? net : 0);
}

bool SameSheet(const ECS::Score::Sheet& a, const ECS::Score::Sheet& b) {
    return std::memcmp(&a, &b, sizeof(a)) == 0;
}

// --- The history (ecs/Timeline). ---

namespace T = ECS::Timeline;

const T::Record& History(const ECS::Simulation& sim, Core::PlayerId player) {
    static const T::Record kNone;
    const T::Record* r = T::Of(sim.GetRegistry(), player);
    return r != nullptr ? *r : kNone;
}

// Runs to `before` ticks short of the tick reading `k` is taken at: the
// game second 25k, which the clock passes at the tick ceil(25000k / 16).
void RunToReading(ECS::Simulation& sim, int k, int before = 0) {
    const uint64_t at = (static_cast<uint64_t>(k) * 25000 + 15) / 16 - static_cast<uint64_t>(before);
    while (sim.Tick() < at) sim.Update(16);
}

bool SameHistory(const T::Record& a, const T::Record& b) {
    if (a.samples.size() != b.samples.size() || a.events.size() != b.events.size()) return false;
    for (size_t i = 0; i < a.samples.size(); ++i) {
        if (a.samples[i].civilian != b.samples[i].civilian ||
            a.samples[i].military != b.samples[i].military) {
            return false;
        }
    }
    for (size_t i = 0; i < a.events.size(); ++i) {
        const T::Event& e = a.events[i];
        const T::Event& f = b.events[i];
        if (e.kind != f.kind || e.sample != f.sample || e.time_ms != f.time_ms || e.a != f.a ||
            e.b != f.b || e.c != f.c) {
            return false;
        }
    }
    return a.seconds == b.seconds && a.fighting == b.fighting && a.weight == b.weight &&
           std::memcmp(&a.last, &b.last, sizeof(a.last)) == 0;
}

// The weights of the battles on a history, newest first.
std::vector<Fixed32> Battles(const T::Record& r) {
    std::vector<Fixed32> weights;
    for (const T::Event& e : r.events) {
        if (e.kind == T::kBattle) weights.push_back(e.b);
    }
    return weights;
}

long long Count(size_t n) { return static_cast<long long>(n); }

// The history each player keeps for the Timeline: the first reading as the
// game starts, one every 25 game seconds to the tick, of the civilians and
// soldiers; the ages, a Wonder and a Castle finished and brought down on the
// right player's list; a battle weighed from the reading before it began,
// below 0.15 forgotten, at 0.15 kept and naming its enemy; only three kept,
// a new one pushing out the weaker of the two before it, or going; a defeat
// noted, a resignation not; and the whole of it, a battle half fought
// included, carried by a save.
void CheckHistory(const char*& verdict, std::string& detail) {
    auto ok = [&]() { return std::strcmp(verdict, "ok") == 0; };
    auto fail = [&](const char* why, long long got, long long want) {
        if (!ok()) return;
        verdict = why;
        detail = " (" + std::to_string(got) + ", not " + std::to_string(want) + ")";
    };

    ECS::Simulation sim;
    sim.Init(Core::DefaultSetup());
    auto& reg = sim.GetRegistry();
    auto& grid = sim.GetGrid();
    Core::CommandQueue::Get().PopAllCommands();
    auto clock = [&]() { return reg.ctx().get<ECS::Score::Table>().clock_ms; };

    // The first reading, of the start's three villagers and Scout.
    {
        const T::Record& us = History(sim, kUs);
        if (us.samples.size() != 1) {
            fail("the history did not begin with one reading", Count(us.samples.size()), 1);
        } else if (us.samples[0].civilian != 3 || us.samples[0].military != 1) {
            fail("the first reading is not three villagers and a Scout",
                 us.samples[0].civilian * 10 + us.samples[0].military, 31);
        } else if (!us.events.empty()) {
            fail("the history began with an event", Count(us.events.size()), 0);
        }
    }

    int x = grid.GetWidth() / 2;
    int y = grid.GetHeight() / 2;
    if (ok() && !Clearing(grid, x, y, 6)) verdict = "no open ground for the history";
    // A quiet field: no wolf of the map's near it, to kill the soldiers
    // whose deaths are being counted.
    {
        std::vector<entt::entity> wild;
        auto view = reg.view<const ECS::Components::Combat, const ECS::Components::Owner,
                             const ECS::Components::Transform>();
        for (auto e : view) {
            if (view.get<const ECS::Components::Owner>(e).player_id != Core::PLAYER_GAIA) continue;
            const auto& at = view.get<const ECS::Components::Transform>(e).position;
            const int dx = at.x.ToInt() - x, dy = at.y.ToInt() - y;
            if (dx * dx + dy * dy < 30 * 30) wild.push_back(e);
        }
        for (entt::entity e : wild) reg.destroy(e);
    }
    entt::entity soldier = entt::null;
    std::vector<entt::entity> targets;
    // The next reading at the 25th game second, to the tick; a Militia made
    // just before that tick is in it, since a unit counts the moment it is
    // made. The soldier's 80 is on the books by then, so a battle is weighed
    // against 230 + 80 of units and the Town Center's 275.
    constexpr int kOwned = 230 + kMilitia + 275;
    if (ok()) {
        for (int i = 0; i < 3; ++i) targets.push_back(Peaceful(sim, "SPRMN", kThem, x + 2, y - 1 + i));
        RunToReading(sim, 1, 1);
        if (History(sim, kUs).samples.size() != 1) {
            fail("a reading before the 25th second", Count(History(sim, kUs).samples.size()), 1);
        }
        soldier = Peaceful(sim, "SPRMN", kUs, x - 2, y);
        RunToReading(sim, 1);
        const T::Record& us = History(sim, kUs);
        if (ok() && us.samples.size() != 2) {
            fail("no reading at the 25th second", Count(us.samples.size()), 2);
        } else if (ok() && (us.samples[1].civilian != 3 || us.samples[1].military != 2)) {
            fail("the reading is not the civilians and soldiers then",
                 us.samples[1].civilian * 10 + us.samples[1].military, 32);
        } else if (ok() && us.seconds != 0) {
            fail("the seconds were not counted afresh after a reading", us.seconds, 0);
        }
    }

    // An age and a Wonder: on the list newest first, marked for the next
    // reading, at the game time. A Militia killed: 80 of 585 destroyed is
    // fighting, but a battle of 0.137 is forgotten when it ends.
    if (ok()) {
        const int32_t aged = clock();
        ECS::Techs::Complete(reg, kUs, 101);
        if (Raise(sim, "WNDR", kUs, x + 12, y - 12, true) == entt::null) {
            verdict = "no ground for a Wonder";
        }
        if (ok()) ECS::Combat::Hurt(reg, grid, targets[0], Fixed32(1000), soldier);
        RunToReading(sim, 2);
        const T::Record& us = History(sim, kUs);
        if (ok() && (us.events.size() != 2 || us.events[0].kind != T::kWonderBuilt ||
                     us.events[1].kind != T::kFeudalAge)) {
            fail("the Feudal Age and a Wonder are not on the history, newest first",
                 Count(us.events.size()), 2);
        } else if (ok() && (us.events[1].sample != 2 || us.events[1].time_ms != aged)) {
            fail("the age is not marked for the next reading at its time", us.events[1].sample, 2);
        } else if (ok() && us.events[0].a != kUs) {
            fail("a Wonder finished does not note its owner", us.events[0].a, kUs);
        } else if (ok() && !us.fighting) {
            fail("a kill of 80 against 585 owned did not start a battle", 0, 1);
        } else if (ok() && us.weight != Fixed32(kMilitia) / Fixed32(kOwned)) {
            fail("the battle's first reading is not weighed 80 of 585",
                 us.weight.value, (Fixed32(kMilitia) / Fixed32(kOwned)).value);
        }
        RunToReading(sim, 3);
        const T::Record& later = History(sim, kUs);
        if (ok() && (later.fighting || !Battles(later).empty())) {
            fail("a battle under 0.15 was kept", Count(Battles(later).size()), 0);
        }
    }

    // Two Militia: 160 of 585, a battle of 0.27 under way when the game is
    // saved; the next reading quiet, it ends, kept, naming player 2 -- in the
    // game loaded too.
    ECS::Simulation loaded;
    if (ok()) {
        ECS::Combat::Hurt(reg, grid, targets[1], Fixed32(1000), soldier);
        ECS::Combat::Hurt(reg, grid, targets[2], Fixed32(1000), soldier);
        RunToReading(sim, 4);
        std::vector<uint8_t> bytes;
        std::string error;
        if (!History(sim, kUs).fighting) {
            fail("two Militia killed did not start a battle", 0, 1);
        } else if (!Save::Write(sim, bytes) || !Save::Read(loaded, bytes, &error)) {
            verdict = "the game with a battle under way would not save and load";
        }
        for (int p = 1; p <= 2 && ok(); ++p) {
            const auto id = static_cast<Core::PlayerId>(p);
            if (!SameHistory(History(sim, id), History(loaded, id))) {
                fail("a loaded game's history is not the saved one's", p, 0);
            }
        }
        if (ok()) {
            RunToReading(sim, 5);
            RunToReading(loaded, 5);
        }
        const T::Record& us = History(sim, kUs);
        const std::vector<Fixed32> battles = Battles(us);
        if (ok() && battles.size() != 1) {
            fail("a battle of 0.27 was not kept", Count(battles.size()), 1);
        } else if (ok() && battles[0] != Fixed32(2 * kMilitia) / Fixed32(kOwned)) {
            fail("the battle is not weighed as 160 of 585", battles[0].value,
                 (Fixed32(2 * kMilitia) / Fixed32(kOwned)).value);
        } else if (ok() && us.events[0].a != kThem) {
            fail("the battle does not name the player it destroyed most of", us.events[0].a, kThem);
        } else if (ok() && (us.events[0].c != 1 || us.events[0].sample != 6)) {
            fail("the battle is not kept, marked for the reading after it ended",
                 us.events[0].sample, 6);
        }
        for (int p = 1; p <= 2 && ok(); ++p) {
            const auto id = static_cast<Core::PlayerId>(p);
            if (!SameHistory(History(sim, id), History(loaded, id))) {
                fail("the loaded game's battle did not end as the saved one's", p, 0);
            }
        }
    }

    // Three battles kept: a fourth pushes out the weaker of the two before
    // it -- never the oldest, though here the weakest -- and a fifth weaker
    // than both goes itself.
    if (ok()) {
        auto* seat = ECS::Players::Find(reg, kUs);
        auto battle = [&](int value, int reading) {
            ECS::Players::AddResource(*seat, S::MilitaryValue, Fixed32(value));
            RunToReading(sim, reading);
            RunToReading(sim, reading + 1);
        };
        const Fixed32 b1 = Fixed32(2 * kMilitia) / Fixed32(kOwned);
        const Fixed32 b2 = Fixed32(200) / Fixed32(kOwned);
        const Fixed32 b3 = Fixed32(170) / Fixed32(kOwned);
        const Fixed32 b4 = Fixed32(300) / Fixed32(kOwned);
        battle(200, 6);
        battle(170, 8);
        std::vector<Fixed32> want = {b3, b2, b1};
        if (Battles(History(sim, kUs)) != want) {
            fail("three battles are not all kept, newest first",
                 Count(Battles(History(sim, kUs)).size()), 3);
        }
        battle(300, 10);
        want = {b4, b2, b1};
        if (ok() && Battles(History(sim, kUs)) != want) {
            fail("a fourth battle did not push out the weaker of the two before it",
                 Count(Battles(History(sim, kUs)).size()), 3);
        }
        battle(90, 12);
        if (ok() && Battles(History(sim, kUs)) != want) {
            fail("a battle weaker than those before it was kept",
                 Count(Battles(History(sim, kUs)).size()), 3);
        }
    }

    // A Castle and a Wonder brought down: on the destroyer's history, the
    // owner noted.
    if (ok()) {
        const entt::entity castle = Raise(sim, "CSTL", kThem, x - 12, y + 12, true);
        const entt::entity wonder = Raise(sim, "WNDR", kUs, x + 12, y + 12, true);
        const entt::entity raider = Peaceful(sim, "SPRMN", kThem, x, y + 4);
        if (castle == entt::null || wonder == entt::null) verdict = "no ground for a Castle";
        if (ok()) {
            ECS::Combat::Hurt(reg, grid, castle, Fixed32(100000), soldier);
            ECS::Combat::Hurt(reg, grid, wonder, Fixed32(100000), raider);
        }
        Run(sim, 1);
        const T::Record& us = History(sim, kUs);
        const T::Record& them = History(sim, kThem);
        if (ok() && (them.events.size() < 2 || them.events[1].kind != T::kCastleBuilt ||
                     them.events[1].a != kThem)) {
            fail("a Castle finished is not on its owner's history",
                 them.events.size() > 1 ? them.events[1].kind : -1, T::kCastleBuilt);
        } else if (ok() && (us.events[0].kind != T::kCastleDestroyed || us.events[0].a != kThem)) {
            fail("a Castle brought down is not on its destroyer's history", us.events[0].kind,
                 T::kCastleDestroyed);
        } else if (ok() && (them.events[0].kind != T::kWonderDestroyed || them.events[0].a != kUs)) {
            fail("a Wonder brought down is not on its destroyer's history", them.events[0].kind,
                 T::kWonderDestroyed);
        }
    }

    // Out of the game: a resignation is not noted; a defeat is, naming player 1
    // with nothing when nobody's own units died. And nothing comes before the
    // first reading: a game begun in the Feudal Age has the age researched, but
    // not on the history.
    if (ok()) {
        ECS::Simulation third;
        Core::GameSetup three = Core::DefaultSetup();
        three.players[3] = {true, 1, 0};
        three.starting_age = 1;
        third.Init(three);
        if (!History(third, kUs).events.empty()) {
            fail("the starting age was noted before the first reading",
                 Count(History(third, kUs).events.size()), 0);
        } else if (Sheet(third, kUs).feudal_ms != 0) {
            fail("the game did not begin in the Feudal Age", Sheet(third, kUs).feudal_ms, 0);
        }
        Run(third, 1);
        ECS::Victory::Resign(third.GetRegistry(), third.GetGrid(), 3);
        Run(third, 1);
        if (!History(third, 3).events.empty()) {
            fail("a resignation was noted on the history", Count(History(third, 3).events.size()), 0);
        }
        ECS::Victory::Declare(reg, static_cast<uint16_t>(1u << kUs));
        Run(sim, 1);
        const T::Record& them = History(sim, kThem);
        if (ok() && (them.events.empty() || them.events[0].kind != T::kDefeated)) {
            fail("a defeat was not noted on the history",
                 them.events.empty() ? -1 : them.events[0].kind, T::kDefeated);
        } else if (ok() && (them.events[0].a != 1 || them.events[0].b != Fixed32(0))) {
            fail("the defeat does not name player 1 with nothing", them.events[0].a, 1);
        } else if (ok() && History(sim, kUs).events[0].kind == T::kDefeated) {
            fail("the winner's history noted a defeat", History(sim, kUs).events[0].kind, 0);
        }
    }
    Core::CommandQueue::Get().PopAllCommands();
}

} // namespace

// The score: concrete figures for each of the seven scoring elements and the
// statistics, at the moment each event happens -- the start's worth,
// research, a kill by blow and by a dead shooter's missile, Gaia's and walls'
// not counting, a razed house and a razed site, a conversion, tribute both
// ways, a Castle and a Wonder, the collected resources, the most ever under
// arms, the map explored once a second and in full when revealed, survival,
// the team figure -- and all of it carried by a save.
bool CheckScore() {
    const char* verdict = "ok";
    std::string detail;
    auto ok = [&]() { return std::strcmp(verdict, "ok") == 0; };
    auto fail = [&](const char* why, int got, int want) {
        if (!ok()) return;
        verdict = why;
        detail = " (" + std::to_string(got) + ", not " + std::to_string(want) + ")";
    };

    ECS::Simulation sim;
    Core::GameSetup setup = Core::DefaultSetup();
    setup.lock_teams = false;   // so a tribute may go to the enemy
    sim.Init(setup);
    auto& reg = sim.GetRegistry();
    auto& grid = sim.GetGrid();
    Core::CommandQueue::Get().PopAllCommands();
    Run(sim, 1);

    // The start: what a player owns is worth its price, the stockpile a
    // tenth of itself; nothing researched though Init cascaded dozens of
    // technologies nobody researches at a building; no second yet, so no
    // map explored.
    {
        const auto s = Sheet(sim, kUs);
        if (Res(sim, kUs, S::ValueOwned) != kStartValue) {
            fail("the start's units and Town Center are not worth their price",
                 Res(sim, kUs, S::ValueOwned), kStartValue);
        } else if (s.economy != 20 + 20 + 20 + 10 + kStartValue / 5) {
            fail("the starting economy is not the stockpile's tenth and the value's fifth",
                 s.economy, 20 + 20 + 20 + 10 + kStartValue / 5);
        } else if (s.military != 0 || s.society != 0 || s.technology != 0) {
            fail("a score other than the economy at the start", s.total, s.economy);
        } else if (s.total != s.economy) {
            fail("the total is not the sum of the categories", s.total, s.economy);
        } else if (s.research_count != 0) {
            fail("a technology no building researches was counted", s.research_count, 0);
        } else if (s.villager_high != 3) {
            fail("three villagers are not the villager high", s.villager_high, 3);
        } else if (s.largest_army != 1) {
            fail("the Scout Cavalry is not the largest army", s.largest_army, 1);
        } else if (s.survived != 1) {
            fail("a player at the start is not surviving", s.survived, 1);
        }
    }

    // The map explored is counted once a game second, not before.
    if (ok()) {
        Run(sim, 61);   // 62 ticks of 16 ms: 992 ms
        if (Sheet(sim, kUs).map_explored != 0) {
            fail("the map explored was counted before a second had passed",
                 Sheet(sim, kUs).map_explored, 0);
        }
        Run(sim, 1);   // 1008 ms
        const int pct = ExploredPercent(sim, kUs);
        const auto s = Sheet(sim, kUs);
        if (pct <= 0) {
            fail("nothing explored to count", pct, 1);
        } else if (s.map_explored != pct) {
            fail("the map explored is not explored tiles over all tiles", s.map_explored, pct);
        } else if (s.technology != 10 * pct) {
            fail("exploration is not ten points a percent", s.technology, 10 * pct);
        }
        // More ground seen counts at the next second.
        ECS::Vision::Explore(reg, kUs, grid.GetWidth() / 2, grid.GetHeight() / 2, 20);
        Run(sim, kTicksPerSecond + 1);
        if (ok() && Sheet(sim, kUs).map_explored != ExploredPercent(sim, kUs)) {
            fail("newly explored ground was not counted", Sheet(sim, kUs).map_explored,
                 ExploredPercent(sim, kUs));
        }
        // A percent and a half or more is still the percent below: tiles
        // explored one at a time until the share's fraction is past a half.
        const int tiles = grid.GetWidth() * grid.GetHeight();
        int seen = ExploredTiles(sim, kUs);
        for (int ty = 0; ty < grid.GetHeight() && (seen * 100) % tiles < tiles / 2; ++ty) {
            for (int tx = 0; tx < grid.GetWidth() && (seen * 100) % tiles < tiles / 2; ++tx) {
                if (ECS::Vision::Explored(reg, kUs, tx, ty)) continue;
                ECS::Vision::Explore(reg, kUs, tx, ty, 0);
                ++seen;
            }
        }
        Run(sim, kTicksPerSecond + 1);
        seen = ExploredTiles(sim, kUs);
        if (ok() && (seen * 100) % tiles < tiles / 2) {
            verdict = "the explored share could not be set past a half";
        } else if (ok() && Sheet(sim, kUs).map_explored != seen * 100 / tiles) {
            fail("the map explored was not rounded down", Sheet(sim, kUs).map_explored,
                 seen * 100 / tiles);
        }
    }

    // Research: Loom adds its 50 gold's fifth and one to the count; the
    // Feudal Age its 500 food's, and its time.
    if (ok()) {
        ECS::Techs::Complete(reg, kUs, 22);
        Run(sim, 1);
        auto s = Sheet(sim, kUs);
        if (s.research_count != 1 || Res(sim, kUs, S::ResearchValue) != 50) {
            fail("Loom was not counted at its price", Res(sim, kUs, S::ResearchValue), 50);
        } else if (s.technology != 50 / 5 + 10 * s.map_explored) {
            fail("research is not a fifth of its price", s.technology,
                 50 / 5 + 10 * s.map_explored);
        }
        const int32_t at = reg.ctx().get<ECS::Score::Table>().clock_ms;
        ECS::Techs::Complete(reg, kUs, 101);
        Run(sim, 1);
        s = Sheet(sim, kUs);
        if (ok() && (s.research_count != 2 || s.technology != 550 / 5 + 10 * s.map_explored)) {
            fail("the Feudal Age was not counted at its price", s.technology,
                 550 / 5 + 10 * s.map_explored);
        } else if (ok() && (s.feudal_ms != at || s.castle_ms != -1)) {
            fail("the Feudal Age's time is not the moment it was reached", s.feudal_ms, at);
        }
    }

    // The research cost modifier, the Chinese discount: each slot times the
    // dat's float and truncated, so Wheelbarrow's 175 food and 50 wood at 0.9
    // come to 157 and 44, not 202; an age's price is never modified.
    if (ok()) {
        auto* seat = ECS::Players::Find(reg, kUs);
        const Fixed32 kept = ECS::Players::Resource(*seat, 85);
        ECS::Players::SetResource(*seat, 85, Fixed32(900) / Fixed32(1000));
        const int before = Res(sim, kUs, S::ResearchValue);
        ECS::Techs::Complete(reg, kUs, 213);
        if (Res(sim, kUs, S::ResearchValue) - before != 157 + 44) {
            fail("the research cost modifier was not applied as the original's float does",
                 Res(sim, kUs, S::ResearchValue) - before, 157 + 44);
        }
        const int32_t at = reg.ctx().get<ECS::Score::Table>().clock_ms;
        ECS::Techs::Complete(reg, kUs, 102);
        Run(sim, 1);
        if (ok() && Res(sim, kUs, S::ResearchValue) - before != 157 + 44 + 800 + 200) {
            fail("an age's price was modified", Res(sim, kUs, S::ResearchValue) - before,
                 157 + 44 + 800 + 200);
        } else if (ok() && (Sheet(sim, kUs).castle_ms != at || Sheet(sim, kUs).research_count != 4)) {
            fail("the Castle Age was not counted when it was reached", Sheet(sim, kUs).castle_ms,
                 at);
        }
        ECS::Players::SetResource(*seat, 85, kept);
    }

    int x = grid.GetWidth() / 2;
    int y = grid.GetHeight() / 2;
    if (ok() && !Clearing(grid, x, y, 6)) verdict = "no open ground to fight on";

    // A kill by a blow: the Militia's 80 to the killer's military, a unit
    // killed and lost each side, the per-player slots; the victim's worth
    // back off its books.
    entt::entity soldier = entt::null;
    if (ok()) {
        soldier = Peaceful(sim, "SPRMN", kUs, x - 2, y);
        const entt::entity victim = Peaceful(sim, "SPRMN", kThem, x + 2, y);
        Run(sim, 1);
        const int theirs = Res(sim, kThem, S::ValueOwned);
        if (theirs != kStartValue + kMilitia) {
            fail("a unit spawned was not added at its price", theirs, kStartValue + kMilitia);
        }
        ECS::Combat::Hurt(reg, grid, victim, Fixed32(1000), soldier);
        Run(sim, 1);
        const auto s = Sheet(sim, kUs);
        if (ok() && (s.units_killed != 1 || Res(sim, kUs, S::MilitaryValue) != kMilitia)) {
            fail("a kill was not counted at the victim's value", Res(sim, kUs, S::MilitaryValue),
                 kMilitia);
        } else if (ok() && s.military != kMilitia / 5) {
            fail("military is not a fifth of the value", s.military, kMilitia / 5);
        } else if (ok() && (Res(sim, kUs, S::KilledFirst + kThem - 1) != 1 ||
                            Res(sim, kUs, S::KillValueFirst + kThem - 1) != kMilitia)) {
            fail("the kill was not kept against the victim's player",
                 Res(sim, kUs, S::KilledFirst + kThem - 1), 1);
        } else if (ok() && (Sheet(sim, kThem).units_lost != 1 ||
                            Res(sim, kThem, S::ValueLost) != kMilitia ||
                            Res(sim, kThem, S::UnitsLostToFirst + kUs - 1) != 1)) {
            fail("the loss was not kept for the victim", Sheet(sim, kThem).units_lost, 1);
        } else if (ok() && Res(sim, kThem, S::ValueOwned) != kStartValue) {
            fail("a unit killed kept its worth on its owner's books",
                 Res(sim, kThem, S::ValueOwned), kStartValue);
        }
    }

    // A missile whose shooter is gone is still its owner's kill; Gaia's
    // animals are nobody's.
    if (ok()) {
        const entt::entity second = Peaceful(sim, "SPRMN", kThem, x + 2, y + 2);
        ECS::Combat::Hurt(reg, grid, second, Fixed32(1000), entt::null, kUs);
        const entt::entity deer = Peaceful(sim, "DEERX", Core::PLAYER_GAIA, x, y + 3);
        ECS::Combat::Hurt(reg, grid, deer, Fixed32(1000), soldier);
        Run(sim, 1);
        if (Sheet(sim, kUs).units_killed != 2) {
            fail("a dead shooter's missile, or Gaia's deer, was misattributed",
                 Sheet(sim, kUs).units_killed, 2);
        } else if (Sheet(sim, kUs).military != 2 * kMilitia / 5) {
            fail("two kills are not two fifths", Sheet(sim, kUs).military, 2 * kMilitia / 5);
        }
    }

    // And in flight: an archer's arrow loosed, the archer gone from the world
    // before it lands, the kill still the archer's owner's.
    if (ok()) {
        const entt::entity mark = Peaceful(sim, "SPRMN", kThem, x - 2, y + 5);
        reg.get<ECS::Components::Health>(mark).current_hp = 1;
        const int kills = Sheet(sim, kUs).units_killed;
        bool loosed = false;
        for (int attempt = 0; attempt < 5 && ECS::Combat::Alive(reg, mark); ++attempt) {
            const entt::entity archer = ECS::Units::Spawn(
                reg, Core::ConfigManager::Get().GetUnitIdByName("ARCHR"), kUs,
                Shore::At(x - 5, y + 5));
            Core::CommandQueue::Get().PushCommand(Core::AttackCommand{archer, mark, kUs});
            bool flying = false;
            for (int t = 0; t < 6 * kTicksPerSecond && !flying; ++t) {
                sim.Update(16);
                for (auto arrow : reg.view<const ECS::Components::Projectile>()) {
                    if (reg.get<const ECS::Components::Projectile>(arrow).shooter == archer) {
                        flying = true;
                    }
                }
            }
            if (!flying) break;
            loosed = true;
            reg.destroy(archer);
            Run(sim, 3 * kTicksPerSecond);
        }
        if (!loosed) {
            verdict = "an archer loosed nothing to test with";
        } else if (ECS::Combat::Alive(reg, mark)) {
            verdict = "no arrow found its mark to test with";
        } else if (Sheet(sim, kUs).units_killed != kills + 1) {
            fail("an arrow whose archer was gone was not its owner's kill",
                 Sheet(sim, kUs).units_killed, kills + 1);
        }
    }

    // Razing: a House's 30 to the military and the razing value; a wall
    // never counts; a site razed counts, though it was never worth anything
    // on its owner's books.
    if (ok()) {
        const int before = Res(sim, kUs, S::MilitaryValue);
        const entt::entity house = Raise(sim, "HOUS", kThem, x + 4, y - 4, true);
        const int theirs = Res(sim, kThem, S::ValueOwned);
        if (house == entt::null) verdict = "no ground for a House";
        if (ok() && theirs != kStartValue + kHouse) {
            fail("a finished House was not added at its price", theirs, kStartValue + kHouse);
        }
        if (ok()) ECS::Combat::Hurt(reg, grid, house, Fixed32(100000), soldier);
        const entt::entity wall = ok() ? Raise(sim, "WALL", kThem, x - 4, y - 4, true) : entt::null;
        if (ok() && wall == entt::null) verdict = "no ground for a wall";
        if (ok()) ECS::Combat::Hurt(reg, grid, wall, Fixed32(100000), soldier);
        Run(sim, 1);
        const int military = before + kHouse;
        if (ok() && (Sheet(sim, kUs).buildings_razed != 1 ||
                     Res(sim, kUs, S::MilitaryValue) != military ||
                     Res(sim, kUs, S::RazingValue) != kHouse ||
                     Res(sim, kUs, S::RazingsFirst + kThem - 1) != 1 ||
                     Res(sim, kUs, S::RazingValueFirst + kThem - 1) != kHouse)) {
            fail("a House razed, or a wall, was not counted as the original counts",
                 Res(sim, kUs, S::MilitaryValue), military);
        } else if (ok() && (Sheet(sim, kThem).buildings_lost != 1 ||
                            Res(sim, kThem, S::BuildingValueLost) != kHouse ||
                            Res(sim, kThem, S::BuildingsLostToFirst + kUs - 1) != 1)) {
            fail("the House's loss was not kept for its owner", Sheet(sim, kThem).buildings_lost,
                 1);
        } else if (ok() && Res(sim, kThem, S::ValueOwned) != kStartValue) {
            fail("a House razed kept its worth", Res(sim, kThem, S::ValueOwned), kStartValue);
        }
        const entt::entity site = ok() ? Raise(sim, "HOUS", kThem, x + 4, y + 5, false) : entt::null;
        if (ok() && site == entt::null) verdict = "no ground for a House site";
        if (ok()) {
            ECS::Combat::Hurt(reg, grid, site, Fixed32(100000), soldier);
            Run(sim, 1);
            if (Sheet(sim, kUs).buildings_razed != 2 ||
                Res(sim, kUs, S::MilitaryValue) != military + kHouse) {
                fail("a site razed was not counted at its price", Res(sim, kUs, S::MilitaryValue),
                     military + kHouse);
            } else if (Res(sim, kThem, S::ValueOwned) != kStartValue) {
                fail("a site razed took worth it never added", Res(sim, kThem, S::ValueOwned),
                     kStartValue);
            }
        }
    }

    // A site is worth nothing until it is finished, then its price.
    if (ok()) {
        const int worth = Res(sim, kUs, S::ValueOwned);
        const entt::entity site = Raise(sim, "HOUS", kUs, x - 8, y - 8, false);
        if (site == entt::null) verdict = "no ground for a House site of our own";
        Run(sim, 1);
        if (ok() && Res(sim, kUs, S::ValueOwned) != worth) {
            fail("a House site was worth something before it was finished",
                 Res(sim, kUs, S::ValueOwned), worth);
        }
        if (ok()) ECS::Buildings::Complete(reg, grid, site);
        Run(sim, 1);
        if (ok() && Res(sim, kUs, S::ValueOwned) != worth + kHouse) {
            fail("a House finished was not added at its price", Res(sim, kUs, S::ValueOwned),
                 worth + kHouse);
        }
    }

    // A conversion: the converter's military gains the unit's value, its
    // loser a unit lost and the worth.
    if (ok()) {
        const entt::entity monk = Peaceful(sim, "MONKX", kUs, x - 1, y + 1);
        const entt::entity target = Peaceful(sim, "SPRMN", kThem, x + 3, y + 1);
        Run(sim, 1);
        Core::CommandQueue::Get().PushCommand(Core::AttackCommand{monk, target, kUs});
        int military = Res(sim, kUs, S::MilitaryValue);
        int lost = Res(sim, kThem, S::UnitsLost);
        int worth = Res(sim, kThem, S::ValueOwned);
        bool took = false;
        for (int t = 0; t < 12 * kTicksPerSecond && !took; ++t) {
            military = Res(sim, kUs, S::MilitaryValue);
            lost = Res(sim, kThem, S::UnitsLost);
            worth = Res(sim, kThem, S::ValueOwned);
            sim.Update(16);
            took = reg.get<ECS::Components::Owner>(target).player_id == kUs;
        }
        if (!took) {
            verdict = "a monk did not convert to test with";
        } else if (Sheet(sim, kUs).units_converted != 1 ||
                   Res(sim, kUs, S::MilitaryValue) != military + kMilitia) {
            fail("a conversion did not add the unit's value", Res(sim, kUs, S::MilitaryValue),
                 military + kMilitia);
        } else if (Res(sim, kThem, S::UnitsLost) != lost + 1 ||
                   Res(sim, kThem, S::ValueOwned) != worth - kMilitia) {
            fail("the converted unit's loss was not kept", Res(sim, kThem, S::ValueOwned),
                 worth - kMilitia);
        }
    }

    // Tribute: 100 food away costs the stockpile 130 and wins back a tenth
    // of the 100 sent; the recipient's net is negative and counts nothing.
    if (ok()) {
        const auto [sx, sy] = sim.player_starts[0];
        if (Shore::RaiseNear(sim, kUs, Core::ConfigManager::Get().GetUnitIdByName("MRKT"), sx + 8,
                             sy + 2, 14) == entt::null) {
            verdict = "no ground for a Market";
        }
        if (ok()) {
            ECS::Players::SetResource(*ECS::Players::Find(reg, kUs), R::Res::Food, Fixed32(400));
            Run(sim, 1);
            const int base = Sheet(sim, kUs).economy;
            if (ECS::Tribute::Send(reg, kUs, kThem, R::Res::Food, 100) !=
                ECS::Tribute::Refusal::None) {
                verdict = "a tribute to test with was refused";
            }
            Run(sim, 1);
            const auto s = Sheet(sim, kUs);
            if (ok() && (s.tribute_sent != 100 || Res(sim, kUs, S::TributeGiven) != 100)) {
                fail("the tribute sent was not kept", Res(sim, kUs, S::TributeGiven), 100);
            } else if (ok() && (Sheet(sim, kThem).tribute_received != 100 ||
                                Res(sim, kThem, S::TributeFromFirst + kUs - 1) != 100)) {
                fail("the tribute received was not kept", Sheet(sim, kThem).tribute_received,
                     100);
            } else if (ok() && s.economy != base - 400 / 10 + 270 / 10 + 100 / 10) {
                fail("tribute sent did not net a tenth back", s.economy,
                     base - 400 / 10 + 270 / 10 + 100 / 10);
            } else if (ok() && Sheet(sim, kThem).economy != Economy(sim, kThem)) {
                fail("tribute received was not left out of the economy",
                     Sheet(sim, kThem).economy, Economy(sim, kThem));
            }
        }
    }

    // A Castle and a Wonder: society, never taken away; a Castle that falls
    // still takes its worth off the economy's books, though it never put it
    // there, and no further than nothing.
    if (ok()) {
        const entt::entity castle = Raise(sim, "CSTL", kUs, x - 10, y + 10, true);
        if (castle == entt::null) verdict = "no ground for a Castle";
        Run(sim, 1);
        if (ok() && (Sheet(sim, kUs).castles != 1 || Sheet(sim, kUs).society != 650 / 5)) {
            fail("a Castle is not a fifth of its 650 to society", Sheet(sim, kUs).society,
                 650 / 5);
        }
        const int worth = Res(sim, kUs, S::ValueOwned);
        if (ok()) ECS::Buildings::Destroy(reg, grid, castle);
        Run(sim, 1);
        const int left = worth > 650 ? worth - 650 : 0;
        if (ok() && Sheet(sim, kUs).society != 650 / 5) {
            fail("a Castle's society was taken away with it", Sheet(sim, kUs).society, 650 / 5);
        } else if (ok() && Res(sim, kUs, S::ValueOwned) != left) {
            fail("a Castle's fall did not take its worth off, to no less than nothing",
                 Res(sim, kUs, S::ValueOwned), left);
        }
        const entt::entity wonder = ok() ? Raise(sim, "WNDR", kUs, x + 12, y - 12, true)
                                         : entt::null;
        if (ok() && wonder == entt::null) verdict = "no ground for a Wonder";
        Run(sim, 1);
        if (ok() && (Sheet(sim, kUs).wonders != 1 ||
                     Sheet(sim, kUs).society != (650 + 3000) / 5)) {
            fail("a Wonder is not a fifth of its 3000 to society", Sheet(sim, kUs).society,
                 (650 + 3000) / 5);
        }
    }

    // Collected: what a villager hands in.
    if (ok()) {
        entt::entity villager = entt::null;
        auto workers = reg.view<ECS::Components::Worker, const ECS::Components::Owner>();
        for (auto e : workers) {
            if (workers.get<const ECS::Components::Owner>(e).player_id == kUs) villager = e;
        }
        auto* seat = ECS::Players::Find(reg, kUs);
        if (villager == entt::null) verdict = "no villager to hand anything in";
        if (ok()) {
            auto& job = reg.get<ECS::Components::Worker>(villager);
            job.carried[static_cast<int>(Core::ResourceType::Wood)] = 10;
            job.carried[static_cast<int>(Core::ResourceType::Stone)] = 7;
            ECS::DropSites::Deposit(Core::ConfigManager::Get().GetUnitIdByName("RTWC"), job, seat);
            Run(sim, 1);
            const auto s = Sheet(sim, kUs);
            if (s.wood_collected != 10 || s.stone_collected != 7 || s.food_collected != 0) {
                fail("what a villager handed in was not collected", s.wood_collected, 10);
            }
        }
    }

    // The most ever under arms: five more Militia count at once, and still
    // while they die; the most stays when they are gone.
    if (ok()) {
        const int army = Res(sim, kUs, S::MilitaryPopulation);
        std::vector<entt::entity> squad;
        for (int i = 0; i < 5; ++i) squad.push_back(Peaceful(sim, "SPRMN", kUs, x - 3, y - 2 + i));
        Run(sim, 1);
        if (Sheet(sim, kUs).largest_army != army + 5) {
            fail("five more soldiers did not make the largest army", Sheet(sim, kUs).largest_army,
                 army + 5);
        }
        for (entt::entity e : squad) ECS::Mortality::Kill(reg, grid, e);
        Run(sim, 1);
        if (ok() && Res(sim, kUs, S::MilitaryPopulation) != army + 5) {
            fail("soldiers still dying were not counted", Res(sim, kUs, S::MilitaryPopulation),
                 army + 5);
        }
        Run(sim, 10 * kTicksPerSecond);
        if (ok() && Res(sim, kUs, S::MilitaryPopulation) != army) {
            fail("the dead were still counted", Res(sim, kUs, S::MilitaryPopulation), army);
        } else if (ok() && Sheet(sim, kUs).largest_army != army + 5) {
            fail("the largest army fell with its soldiers", Sheet(sim, kUs).largest_army,
                 army + 5);
        }
    }

    // A save carries all of it; the game loaded goes on scoring the same.
    if (ok()) {
        std::vector<uint8_t> bytes;
        ECS::Simulation loaded;
        std::string error;
        if (!Save::Write(sim, bytes) || !Save::Read(loaded, bytes, &error)) {
            verdict = "the scored game would not save and load";
        }
        for (int t = 0; ok() && t < 3; ++t) {
            for (int p = 1; p <= 2 && ok(); ++p) {
                const auto id = static_cast<Core::PlayerId>(p);
                if (!SameSheet(Sheet(sim, id), Sheet(loaded, id))) {
                    fail("a loaded game's score is not the saved one's", Sheet(loaded, id).total,
                         Sheet(sim, id).total);
                }
            }
            sim.Update(16);
            loaded.Update(16);
        }
    }

    // Out of the game: not a survivor, and what it loses by resigning is
    // nobody's kill; nothing of it is left on its books.
    if (ok()) {
        const int kills = Sheet(sim, kUs).units_killed;
        ECS::Victory::Resign(reg, grid, kThem);
        Run(sim, 1);
        if (Sheet(sim, kThem).survived != 0 || Sheet(sim, kUs).survived != 1) {
            fail("survival did not follow who is out", Sheet(sim, kThem).survived, 0);
        } else if (Sheet(sim, kUs).units_killed != kills) {
            fail("a resignation's losses were counted as kills", Sheet(sim, kUs).units_killed,
                 kills);
        } else if (Res(sim, kThem, S::ValueOwned) != 0) {
            fail("a player with nothing left is still worth something",
                 Res(sim, kThem, S::ValueOwned), 0);
        }
    }

    // The team figure: allies who both pursue an allied victory average
    // their totals; without it, a player's own.
    if (ok()) {
        Core::GameSetup setup = Core::DefaultSetup();
        setup.players[1].team = 1;
        setup.players[2].team = 1;
        setup.lock_teams = false;   // so allied victory may be given up
        ECS::Simulation team;
        team.Init(setup);
        ECS::Players::SetResource(*ECS::Players::Find(team.GetRegistry(), kThem), R::Res::Food,
                                  Fixed32(1200));
        Run(team, 1);
        const int a = Sheet(team, kUs).total;
        const int b = Sheet(team, kThem).total;
        if (a == b) verdict = "the team test's totals do not differ";
        if (ok() && ECS::Score::TeamAverage(team.GetRegistry(), kUs) != (a + b) / 2) {
            fail("allies' totals were not averaged", ECS::Score::TeamAverage(team.GetRegistry(), kUs),
                 (a + b) / 2);
        }
        if (ok() && !ECS::Diplomacy::SetAlliedVictory(team.GetRegistry(), kUs, false)) {
            verdict = "allied victory could not be given up to test with";
        }
        if (ok() && ECS::Score::TeamAverage(team.GetRegistry(), kUs) != a) {
            fail("without allied victory the team figure is not one's own",
                 ECS::Score::TeamAverage(team.GetRegistry(), kUs), a);
        }
    }

    // A map revealed in the lobby is all explored: a thousand points.
    if (ok()) {
        Core::GameSetup setup = Core::DefaultSetup();
        setup.reveal = Core::Reveal::Explored;
        ECS::Simulation open;
        open.Init(setup);
        Run(open, kTicksPerSecond + 1);
        if (Sheet(open, kUs).map_explored != 100 || Sheet(open, kUs).technology != 1000) {
            fail("a revealed map is not all explored", Sheet(open, kUs).technology, 1000);
        }
    }

    // The history the Timeline draws.
    if (ok()) CheckHistory(verdict, detail);

    Core::CommandQueue::Get().PopAllCommands();
    std::printf("score=%s%s\n", verdict, ok() ? "" : detail.c_str());
    return ok();
}

} // namespace Verify
