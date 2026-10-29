#include "verify/Harness.h"

#include <cstdio>
#include <cstring>
#include <vector>

#include "core/Command.h"
#include "core/CommandQueue.h"
#include "core/ConfigManager.h"
#include "core/FixedMath.h"
#include "core/Rules.h"
#include "ecs/Events.h"
#include "ecs/Buildings.h"
#include "ecs/Combat.h"
#include "ecs/Players.h"
#include "ecs/Simulation.h"
#include "ecs/Units.h"
#include "ecs/components/Animation.h"
#include "ecs/components/Combat.h"
#include "ecs/components/Health.h"
#include "ecs/components/Identity.h"
#include "ecs/components/Mortal.h"
#include "ecs/components/Owner.h"
#include "ecs/components/Path.h"
#include "ecs/components/Projectile.h"
#include "ecs/components/Transform.h"
#include "ecs/components/Worker.h"

namespace Verify {

namespace {

constexpr Core::PlayerId kUs = 1;
constexpr Core::PlayerId kThem = 2;
constexpr int kTicksPerSecond = 1000 / 16;

// Open ground: the first spot from (x, y) outward with a clear square round it.
bool OpenGround(const Physics::Grid& grid, int& x, int& y, int half) {
    for (int radius = 0; radius < 40; ++radius) {
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

// A point inside a tile, off its centre and corners, the way units stand.
// Tiles are centred on whole numbers, so the offsets stay within half a tile
// of it -- seven tenths would round into the next tile, which may be a tree.
ECS::Components::Vector2D At(int x, int y) {
    return {Core::Fixed32(x) + Core::Fixed32(3) / Core::Fixed32(10),
            Core::Fixed32(y) - Core::Fixed32(2) / Core::Fixed32(10)};
}

int CorpseCount(const entt::registry& reg) {
    int n = 0;
    auto view = reg.view<const ECS::Components::Corpse>();
    for (auto e : view) {
        (void)e;
        ++n;
    }
    return n;
}

// Open ground about three tiles from `around`, for a hunter to start on.
ECS::Components::Vector2D BesidePrey(const Physics::Grid& grid,
                                     ECS::Components::Vector2D around) {
    static const int kOffX[8] = {3, -3, 0, 0, 2, -2, 2, -2};
    static const int kOffY[8] = {0, 0, 3, -3, 2, 2, -2, -2};
    for (int i = 0; i < 8; ++i) {
        const int x = around.x.ToInt() + kOffX[i];
        const int y = around.y.ToInt() + kOffY[i];
        if (grid.IsWalkable(x, y)) return At(x, y);
    }
    return around;
}

// The nearest map object of `unit_id` to a point.
entt::entity Nearest(const entt::registry& reg, int unit_id, ECS::Components::Vector2D from) {
    entt::entity best = entt::null;
    Core::Fixed32 best_d{};
    auto view = reg.view<const ECS::Components::Identity, const ECS::Components::Transform>();
    for (auto e : view) {
        if (view.get<const ECS::Components::Identity>(e).unit_id != unit_id) continue;
        if (!ECS::Combat::Alive(reg, e)) continue;
        const auto& p = view.get<const ECS::Components::Transform>(e).position;
        const Core::Fixed32 d = Core::FixedMath::Distance(p.x - from.x, p.y - from.y);
        if (best == entt::null || d < best_d) {
            best = e;
            best_d = d;
        }
    }
    return best;
}

} // namespace

// Fighting by the dat's rules, watched blow by blow: the damage done, an
// archer's arrow in flight, a dead unit's place in the population given back,
// and a hunter going on to its deer's carcass are each looked at directly.
bool CheckCombat() {
    ECS::Simulation headless;
    headless.Init();
    auto& reg = headless.GetRegistry();
    auto& grid = headless.GetGrid();
    auto& cfg = Core::ConfigManager::Get();
    const char* verdict = "ok";
    auto ok = [&]() { return std::strcmp(verdict, "ok") == 0; };
    auto run = [&](int ticks) {
        for (int t = 0; t < ticks; ++t) headless.Update(16);
    };

    const int militia_id = cfg.GetUnitIdByName("SPRMN");
    const int archer_id = cfg.GetUnitIdByName("ARCHR");
    const int knight_id = cfg.GetUnitIdByName("KNGHT");
    const int spear_id = cfg.GetUnitIdByName("PKEMN");
    const int villager_id = cfg.GetUnitIdByName("VMBAS");
    const int tc_id = cfg.GetUnitIdByName("RTWC");
    const int tower_id = cfg.GetUnitIdByName("WCTW");
    const int deer_id = cfg.GetUnitIdByName("DEERX");
    const int boar_id = cfg.GetUnitIdByName("BOARX");

    // The formula, against figures worked out from the dat: a spearman's
    // 3 melee less a knight's 2 armour, plus its 15 against cavalry, which the
    // knight has 0 armour for -- 16; a villager's 3 against a Town Center's
    // building class -- 3, its melee being stopped by the Town Center's 3; an
    // archer's 4 pierce against the Town Center's 5 -- nothing, so the least a
    // blow can do, 1.
    {
        const auto* spear = ECS::Players::UnitOf(reg, kUs, spear_id);
        const auto* knight = ECS::Players::UnitOf(reg, kUs, knight_id);
        const auto* villager = ECS::Players::UnitOf(reg, kUs, villager_id);
        const auto* archer = ECS::Players::UnitOf(reg, kUs, archer_id);
        const auto* tc = ECS::Players::UnitOf(reg, kUs, tc_id);
        if (!spear || !knight || !villager || !archer || !tc) {
            verdict = "the units this run fights with are missing from the data";
        } else if (ECS::Combat::Damage(spear->attacks, spear->attack_count, *knight) != 16) {
            verdict = "a spearman does not do 16 to a knight";
        } else if (ECS::Combat::Damage(villager->attacks, villager->attack_count, *tc) != 3) {
            verdict = "a villager does not do 3 to a town center";
        } else if (ECS::Combat::Damage(archer->attacks, archer->attack_count, *tc) != 1) {
            verdict = "a blow that armour stops entirely does not still do 1";
        }
    }

    // Each fight gets its own patch of open ground, far enough apart that no
    // one in one can see into another.
    entt::entity our_tc = entt::null;
    {
        auto buildings = reg.view<ECS::Components::Identity, ECS::Components::Owner>();
        for (auto b : buildings) {
            if (buildings.get<ECS::Components::Identity>(b).unit_id == tc_id &&
                buildings.get<ECS::Components::Owner>(b).player_id == kUs) {
                our_tc = b;
            }
        }
    }
    if (ok() && our_tc == entt::null) verdict = "no town center to fight beside";
    const auto base = ok() ? reg.get<ECS::Components::Transform>(our_tc).position
                           : ECS::Components::Vector2D{};

    // Reach is measured as the original measures it, between boxes of the
    // dat's sizes: two militia a tile apart each way are their boxes' corners
    // apart, not their circles'.
    if (ok()) {
        const auto* militia = ECS::Players::UnitOf(reg, kUs, militia_id);
        const ECS::Components::Vector2D at{base.x + Core::Fixed32(30), base.y + Core::Fixed32(30)};
        const entt::entity a = ECS::Units::Spawn(reg, militia_id, kUs, at);
        const entt::entity b = ECS::Units::Spawn(reg, militia_id, kThem,
                                                 {at.x + Core::Fixed32(1), at.y + Core::Fixed32(1)});
        const Core::Fixed32 side = Core::Fixed32(1) - militia->size_x - militia->size_x;
        if (ECS::Combat::Gap(reg, a, b) != Core::FixedMath::Distance(side, side)) {
            verdict = "the gap between two units is not between their boxes";
        }
        reg.destroy(a);
        reg.destroy(b);
    }
    auto patch = [&](int dx, int dy, int& x, int& y) {
        x = base.x.ToInt() + dx;
        y = base.y.ToInt() + dy;
        return OpenGround(grid, x, y, 4);
    };

    // A duel: two militia, each finds the other on its own, and fights until
    // one is dead. The dead one plays its death, gives back its place in its
    // owner's population, and leaves its body.
    if (ok()) {
        int x = 0;
        int y = 0;
        if (!patch(14, 0, x, y)) {
            verdict = "no open ground for a duel";
        } else {
            const entt::entity ours = ECS::Units::Spawn(reg, militia_id, kUs, At(x, y));
            const entt::entity theirs = ECS::Units::Spawn(reg, militia_id, kThem, At(x + 2, y));
            // Wounded already, so the duel has a winner: two identical militia
            // starting in step trade blow for blow and fall on the same tick.
            reg.get<ECS::Components::Health>(theirs).current_hp /= 2;
            const auto* us = ECS::Players::Find(reg, kUs);
            const auto* them = ECS::Players::Find(reg, kThem);
            const int our_pop = ECS::Players::Resource(*us, Core::Rules::Res::CurrentPopulation).ToInt();
            const int their_pop = ECS::Players::Resource(*them, Core::Rules::Res::CurrentPopulation).ToInt();
            const int corpses = CorpseCount(reg);
            bool saw_dying = false;
            // The blows and the death are news as well as state: the alarm
            // and the death cry hear of them only through the event log.
            bool heard_blow = false;
            bool heard_death = false;
            int t = 0;
            for (; t < 90 * kTicksPerSecond; ++t) {
                headless.Update(16);
                if (const ECS::Events::Log* log = ECS::Events::Read(reg)) {
                    for (const auto& event : log->events) {
                        if (event.tick != log->tick) continue;
                        const bool duellist = event.subject == ours || event.subject == theirs;
                        if (event.kind == ECS::Events::Kind::Struck && duellist &&
                            (event.other == ours || event.other == theirs)) {
                            heard_blow = true;
                        }
                        if (event.kind == ECS::Events::Kind::Died && duellist) heard_death = true;
                    }
                }
                if (reg.valid(ours) && reg.all_of<ECS::Components::Dying>(ours)) saw_dying = true;
                if (reg.valid(theirs) && reg.all_of<ECS::Components::Dying>(theirs)) saw_dying = true;
                if (!reg.valid(ours) || !reg.valid(theirs)) break;
            }
            run(2 * kTicksPerSecond);
            const bool we_won = reg.valid(ours) && !reg.valid(theirs);
            const bool they_won = reg.valid(theirs) && !reg.valid(ours);
            if (!we_won && !they_won) {
                verdict = "two militia fought for a minute and a half and neither died";
            } else if (!saw_dying) {
                verdict = "a unit vanished without dying";
            } else if (!heard_blow || !heard_death) {
                verdict = "the event log missed a duel's blows or its death";
            } else if (CorpseCount(reg) <= corpses) {
                verdict = "a dead unit left no body";
            } else if (we_won &&
                       ECS::Players::Resource(*them, Core::Rules::Res::CurrentPopulation).ToInt() !=
                           their_pop - 1) {
                verdict = "the dead unit's place in the population was not given back";
            } else if (they_won &&
                       ECS::Players::Resource(*us, Core::Rules::Res::CurrentPopulation).ToInt() !=
                           our_pop - 1) {
                verdict = "the dead unit's place in the population was not given back";
            } else {
                const entt::entity winner = we_won ? ours : theirs;
                const auto& hp = reg.get<ECS::Components::Health>(winner);
                if (hp.current_hp >= hp.max_hp) {
                    verdict = "the winner of a duel took no damage";
                }
            }
        }
    }

    // An archer: it closes to its range, looses arrows that fly, and they hurt
    // when they land -- not before.
    if (ok()) {
        int x = 0;
        int y = 0;
        if (!patch(-14, 0, x, y)) {
            verdict = "no open ground for archery";
        } else {
            const entt::entity archer = ECS::Units::Spawn(reg, archer_id, kUs, At(x - 3, y));
            const entt::entity target = ECS::Units::Spawn(reg, villager_id, kThem, At(x + 2, y));
            const int full = reg.get<ECS::Components::Health>(target).current_hp;
            int first_arrow = -1;
            int first_wound = -1;
            for (int t = 0; t < 20 * kTicksPerSecond; ++t) {
                headless.Update(16);
                if (first_arrow < 0) {
                    auto flying = reg.view<ECS::Components::Projectile>();
                    for (auto a : flying) {
                        if (flying.get<ECS::Components::Projectile>(a).shooter == archer) {
                            first_arrow = t;
                        }
                    }
                }
                if (first_wound < 0 && reg.valid(target) &&
                    reg.get<ECS::Components::Health>(target).current_hp < full) {
                    first_wound = t;
                }
                if (first_wound >= 0 && first_arrow >= 0) break;
            }
            // Ordered onto a villager -- villagers are never picked up on
            // sight -- so the order is what starts it; a few seconds in, it
            // has to have been shooting.
            if (first_arrow < 0) {
                Core::CommandQueue::Get().PushCommand(
                    Core::AttackCommand{archer, target, kUs});
                for (int t = 0; t < 20 * kTicksPerSecond && first_wound < 0; ++t) {
                    headless.Update(16);
                    if (first_arrow < 0) {
                        auto flying = reg.view<ECS::Components::Projectile>();
                        for (auto a : flying) {
                            if (flying.get<ECS::Components::Projectile>(a).shooter == archer) {
                                first_arrow = t;
                            }
                        }
                    }
                    if (reg.valid(target) &&
                        reg.get<ECS::Components::Health>(target).current_hp < full) {
                        first_wound = t;
                    }
                }
            }
            if (first_arrow < 0) {
                verdict = "an archer ordered to attack never loosed an arrow";
            } else if (first_wound < 0) {
                verdict = "an archer's arrows never hurt its target";
            } else if (first_wound <= first_arrow) {
                verdict = "an arrow hurt its target before it had flown";
            }
        }
    }

    // A watch tower shoots an enemy that walks into its range, by itself.
    if (ok()) {
        int x = 0;
        int y = 0;
        if (!patch(0, 16, x, y)) {
            verdict = "no open ground for a tower";
        } else {
            const entt::entity tower =
                ECS::Buildings::Place(reg, grid, tower_id, kUs, x, y, true);
            const entt::entity intruder =
                ECS::Units::Spawn(reg, villager_id, kThem, At(x + 5, y));
            const int full = reg.get<ECS::Components::Health>(intruder).current_hp;
            bool shot = false;
            for (int t = 0; t < 15 * kTicksPerSecond; ++t) {
                headless.Update(16);
                auto flying = reg.view<ECS::Components::Projectile>();
                for (auto a : flying) {
                    if (flying.get<ECS::Components::Projectile>(a).shooter == tower) shot = true;
                }
                if (!reg.valid(intruder) ||
                    reg.get<ECS::Components::Health>(intruder).current_hp < full) {
                    break;
                }
            }
            if (tower == entt::null) {
                verdict = "a watch tower could not be placed";
            } else if (!shot) {
                verdict = "a watch tower never shot at an enemy in its range";
            } else if (reg.valid(intruder) &&
                       reg.get<ECS::Components::Health>(intruder).current_hp >= full) {
                verdict = "a watch tower's arrows never hurt anyone";
            }
        }
    }

    // Stand ground: a unit holds its spot. It fights what comes into reach,
    // and when that walks away it lets it go rather than following.
    if (ok()) {
        int x = 0;
        int y = 0;
        if (!patch(0, -16, x, y)) {
            verdict = "no open ground to stand on";
        } else {
            const entt::entity guard = ECS::Units::Spawn(reg, militia_id, kUs, At(x, y));
            reg.get<ECS::Components::Combat>(guard).stance = ECS::Components::Stance::StandGround;
            // Touching: a soldier's blow reaches no further (its combat
            // task's work range is 0).
            const entt::entity passer = ECS::Units::Spawn(
                reg, militia_id, kThem,
                {At(x, y).x + Core::Fixed32(2) / Core::Fixed32(5), At(x, y).y});
            reg.get<ECS::Components::Combat>(passer).stance = ECS::Components::Stance::NoAttack;
            run(kTicksPerSecond);
            const bool engaged =
                reg.get<ECS::Components::Combat>(guard).target == passer;
            const auto start = reg.get<ECS::Components::Transform>(guard).position;
            // The intruder walks off, well out of reach.
            const auto& from = reg.get<ECS::Components::Transform>(passer).position;
            Core::CommandQueue::Get().PushCommand(Core::MoveCommand{
                passer, from.x + Core::Fixed32(5), from.y, kThem});
            run(5 * kTicksPerSecond);
            const auto now = reg.get<ECS::Components::Transform>(guard).position;
            if (!engaged) {
                verdict = "a unit standing its ground did not fight an enemy in reach";
            } else if (Core::FixedMath::Distance(now.x - start.x, now.y - start.y) >
                       Core::Fixed32(1) / Core::Fixed32(2)) {
                verdict = "a unit standing its ground followed an enemy out of reach";
            }
        }
    }

    // A group marching in line: the hand-to-hand fighters arrive in front of
    // the archers, facing the way it marched, and none outpaces the slowest.
    if (ok()) {
        int x = 0;
        int y = 0;
        if (!patch(-16, -16, x, y)) {
            verdict = "no open ground to march on";
        } else {
            std::vector<entt::entity> group;
            std::vector<entt::entity> fighters;
            std::vector<entt::entity> archers;
            for (int i = 0; i < 3; ++i) {
                fighters.push_back(ECS::Units::Spawn(reg, militia_id, kUs, At(x + i, y)));
                archers.push_back(ECS::Units::Spawn(reg, archer_id, kUs, At(x + i, y + 1)));
            }
            for (auto e : fighters) {
                reg.get<ECS::Components::Combat>(e).stance = ECS::Components::Stance::NoAttack;
                group.push_back(e);
            }
            for (auto e : archers) {
                reg.get<ECS::Components::Combat>(e).stance = ECS::Components::Stance::NoAttack;
                group.push_back(e);
            }
            // March eight tiles along +x; "in front" is further along it.
            Core::CommandQueue::Get().PushCommand(Core::FormationMoveCommand{
                group, Core::Fixed32(x + 9), Core::Fixed32(y), Core::Formation::Line, kUs});
            headless.Update(16);
            Core::Fixed32 cap{};
            if (const auto* path = reg.try_get<ECS::Components::Path>(archers.front())) {
                cap = path->speed_cap;
            }
            const auto* militia = ECS::Players::UnitOf(reg, kUs, militia_id);
            const auto* archer = ECS::Players::UnitOf(reg, kUs, archer_id);
            const Core::Fixed32 slowest = militia->speed < archer->speed ? militia->speed
                                                                         : archer->speed;
            run(20 * kTicksPerSecond);
            Core::Fixed32 rearmost_fighter{};
            Core::Fixed32 foremost_archer{};
            bool first = true;
            for (auto e : fighters) {
                const auto x_at = reg.get<ECS::Components::Transform>(e).position.x;
                if (first || x_at < rearmost_fighter) rearmost_fighter = x_at;
                first = false;
            }
            first = true;
            for (auto e : archers) {
                const auto x_at = reg.get<ECS::Components::Transform>(e).position.x;
                if (first || x_at > foremost_archer) foremost_archer = x_at;
                first = false;
            }
            if (cap != slowest) {
                verdict = "a formation does not keep to its slowest member's pace";
            } else if (!(rearmost_fighter > foremost_archer)) {
                verdict = "a line formation did not put its fighters in front of its archers";
            }
        }
    }

    // High ground: a blow struck down a hill does a quarter more. A militia
    // on raised ground hits a militia below for 5, not 4. And the dat's hill
    // rule decides what may be built on uneven ground: a Town Center needs it
    // flat, a Barracks tolerates a level of difference, a House goes anywhere.
    if (ok()) {
        int x = 0;
        int y = 0;
        if (!patch(16, 16, x, y)) {
            verdict = "no open ground for a hill";
        } else if (ECS::Combat::HeightAdjusted(4, 2, 0) != 5 ||
                   ECS::Combat::HeightAdjusted(4, 0, 2) != 3 ||
                   ECS::Combat::HeightAdjusted(4, 1, 1) != 4) {
            verdict = "the high ground bonus is not a quarter either way";
        } else {
            // The map's own hills may be anywhere: level the patch first, then
            // raise a hill of its own on one side of it.
            for (int dy = -4; dy <= 4; ++dy) {
                for (int dx = -4; dx <= 4; ++dx) grid.GetTileMut(x + dx, y + dy).elevation = 0;
            }
            for (int dy = -2; dy <= 2; ++dy) {
                for (int dx = -2; dx <= 0; ++dx) grid.GetTileMut(x + dx, y + dy).elevation = 2;
            }
            const entt::entity high = ECS::Units::Spawn(reg, militia_id, kUs, At(x - 1, y));
            const entt::entity low = ECS::Units::Spawn(
                reg, militia_id, kThem,
                {At(x - 1, y).x + Core::Fixed32(6) / Core::Fixed32(10), At(x - 1, y).y});
            reg.get<ECS::Components::Combat>(low).stance = ECS::Components::Stance::NoAttack;
            // Stood on the edge of the hill and at its foot, already within
            // reach of each other, so neither has to step off its ground.
            reg.get<ECS::Components::Transform>(high).position.x =
                Core::Fixed32(x) + Core::Fixed32(3) / Core::Fixed32(10);
            reg.get<ECS::Components::Transform>(low).position.x =
                Core::Fixed32(x) + Core::Fixed32(85) / Core::Fixed32(100);
            Core::CommandQueue::Get().PushCommand(Core::AttackCommand{high, low, kUs});
            const int full = reg.get<ECS::Components::Health>(low).current_hp;
            int first_wound = 0;
            for (int t = 0; t < 10 * kTicksPerSecond && first_wound == 0; ++t) {
                headless.Update(16);
                if (!reg.valid(low)) break;
                first_wound = full - reg.get<ECS::Components::Health>(low).current_hp;
            }
            const int tc_button = cfg.GetUnitIdByName("RTWC1X");
            const int house = cfg.GetUnitIdByName("HOUS");
            const int barracks = cfg.GetUnitIdByName("BRKS");
            if (ECS::Combat::LevelOf(reg, grid, high) <= ECS::Combat::LevelOf(reg, grid, low)) {
                verdict = "the fight on the hill was not fought across the slope";
            } else if (first_wound != 5) {
                verdict = "a blow struck down a hill did not do a quarter more";
            } else {
                // A footprint with one tile raised a level.
                int bx = 0;
                int by = 0;
                if (!patch(-16, 16, bx, by)) {
                    verdict = "no open ground to test building on a slope";
                } else {
                    for (int dy = -3; dy <= 3; ++dy) {
                        for (int dx = -3; dx <= 3; ++dx) {
                            grid.GetTileMut(bx + dx, by + dy).elevation = 0;
                        }
                    }
                    grid.GetTileMut(bx, by).elevation = 1;
                    const bool tc_ok = ECS::Buildings::CanPlace(grid, tc_button, bx, by);
                    const bool barracks_ok = ECS::Buildings::CanPlace(grid, barracks, bx, by);
                    grid.GetTileMut(bx, by).elevation = 2;
                    const bool barracks_steep = ECS::Buildings::CanPlace(grid, barracks, bx, by);
                    const bool house_steep = ECS::Buildings::CanPlace(grid, house, bx, by);
                    grid.GetTileMut(bx, by).elevation = 0;
                    if (tc_ok) {
                        verdict = "a town center could be placed on uneven ground";
                    } else if (!barracks_ok) {
                        verdict = "a barracks could not be placed across one level";
                    } else if (!barracks_steep) {
                        verdict = "a barracks was refused for its middle tile, which the hill rule does not look at";
                    } else if (!house_steep) {
                        verdict = "a house, which goes anywhere, was refused on a slope";
                    }
                }
            }
        }
    }

    // Hunting: a villager sent at a deer becomes a hunter, kills it with its
    // spear, and gathers the carcass it leaves.
    if (ok()) {
        const entt::entity deer = Nearest(reg, deer_id, base);
        if (deer == entt::null) {
            verdict = "no deer on the map to hunt";
        } else {
            const auto spot = reg.get<ECS::Components::Transform>(deer).position;
            const entt::entity hunter = ECS::Units::Spawn(
                reg, villager_id, kUs,
                BesidePrey(grid, spot));
            Core::CommandQueue::Get().PushCommand(Core::GatherCommand{hunter, deer, kUs});
            bool carried = false;
            for (int t = 0; t < 60 * kTicksPerSecond && !carried; ++t) {
                headless.Update(16);
                if (!reg.valid(hunter)) break;
                const auto& w = reg.get<ECS::Components::Worker>(hunter);
                if (w.carried[static_cast<int>(Core::ResourceType::Food)] > 0) carried = true;
            }
            if (!reg.valid(hunter)) {
                verdict = "the hunter did not survive its hunt";
            } else if (reg.get<ECS::Components::Worker>(hunter).job !=
                       ECS::Components::VillagerJob::Hunt) {
                verdict = "a villager sent at a deer did not become a hunter";
            } else if (ECS::Combat::Alive(reg, deer)) {
                verdict = "a hunter never killed its deer";
            } else if (!carried) {
                verdict = "a hunter never gathered from the carcass";
            }
        }
    }

    // A boar answers its hunter.
    if (ok()) {
        const entt::entity boar = Nearest(reg, boar_id, base);
        if (boar == entt::null) {
            verdict = "no boar on the map";
        } else {
            const auto spot = reg.get<ECS::Components::Transform>(boar).position;
            const entt::entity hunter = ECS::Units::Spawn(
                reg, villager_id, kUs,
                BesidePrey(grid, spot));
            Core::CommandQueue::Get().PushCommand(Core::GatherCommand{hunter, boar, kUs});
            bool answered = false;
            for (int t = 0; t < 20 * kTicksPerSecond && !answered; ++t) {
                headless.Update(16);
                if (!reg.valid(boar) || !reg.all_of<ECS::Components::Combat>(boar)) break;
                if (reg.get<ECS::Components::Combat>(boar).target == hunter) answered = true;
            }
            if (!answered) verdict = "a boar did not turn on the villager hunting it";
        }
    }

    // A mangonel's stone: a blast that takes everyone where it lands, its own
    // side's units too, while an archer's arrow -- the dat's target-only blast
    // level -- takes only what it hits.
    if (ok()) {
        int x = 0;
        int y = 0;
        const int mangonel_id = cfg.GetUnitIdByName("MANGO");
        if (mangonel_id < 0 || !patch(-14, 14, x, y)) {
            verdict = "no mangonel, or no open ground for one";
        } else {
            const entt::entity mangonel = ECS::Units::Spawn(reg, mangonel_id, kUs, At(x - 6, y));
            auto still = [&](entt::entity e) {
                reg.get<ECS::Components::Combat>(e).stance = ECS::Components::Stance::NoAttack;
                return e;
            };
            const entt::entity aimed = still(ECS::Units::Spawn(reg, militia_id, kThem, At(x, y)));
            const ECS::Components::Vector2D at = reg.get<ECS::Components::Transform>(aimed).position;
            const Core::Fixed32 half = Core::Fixed32(1) / Core::Fixed32(2);
            const entt::entity beside = still(ECS::Units::Spawn(reg, militia_id, kThem,
                                                                {at.x, at.y + half}));
            const entt::entity friend_ = still(ECS::Units::Spawn(reg, militia_id, kUs,
                                                                 {at.x, at.y - half}));
            const int full = reg.get<ECS::Components::Health>(beside).max_hp;
            // Archers would be quicker to their own targets than the stone.
            ECS::Combat::Engage(reg, mangonel, aimed, true);
            bool landed = false;
            for (int t = 0; t < 15 * kTicksPerSecond && !landed; ++t) {
                headless.Update(16);
                landed = !ECS::Combat::Alive(reg, aimed) ||
                         reg.get<ECS::Components::Health>(aimed).current_hp < full;
            }
            if (!landed) {
                verdict = "a mangonel never hit what it was aimed at";
            } else if (reg.get<ECS::Components::Health>(beside).current_hp >= full &&
                       ECS::Combat::Alive(reg, beside)) {
                verdict = "a mangonel's blast spared the enemy beside its target";
            } else if (reg.valid(friend_) && ECS::Combat::Alive(reg, friend_) &&
                       reg.get<ECS::Components::Health>(friend_).current_hp >= full) {
                verdict = "a mangonel's blast spared its own side";
            }
        }
    }

    std::printf("combat=%s\n", verdict);
    return ok();
}

} // namespace Verify
