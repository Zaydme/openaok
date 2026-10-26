#include "verify/Harness.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include "core/ConfigManager.h"
#include "core/Command.h"
#include "core/CommandQueue.h"
#include "core/Rules.h"
#include "ecs/Events.h"
#include "ecs/BuildingGeometry.h"
#include "ecs/Buildings.h"
#include "ecs/Players.h"
#include "ecs/Population.h"
#include "ecs/Production.h"
#include "ecs/Simulation.h"
#include "ecs/Units.h"
#include "ecs/components/Animation.h"
#include "ecs/components/Building.h"
#include "ecs/components/Health.h"
#include "ecs/components/Identity.h"
#include "ecs/components/Owner.h"
#include "ecs/components/ProductionQueue.h"
#include "ecs/components/Resource.h"
#include "ecs/components/Selectable.h"
#include "ecs/components/Transform.h"
#include "ecs/components/Worker.h"

namespace Verify {

namespace {

constexpr Core::PlayerId kPlayer = 1;
constexpr Core::ResourceType kFood = Core::ResourceType::Food;

// The paid amount of one resource in a price.
int PriceIn(const Core::Rules::UnitRules& unit, int dat_resource) {
    int amount = 0;
    for (int i = 0; i < unit.cost_count; ++i) {
        if (unit.costs[i].paid && unit.costs[i].resource == dat_resource) {
            amount += unit.costs[i].amount;
        }
    }
    return amount;
}

// Units the player owns that count toward its population: everything it has
// that is not a building.
int OwnedUnits(const entt::registry& registry) {
    int count = 0;
    auto view = registry.view<const ECS::Components::Owner,
                              const ECS::Components::Identity>();
    for (auto e : view) {
        if (view.get<const ECS::Components::Owner>(e).player_id != kPlayer) continue;
        if (registry.all_of<ECS::Components::Building>(e)) continue;
        ++count;
    }
    return count;
}

// The villagers in `now` that were not in `then`. Picked out by membership,
// never by position in a view: EnTT walks its storage back to front, so "the
// last one" of a view is the oldest entity, not the newest.
std::vector<entt::entity> Arrived(const std::vector<entt::entity>& then,
                                  const std::vector<entt::entity>& now) {
    std::vector<entt::entity> out;
    for (entt::entity e : now) {
        bool seen = false;
        for (entt::entity old : then) {
            if (old == e) { seen = true; break; }
        }
        if (!seen) out.push_back(e);
    }
    return out;
}

// The player's villagers, in no particular order.
std::vector<entt::entity> VillagersOf(const entt::registry& registry) {
    std::vector<entt::entity> out;
    auto view = registry.view<const ECS::Components::Worker,
                              const ECS::Components::Owner>();
    for (auto e : view) {
        if (view.get<const ECS::Components::Owner>(e).player_id == kPlayer) {
            out.push_back(e);
        }
    }
    return out;
}

bool OnLattice(float v) {
    const float r = v - std::floor(v);
    return r < 0.02f || r > 0.98f || std::fabs(r - 0.5f) < 0.02f;
}

} // namespace

// A Town Centre trains villagers, start to end: the price is the dat's and is
// paid at queue time; a building refuses what it does not train, what its
// owner cannot pay for and orders from anyone else; a finished villager steps
// out beside the building fully formed; production stops at the population
// limit and resumes the moment a House stands; and cancelling refunds the full
// price.
bool CheckProduction() {
    ECS::Simulation headless;
    headless.Init();
    auto& reg = headless.GetRegistry();
    auto& cfg = Core::ConfigManager::Get();
    ECS::Players::Player* me = ECS::Players::Find(reg, kPlayer);

    const char* verdict = "ok";

    const int town_centre_id = cfg.GetUnitIdByName("RTWC");
    const int villager_id = cfg.GetUnitIdByName("VMBAS");
    const int house_id = cfg.GetUnitIdByName("HOUS");
    const Core::Rules::UnitRules* villager = ECS::Players::UnitOf(reg, kPlayer, villager_id);
    // A run's worth of villagers is more food than a player starts with.
    if (me != nullptr) ECS::Players::AddStock(*me, kFood, 2000);

    entt::entity tc = entt::null;
    auto buildings = reg.view<ECS::Components::Building, ECS::Components::Identity,
                              ECS::Components::Owner>();
    for (auto b : buildings) {
        if (buildings.get<ECS::Components::Identity>(b).unit_id == town_centre_id &&
            buildings.get<ECS::Components::Owner>(b).player_id == kPlayer) {
            tc = b;
            break;
        }
    }

    // Where each trained villager stood on the tick it appeared: units created
    // on top of one another are nudged apart by the movement system over the
    // following seconds.
    std::vector<entt::entity> known = VillagersOf(reg);
    std::vector<ECS::Components::Vector2D> births;
    // Every birth must be news too: the sound of a unit trained, and anything
    // else watching, hear of it only through the event log.
    int unannounced = 0;
    auto announced = [&](entt::entity unit) {
        const ECS::Events::Log* log = ECS::Events::Read(reg);
        if (log == nullptr) return false;
        for (const auto& event : log->events) {
            if (event.kind == ECS::Events::Kind::Born && event.subject == unit &&
                event.tick == log->tick) {
                return true;
            }
        }
        return false;
    };
    auto run = [&](int ticks) {
        for (int t = 0; t < ticks; ++t) {
            headless.Update(16);
            const std::vector<entt::entity> now = VillagersOf(reg);
            for (entt::entity fresh : Arrived(known, now)) {
                births.push_back(reg.get<ECS::Components::Transform>(fresh).position);
                if (!announced(fresh)) ++unannounced;
            }
            known = now;
        }
    };
    // The dat's train time, plus slack for the step out of the door.
    const int train_ticks = villager ? villager->train_time * 1000 / 16 + 30 : 0;

    if (tc == entt::null || villager == nullptr || house_id < 0 || me == nullptr) {
        verdict = "no town centre, villager or house in the data";
    }

    // The starting position: the Town Centre's five, and one place taken by
    // each unit the player starts with -- its villagers and its scout, each
    // booked by its own storage rather than counted here.
    const int start_used = OwnedUnits(reg);
    if (std::strcmp(verdict, "ok") == 0) {
        if (ECS::Population::Capacity(reg, kPlayer) != 5) {
            verdict = "a town centre does not house five";
        } else if (ECS::Population::Used(reg, kPlayer) != start_used) {
            verdict = "the starting population does not match the starting units";
        } else if (start_used != 4) {
            verdict = "the player did not start with three villagers and a scout";
        }
    }

    // Refusals, none of which may charge anything or touch the queue.
    if (std::strcmp(verdict, "ok") == 0) {
        const int food = ECS::Players::Stock(*me, kFood);
        using ECS::Production::Refusal;
        if (ECS::Production::Enqueue(reg, tc, house_id, kPlayer) !=
            Refusal::NotProducedHere) {
            verdict = "a town centre accepted a unit it does not train";
        } else if (ECS::Production::Enqueue(reg, tc, villager_id, 2) !=
                   Refusal::NotOwned) {
            verdict = "a town centre took an order from another player";
        } else {
            ECS::Players::AddStock(*me, kFood, -food);
            const Refusal broke = ECS::Production::Enqueue(reg, tc, villager_id, kPlayer);
            ECS::Players::AddStock(*me, kFood, food);
            if (broke != Refusal::CannotAfford) {
                verdict = "a villager was queued with no food to pay for it";
            }
        }
        if (std::strcmp(verdict, "ok") == 0 && ECS::Players::Stock(*me, kFood) != food) {
            verdict = "a refused order still charged the player";
        }
    }

    // One villager: paid for at once, at the dat's price.
    std::vector<entt::entity> before;
    if (std::strcmp(verdict, "ok") == 0) {
        before = VillagersOf(reg);
        const int food = ECS::Players::Stock(*me, kFood);
        if (ECS::Production::Enqueue(reg, tc, villager_id, kPlayer) !=
            ECS::Production::Refusal::None) {
            verdict = "a town centre refused a villager it can afford";
        } else if (food - ECS::Players::Stock(*me, kFood) !=
                   PriceIn(*villager, Core::Rules::Res::Food)) {
            verdict = "queuing a villager did not charge its dat price";
        }
    }

    // It steps out beside the building, fully formed, after its train time.
    if (std::strcmp(verdict, "ok") == 0) {
        run(train_ticks);
        const std::vector<entt::entity> after = VillagersOf(reg);
        const auto& queue = reg.get<ECS::Components::ProductionQueue>(tc);
        if (after.size() != before.size() + 1) {
            verdict = "no villager came out of the town centre";
        } else if (queue.count != 0) {
            verdict = "the trained villager stayed in the queue";
        } else if (ECS::Population::Used(reg, kPlayer) != start_used + 1) {
            verdict = "the trained villager is not counted in the population";
        } else {
            const entt::entity fresh = Arrived(before, after).front();
            const auto& at = reg.get<ECS::Components::Transform>(fresh).position;
            const auto ground = ECS::Buildings::FootprintBounds(
                reg.get<ECS::Components::Transform>(tc).position, town_centre_id);
            const float gap = ground.DistanceFrom(at).ToFloat();
            const auto* anim = reg.try_get<ECS::Components::Animation>(fresh);
            const auto* hp = reg.try_get<ECS::Components::Health>(fresh);
            if (gap <= 0.0f) {
                verdict = "a villager was made inside the town centre";
            } else if (gap > 1.5f) {
                verdict = "a villager was made away from the town centre";
            } else if (OnLattice(at.x.ToFloat()) && OnLattice(at.y.ToFloat())) {
                verdict = "a villager was made on the grid";
            // A villager missing any of these walks about but cannot be
            // selected, cannot change job, or cannot be hurt -- each named
            // separately, so a failure says which.
            } else if (anim == nullptr ||
                       anim->job != ECS::Components::VillagerJob::Base) {
                verdict = "a trained villager has no villager graphic set";
            } else if (hp == nullptr || hp->max_hp != villager->hp) {
                verdict = "a trained villager does not have the dat's hit points";
            } else if (!reg.all_of<ECS::Components::Selectable,
                                   ECS::Components::Identity>(fresh)) {
                verdict = "a trained villager cannot be selected";
            }
        }
    }

    // Enough to fill the room left, and one more: the last one waits.
    const int to_fill = 5 - (start_used + 1);
    if (std::strcmp(verdict, "ok") == 0) {
        for (int i = 0; i < to_fill + 1; ++i) {
            ECS::Production::Enqueue(reg, tc, villager_id, kPlayer);
        }
        run(train_ticks * (to_fill + 1));
        const auto& queue = reg.get<ECS::Components::ProductionQueue>(tc);
        if (ECS::Population::Used(reg, kPlayer) != 5) {
            verdict = "production did not fill the population to its limit";
        } else if (queue.count != 1 || !queue.housed) {
            verdict = "production went past the population limit";
        } else if (VillagersOf(reg).size() != before.size() + 1 + to_fill) {
            verdict = "more villagers exist than there is room for";
        }
    }

    // A House goes up, and the waiting villager starts at once. Placed and
    // finished through the buildings module, so its five headroom comes from
    // its own storage when it is complete.
    if (std::strcmp(verdict, "ok") == 0) {
        const auto& tc_at = reg.get<ECS::Components::Transform>(tc).position;
        const entt::entity house = ECS::Buildings::Place(
            reg, headless.GetGrid(), house_id, kPlayer,
            tc_at.x.ToInt() + 8, tc_at.y.ToInt() - 8, false);
        // A foundation houses nobody: a House counts only once it is up.
        const int while_building = ECS::Population::Capacity(reg, kPlayer);
        ECS::Buildings::Complete(reg, headless.GetGrid(), house);
        if (house == entt::null) {
            verdict = "a house could not be placed";
        } else if (while_building != 5) {
            verdict = "a house still being built already adds room";
        } else if (ECS::Population::Capacity(reg, kPlayer) != 10) {
            verdict = "a finished house does not add five";
        } else {
            run(train_ticks);
            const auto& queue = reg.get<ECS::Components::ProductionQueue>(tc);
            if (queue.count != 0 || queue.housed) {
                verdict = "production did not resume once there was room";
            } else if (ECS::Population::Used(reg, kPlayer) != 6) {
                verdict = "the housed villager never came out";
            } else {
                // Two more into the new room, so enough villagers have come
                // out one after another to compare where each appeared.
                ECS::Production::Enqueue(reg, tc, villager_id, kPlayer);
                ECS::Production::Enqueue(reg, tc, villager_id, kPlayer);
                run(train_ticks * 2);
                if (ECS::Population::Used(reg, kPlayer) != 8) {
                    verdict = "villagers queued into new room did not come out";
                }
            }
        }
    }

    if (std::strcmp(verdict, "ok") == 0 && unannounced > 0) {
        verdict = "a villager was trained without the event log hearing of it";
    }

    // Villagers made one after another appear side by side, not on each other.
    if (std::strcmp(verdict, "ok") == 0) {
        if (births.size() < 3) {
            verdict = "too few villagers were trained to compare where they appeared";
        }
        for (size_t i = 0; i < births.size() && std::strcmp(verdict, "ok") == 0; ++i) {
            for (size_t j = i + 1; j < births.size(); ++j) {
                const float dx = births[i].x.ToFloat() - births[j].x.ToFloat();
                const float dy = births[i].y.ToFloat() - births[j].y.ToFloat();
                if (dx * dx + dy * dy < 0.4f * 0.4f) {
                    verdict = "trained villagers were made on top of each other";
                    break;
                }
            }
        }
    }

    // A rally point: the next unit out of the door walks to it, and a villager
    // rallied onto a resource goes to work on it.
    if (std::strcmp(verdict, "ok") == 0) {
        const auto& tc_at = reg.get<ECS::Components::Transform>(tc).position;
        entt::entity tree = entt::null;
        float nearest = 1e9f;
        auto stock = reg.view<ECS::Components::Resource, ECS::Components::Transform>();
        for (auto item : stock) {
            if (reg.all_of<ECS::Components::Building>(item)) continue;
            const auto& rt = stock.get<ECS::Components::Transform>(item).position;
            const float dx = rt.x.ToFloat() - tc_at.x.ToFloat();
            const float dy = rt.y.ToFloat() - tc_at.y.ToFloat();
            if (dx * dx + dy * dy < nearest) { nearest = dx * dx + dy * dy; tree = item; }
        }
        // Room for two more first: a second house.
        const entt::entity more = ECS::Buildings::Place(
            reg, headless.GetGrid(), house_id, kPlayer,
            tc_at.x.ToInt() - 8, tc_at.y.ToInt() + 8, true);
        const auto& tree_at = reg.get<ECS::Components::Transform>(tree).position;
        Core::CommandQueue::Get().PushCommand(Core::RallyCommand{
            tc, tree_at.x, tree_at.y, tree, kPlayer});
        const std::vector<entt::entity> then = VillagersOf(reg);
        ECS::Production::Enqueue(reg, tc, villager_id, kPlayer);
        run(train_ticks);
        const std::vector<entt::entity> fresh = Arrived(then, VillagersOf(reg));
        if (more == entt::null || tree == entt::null) {
            verdict = "no house or resource for the rally check";
        } else if (fresh.size() != 1) {
            verdict = "the rallied villager never came out";
        } else {
            const auto& worker = reg.get<ECS::Components::Worker>(fresh.front());
            if (worker.target_resource != tree) {
                verdict = "a villager rallied onto a resource did not go to work on it";
            }
        }
    }

    // Cancelling gives the whole price back, and the population slot with it.
    if (std::strcmp(verdict, "ok") == 0) {
        const int food = ECS::Players::Stock(*me, kFood);
        const int used = ECS::Population::Used(reg, kPlayer);
        ECS::Production::Enqueue(reg, tc, villager_id, kPlayer);
        run(10);   // long enough to have started training
        if (!reg.get<ECS::Components::ProductionQueue>(tc).training) {
            verdict = "a villager with room to start did not start";
        } else if (ECS::Population::Used(reg, kPlayer) != used + 1) {
            // Booked from the moment it starts, so a second building cannot
            // start a unit into the same last free slot on the next tick.
            verdict = "a villager in training is not counted in the population";
        } else if (!ECS::Production::Cancel(reg, tc, 0, kPlayer)) {
            verdict = "a queued villager could not be cancelled";
        } else if (ECS::Players::Stock(*me, kFood) != food) {
            verdict = "cancelling did not refund the full price";
        } else if (ECS::Population::Used(reg, kPlayer) != used) {
            verdict = "cancelling did not give the population slot back";
        }
    }

    std::printf("production=%s\n", verdict);
    return std::strcmp(verdict, "ok") == 0;
}

} // namespace Verify
