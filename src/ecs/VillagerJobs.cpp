#include "ecs/VillagerJobs.h"

#include "core/ConfigManager.h"
#include "core/Rules.h"
#include "ecs/components/Identity.h"
#include "ecs/components/Resource.h"

namespace ECS::Villagers {

const char* JobCode(Components::VillagerJob job) {
    switch (job) {
    case Components::VillagerJob::Build:  return "BLD";
    case Components::VillagerJob::Farm:   return "FAR";
    case Components::VillagerJob::Forage: return "FOR";
    case Components::VillagerJob::Gold:   return "GLD";
    case Components::VillagerJob::Hunt:   return "HUN";
    case Components::VillagerJob::Lumber: return "LUM";
    case Components::VillagerJob::Mine:   return "MIN";
    case Components::VillagerJob::Shepherd: return "SHE";
    case Components::VillagerJob::Fish:   return "FIS";
    case Components::VillagerJob::Repair: return "REP";
    default:                              return "BAS";
    }
}

Components::VillagerJob JobFor(const Components::Worker& worker) {
    switch (worker.state) {
    case Core::UnitState::Building:
        return Components::VillagerJob::Build;
    case Core::UnitState::Repairing:
        return Components::VillagerJob::Repair;
    case Core::UnitState::Moving:
    case Core::UnitState::Gathering:
    case Core::UnitState::Returning:
        // The job its target called for, from the moment it was sent: a
        // villager sent to a tree picks up its axe on the way.
        if (worker.job != Components::VillagerJob::None) return worker.job;
        switch (worker.carry_type) {
        case Core::ResourceType::Wood:  return Components::VillagerJob::Lumber;
        case Core::ResourceType::Gold:  return Components::VillagerJob::Gold;
        case Core::ResourceType::Stone: return Components::VillagerJob::Mine;
        default:                        return Components::VillagerJob::Forage;
        }
    default:
        return Components::VillagerJob::Base;
    }
}

Components::VillagerJob JobForTarget(const entt::registry& registry,
                                     entt::entity target, bool& hunt) {
    hunt = false;
    const auto* identity = registry.try_get<Components::Identity>(target);
    if (identity != nullptr) {
        const Core::Rules::UnitRules* aimed = Core::Rules::Unit(identity->unit_id);
        const int cls = aimed != nullptr ? aimed->cls : -1;
        static const Components::VillagerJob kJobs[] = {
            Components::VillagerJob::Forage, Components::VillagerJob::Farm,
            Components::VillagerJob::Hunt,   Components::VillagerJob::Lumber,
            Components::VillagerJob::Mine,   Components::VillagerJob::Gold,
            Components::VillagerJob::Shepherd, Components::VillagerJob::Fish,
        };
        for (const auto job : kJobs) {
            const Core::Rules::UnitRules* worker = Core::Rules::Unit(UnitId(job, false));
            if (worker == nullptr) continue;
            for (int i = 0; i < worker->task_count; ++i) {
                const auto& task = worker->tasks[i];
                if (task.action != Core::Rules::TaskAction::Gather &&
                    task.action != Core::Rules::TaskAction::Hunt) {
                    continue;
                }
                if (!task.Targets(identity->unit_id, cls)) continue;
                hunt = task.action == Core::Rules::TaskAction::Hunt;
                return job;
            }
        }
    }
    // Nothing names it: go by what it holds.
    if (const auto* held = registry.try_get<Components::Resource>(target)) {
        switch (held->type) {
        case Core::ResourceType::Wood:  return Components::VillagerJob::Lumber;
        case Core::ResourceType::Gold:  return Components::VillagerJob::Gold;
        case Core::ResourceType::Stone: return Components::VillagerJob::Mine;
        default:                        return Components::VillagerJob::Forage;
        }
    }
    return Components::VillagerJob::Base;
}

std::string DatName(Components::VillagerJob job, bool female) {
    return std::string(female ? "VF" : "VM") + JobCode(job);
}

int UnitId(Components::VillagerJob job, bool female) {
    // Looked up by name once for each job, then kept: this is asked for every
    // villager every tick.
    constexpr size_t kJobs = static_cast<size_t>(Components::VillagerJob::Repair) + 1;
    static int ids[kJobs][2];
    static bool known[kJobs][2] = {};
    const size_t j = static_cast<size_t>(job);
    if (j >= kJobs) return Core::ConfigManager::Get().GetUnitIdByName(DatName(job, female));
    if (!known[j][female]) {
        ids[j][female] = Core::ConfigManager::Get().GetUnitIdByName(DatName(job, female));
        known[j][female] = true;
    }
    return ids[j][female];
}

int DisplayUnit(const entt::registry& registry, entt::entity entity) {
    const auto* identity = registry.try_get<Components::Identity>(entity);
    const int own = identity ? identity->unit_id : -1;

    const auto* anim = registry.try_get<Components::Animation>(entity);
    if (anim == nullptr || anim->job == Components::VillagerJob::None ||
        anim->job == Components::VillagerJob::Base) {
        return own;
    }

    // The job's unit, only if the interface knows it, so a job with no entry
    // does not blank the panel: worked out once for each job, as UnitId is.
    constexpr size_t kJobs = static_cast<size_t>(Components::VillagerJob::Repair) + 1;
    static int shown[kJobs][2];
    static bool known[kJobs][2] = {};
    const size_t j = static_cast<size_t>(anim->job);
    const bool female = anim->female;
    int job_id = -1;
    if (j < kJobs && known[j][female]) {
        job_id = shown[j][female];
    } else {
        job_id = UnitId(anim->job, female);
        if (job_id >= 0 && Core::ConfigManager::Get().GetUnitInterface(job_id) == nullptr) job_id = -1;
        if (j < kJobs) {
            shown[j][female] = job_id;
            known[j][female] = true;
        }
    }
    return job_id >= 0 ? job_id : own;
}

} // namespace ECS::Villagers
