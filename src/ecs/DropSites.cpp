#include "ecs/DropSites.h"

#include <algorithm>

#include "core/ConfigManager.h"
#include "ecs/Score.h"
#include "ecs/components/Building.h"
#include "ecs/components/Identity.h"
#include "ecs/components/Owner.h"
#include "ecs/VillagerJobs.h"

namespace ECS::DropSites {

namespace {

// The villager unit that gathers a resource, whose own drop-site list is the
// answer. Wood is a lumberjack's business, so a Lumber Camp's acceptance of
// wood is stated by `VMLUM`, not by the camp.
Components::VillagerJob JobForResource(Core::ResourceType resource) {
    switch (resource) {
    case Core::ResourceType::Wood:  return Components::VillagerJob::Lumber;
    case Core::ResourceType::Gold:  return Components::VillagerJob::Gold;
    case Core::ResourceType::Stone: return Components::VillagerJob::Mine;
    default:                        return Components::VillagerJob::Forage;
    }
}

} // namespace

bool Accepts(int building_unit_id, Core::ResourceType resource, int gatherer) {
    if (building_unit_id < 0) return false;
    if (gatherer < 0) gatherer = Villagers::UnitId(JobForResource(resource), false);
    if (gatherer < 0) return false;

    const auto* ui = Core::ConfigManager::Get().GetUnitInterface(gatherer);
    if (ui == nullptr) return false;
    return std::find(ui->drop_sites.begin(), ui->drop_sites.end(),
                     building_unit_id) != ui->drop_sites.end();
}

bool AcceptsAnything(int building_unit_id, const Components::Worker& worker) {
    for (int r = 0; r < static_cast<int>(Core::ResourceType::COUNT); ++r) {
        if (worker.carried[r] <= 0) continue;
        if (Accepts(building_unit_id, static_cast<Core::ResourceType>(r), worker.gatherer)) {
            return true;
        }
    }
    return false;
}

int Deposit(int building_unit_id, Components::Worker& worker, Players::Player* owner) {
    int handed_over = 0;
    for (int r = 0; r < static_cast<int>(Core::ResourceType::COUNT); ++r) {
        if (worker.carried[r] <= 0) continue;
        if (!Accepts(building_unit_id, static_cast<Core::ResourceType>(r), worker.gatherer)) {
            continue;
        }
        if (owner != nullptr) {
            const auto type = static_cast<Core::ResourceType>(r);
            Players::AddStock(*owner, type, worker.carried[r]);
            // Gathered, for the Achievements (ecs/Score).
            Score::Collected(*owner, Core::Rules::DatResource(type),
                             Core::Fixed32(worker.carried[r]));
        }
        handed_over += worker.carried[r];
        worker.carried[r] = 0;
    }
    return handed_over;
}

bool Takes(const entt::registry& registry, entt::entity unit, entt::entity building) {
    const auto* worker = registry.try_get<Components::Worker>(unit);
    const auto* site = registry.try_get<Components::Building>(building);
    const auto* id = registry.try_get<Components::Identity>(building);
    const auto* mine = registry.try_get<Components::Owner>(unit);
    const auto* theirs = registry.try_get<Components::Owner>(building);
    if (worker == nullptr || site == nullptr || id == nullptr || mine == nullptr || theirs == nullptr) {
        return false;
    }
    if (site->state != Components::BuildingState::Complete || mine->player_id != theirs->player_id) {
        return false;
    }
    for (int r = 0; r < static_cast<int>(Core::ResourceType::COUNT); ++r) {
        if (worker->carried[r] > 0 &&
            Accepts(id->unit_id, static_cast<Core::ResourceType>(r), worker->gatherer)) {
            return true;
        }
    }
    return worker->gather_progress.value > 0 &&
           Accepts(id->unit_id, worker->carry_type, worker->gatherer);
}

void DepositAll(int building_unit_id, Components::Worker& worker, Players::Player* owner) {
    Deposit(building_unit_id, worker, owner);
    if (worker.gather_progress.value <= 0 ||
        !Accepts(building_unit_id, worker.carry_type, worker.gatherer)) {
        return;
    }
    if (owner != nullptr) {
        const int dat = Core::Rules::DatResource(worker.carry_type);
        Players::AddResource(*owner, dat, worker.gather_progress);
        Score::Collected(*owner, dat, worker.gather_progress);
    }
    worker.gather_progress = Core::Fixed32(0);
}

void Bank(Components::Worker& worker, Players::Player* owner) {
    const Core::Fixed32 half = Core::Fixed32(1) / Core::Fixed32(2);
    for (int r = 0; r < static_cast<int>(Core::ResourceType::COUNT); ++r) {
        const auto type = static_cast<Core::ResourceType>(r);
        Core::Fixed32 held(worker.carried[r]);
        if (type == worker.carry_type) held = held + worker.gather_progress;
        const int banked = (held + half).ToInt();
        if (owner != nullptr && banked > 0) {
            Players::AddStock(*owner, type, banked);
            Score::Collected(*owner, Core::Rules::DatResource(type), Core::Fixed32(banked));
        }
        worker.carried[r] = 0;
    }
    worker.gather_progress = Core::Fixed32(0);
}

} // namespace ECS::DropSites
