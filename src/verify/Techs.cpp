#include "verify/Harness.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <vector>

#include "core/ConfigManager.h"
#include "core/Rules.h"
#include "ecs/Buildings.h"
#include "ecs/Players.h"
#include "ecs/Production.h"
#include "ecs/Simulation.h"
#include "ecs/Techs.h"
#include "ecs/components/Building.h"
#include "ecs/components/Health.h"
#include "ecs/components/Identity.h"
#include "ecs/components/Owner.h"
#include "ecs/components/PartOf.h"
#include "ecs/components/ProductionQueue.h"
#include "ecs/components/Sprite.h"
#include "ecs/components/Transform.h"
#include "ecs/components/Worker.h"

namespace Verify {

namespace {

constexpr Core::PlayerId kTeutons = 1;   // the default lobby's first seat
constexpr Core::PlayerId kFranks = 2;

// The dat's own ids for what this run exercises.
constexpr int kDarkAge = 104;
constexpr int kFeudalAge = 101;
constexpr int kLoom = 22;
constexpr int kWheelbarrow = 213;
constexpr int kNodeForAgeTwo = 120;

int PaidIn(const Core::Rules::Cost* costs, int count, int dat_resource) {
    int amount = 0;
    for (int i = 0; i < count; ++i) {
        if (costs[i].paid && costs[i].resource == dat_resource) amount += costs[i].amount;
    }
    return amount;
}

entt::entity TownCenterOf(entt::registry& reg, Core::PlayerId player, int tc_id) {
    auto view = reg.view<ECS::Components::Building, ECS::Components::Identity,
                         ECS::Components::Owner>();
    for (auto e : view) {
        if (view.get<ECS::Components::Identity>(e).unit_id == tc_id &&
            view.get<ECS::Components::Owner>(e).player_id == player) {
            return e;
        }
    }
    return entt::null;
}

// The graphics a building is drawn with: its own sprite and every piece.
std::vector<int> ArtOfEntity(const entt::registry& reg, entt::entity building) {
    std::vector<int> slps;
    if (const auto* own = reg.try_get<ECS::Components::Sprite>(building)) {
        slps.push_back(own->slp_id);
    }
    auto parts = reg.view<const ECS::Components::PartOf, const ECS::Components::Sprite>();
    for (auto part : parts) {
        if (parts.get<const ECS::Components::PartOf>(part).building == building) {
            slps.push_back(parts.get<const ECS::Components::Sprite>(part).slp_id);
        }
    }
    return slps;
}

} // namespace

// Technologies, from the dat alone, start to finish: civ bonuses and the tech
// tree at the start, an age waiting on its buildings, research paid and timed,
// upgrades in place in both numbers and art, and effects reaching the units
// already in the field. Each is looked at directly.
bool CheckTechs() {
    ECS::Simulation headless;
    headless.Init();
    auto& reg = headless.GetRegistry();
    auto& grid = headless.GetGrid();
    auto& cfg = Core::ConfigManager::Get();
    const char* verdict = "ok";

    const int tc_id = cfg.GetUnitIdByName("RTWC");
    const int farm_id = cfg.GetUnitIdByName("FARM");
    const int castle_id = cfg.GetUnitIdByName("CSTL");
    const int barracks_id = cfg.GetUnitIdByName("BRKS");
    const int mill_id = cfg.GetUnitIdByName("MILL");
    const int lumber_id = cfg.GetUnitIdByName("SMIL");
    const int house_id = cfg.GetUnitIdByName("HOUS");
    const int villager_id = cfg.GetUnitIdByName("VMBAS");
    ECS::Players::Player* teutons = ECS::Players::Find(reg, kTeutons);
    ECS::Players::Player* franks = ECS::Players::Find(reg, kFranks);
    const entt::entity tc = TownCenterOf(reg, kTeutons, tc_id);

    if (teutons == nullptr || franks == nullptr || tc == entt::null) {
        verdict = "the default game has no Teuton and Frankish players";
    }

    // The start: the Dark Age researched and in force; each civ's own bonuses
    // applied to it and not to the other; the tech tree's prohibitions made.
    if (std::strcmp(verdict, "ok") == 0) {
        const auto& teuton_farm = teutons->units[farm_id];
        const auto& frank_farm = franks->units[farm_id];
        const auto& teuton_castle = teutons->units[castle_id];
        const auto& frank_castle = franks->units[castle_id];
        if (!ECS::Techs::Researched(reg, kTeutons, kDarkAge)) {
            verdict = "the Dark Age was not researched at the start";
        } else if (ECS::Techs::Age(reg, kTeutons) != 0) {
            verdict = "the game did not start in the Dark Age";
        } else if (PaidIn(teuton_farm.costs, teuton_farm.cost_count, Core::Rules::Res::Wood) >=
                   PaidIn(frank_farm.costs, frank_farm.cost_count, Core::Rules::Res::Wood)) {
            verdict = "the Teutons' cheaper farms did not apply";
        } else if (PaidIn(frank_castle.costs, frank_castle.cost_count, Core::Rules::Res::Stone) >=
                   PaidIn(teuton_castle.costs, teuton_castle.cost_count, Core::Rules::Res::Stone)) {
            verdict = "the Franks' cheaper castles did not apply";
        } else if (!teutons->units[barracks_id].enabled) {
            verdict = "the Barracks was not made available in the Dark Age";
        } else {
            // Another civilisation's bonus is never this one's.
            for (int id = 0; id < static_cast<int>(teutons->techs.size()); ++id) {
                const auto* tech = Core::Rules::Tech(id);
                if (tech == nullptr || tech->civ < 0 || tech->civ == teutons->civ) continue;
                if (teutons->techs[id].state == ECS::Players::TechState::Done) {
                    verdict = "a Teuton player received another civ's bonus";
                    break;
                }
            }
        }
    }

    // The tech tree: every technology disabled by the effect the dat names for
    // the civ is disabled. Read from the effect itself, since a civ bonus also
    // disables a few technologies of its own.
    if (std::strcmp(verdict, "ok") == 0) {
        const auto& civs = Core::Rules::Get().civs;
        const int civ = teutons->civ;
        const int tree = civ >= 0 && civ < static_cast<int>(civs.size()) ? civs[civ].tech_tree : -1;
        const auto& effects = Core::Rules::Get().effects;
        int listed = 0;
        int missed = 0;
        if (tree >= 0 && tree < static_cast<int>(effects.size())) {
            for (const auto& c : effects[tree].commands) {
                if (c.type != Core::Rules::EffectType::DisableTech) continue;
                if (c.d_int < 0 || c.d_int >= static_cast<int>(teutons->techs.size())) continue;
                ++listed;
                if (teutons->techs[c.d_int].state != ECS::Players::TechState::Disabled) ++missed;
            }
        }
        if (listed == 0) verdict = "the Teuton tech tree disables nothing in the data";
        else if (missed > 0) verdict = "a technology the Teuton tech tree forbids was not disabled";
    }

    // The Feudal Age waits for two Dark Age buildings.
    if (std::strcmp(verdict, "ok") == 0) {
        const auto& at = reg.get<ECS::Components::Transform>(tc).position;
        if (ECS::Techs::Available(reg, kTeutons, kFeudalAge)) {
            verdict = "the Feudal Age was available with no buildings";
        } else {
            ECS::Buildings::Place(reg, grid, mill_id, kTeutons,
                                  at.x.ToInt() + 7, at.y.ToInt() - 7, true);
            if (ECS::Techs::Available(reg, kTeutons, kFeudalAge)) {
                verdict = "the Feudal Age was available after one building";
            } else {
                ECS::Buildings::Place(reg, grid, lumber_id, kTeutons,
                                      at.x.ToInt() - 7, at.y.ToInt() - 7, true);
                if (!ECS::Techs::Researched(reg, kTeutons, kNodeForAgeTwo)) {
                    verdict = "two Dark Age buildings did not satisfy the age node";
                } else if (!ECS::Techs::Available(reg, kTeutons, kFeudalAge)) {
                    verdict = "the Feudal Age was not available after two buildings";
                }
            }
        }
    }

    // Researching it: paid at once, at the player's price; refused a second
    // time while under way; done after its research time, with every building
    // upgraded in place -- art and all -- and its identity unchanged.
    if (std::strcmp(verdict, "ok") == 0) {
        ECS::Players::AddStock(*teutons, Core::ResourceType::Food, 2000);
        ECS::Players::AddStock(*teutons, Core::ResourceType::Gold, 2000);
        const int food = ECS::Players::Stock(*teutons, Core::ResourceType::Food);
        const auto& status = teutons->techs[kFeudalAge];
        const int price = PaidIn(status.costs, status.cost_count, Core::Rules::Res::Food);
        const std::vector<int> dark_art = ArtOfEntity(reg, tc);
        using ECS::Production::Refusal;
        if (ECS::Production::EnqueueTech(reg, tc, kFeudalAge, kTeutons) != Refusal::None) {
            verdict = "the town centre refused the Feudal Age";
        } else if (food - ECS::Players::Stock(*teutons, Core::ResourceType::Food) != price) {
            verdict = "the Feudal Age was not charged at its price";
        } else if (ECS::Production::EnqueueTech(reg, tc, kFeudalAge, kTeutons) !=
                   Refusal::Unavailable) {
            verdict = "the Feudal Age could be queued twice";
        } else {
            const int ticks = status.time * 1000 / 16 + 10;
            for (int t = 0; t < ticks; ++t) headless.Update(16);
            const std::vector<int> feudal_art = ArtOfEntity(reg, tc);
            if (ECS::Techs::Age(reg, kTeutons) != 1) {
                verdict = "researching the Feudal Age did not advance the age";
            } else if (ECS::Players::ArtOf(reg, kTeutons, tc_id) == tc_id) {
                verdict = "the age did not upgrade the town centre's slot";
            } else if (reg.get<ECS::Components::Identity>(tc).unit_id != tc_id) {
                verdict = "the upgrade changed the town centre's identity";
            } else if (feudal_art == dark_art) {
                verdict = "the town centre still wears its Dark Age art";
            } else if ([&] {
                           // Every piece of it the age's own: the same art a
                           // Town Center built at this point has. An age
                           // upgrades the Town Center and its annexes
                           // separately.
                           for (int r = 6; r < 30; ++r) {
                               for (int d = -r; d <= r; ++d) {
                                   const auto& at = reg.get<ECS::Components::Transform>(tc).position;
                                   const int x = Physics::TileOf(at.x) + d, y = Physics::TileOf(at.y) + r;
                                   if (!ECS::Buildings::CanPlace(grid, tc_id, x, y)) continue;
                                   const entt::entity fresh =
                                       ECS::Buildings::Place(reg, grid, tc_id, kTeutons, x, y, true);
                                   std::vector<int> theirs = ArtOfEntity(reg, fresh);
                                   std::vector<int> ours = feudal_art;
                                   std::sort(theirs.begin(), theirs.end());
                                   std::sort(ours.begin(), ours.end());
                                   ECS::Buildings::Destroy(reg, grid, fresh, false);
                                   return theirs != ours;
                               }
                           }
                           return true;
                       }()) {
                verdict = "the town centre's art after the age is not the age's own";
            } else if (ECS::Players::ArtOf(reg, kTeutons, house_id) == house_id) {
                verdict = "the age did not upgrade the house";
            } else if (ECS::Players::ArtOf(reg, kFranks, tc_id) != tc_id) {
                verdict = "one player's age advance upgraded another's buildings";
            }
        }
    }

    // A technology changes the units already in the field, not just new ones:
    // Loom gives every villager its hit points at once.
    if (std::strcmp(verdict, "ok") == 0) {
        const int before = teutons->units[villager_id].hp;
        std::vector<entt::entity> villagers;
        auto view = reg.view<ECS::Components::Worker, ECS::Components::Owner,
                             ECS::Components::Health>();
        for (auto e : view) {
            if (view.get<ECS::Components::Owner>(e).player_id == kTeutons) villagers.push_back(e);
        }
        if (ECS::Production::EnqueueTech(reg, tc, kLoom, kTeutons) !=
            ECS::Production::Refusal::None) {
            verdict = "the town centre refused Loom";
        } else {
            const int ticks = teutons->techs[kLoom].time * 1000 / 16 + 10;
            for (int t = 0; t < ticks; ++t) headless.Update(16);
            const int after = teutons->units[villager_id].hp;
            if (!ECS::Techs::Researched(reg, kTeutons, kLoom)) {
                verdict = "Loom never finished";
            } else if (after <= before) {
                verdict = "Loom did not raise the villager's hit points";
            } else if (villagers.empty()) {
                verdict = "no villagers to check Loom against";
            } else {
                for (auto e : villagers) {
                    if (!reg.valid(e)) continue;
                    if (reg.get<ECS::Components::Health>(e).max_hp != after) {
                        verdict = "a villager in the field did not get Loom's hit points";
                        break;
                    }
                }
            }
        }
    }

    // Cancelling research refunds it and makes it available again.
    if (std::strcmp(verdict, "ok") == 0) {
        const int food = ECS::Players::Stock(*teutons, Core::ResourceType::Food);
        if (ECS::Production::EnqueueTech(reg, tc, kWheelbarrow, kTeutons) !=
            ECS::Production::Refusal::None) {
            verdict = "Wheelbarrow was not available in the Feudal Age";
        } else if (!ECS::Production::Cancel(reg, tc, 0, kTeutons)) {
            verdict = "queued research could not be cancelled";
        } else if (ECS::Players::Stock(*teutons, Core::ResourceType::Food) != food) {
            verdict = "cancelled research was not refunded";
        } else if (!ECS::Techs::Available(reg, kTeutons, kWheelbarrow)) {
            verdict = "cancelled research did not become available again";
        }
    }

    std::printf("techs=%s\n", verdict);
    return std::strcmp(verdict, "ok") == 0;
}

} // namespace Verify
