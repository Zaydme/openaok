#include "ecs/Spies.h"

#include <algorithm>
#include <vector>

#include "core/ConfigManager.h"
#include "core/Rules.h"
#include "ecs/Diplomacy.h"
#include "ecs/Events.h"
#include "ecs/Flares.h"
#include "ecs/Players.h"
#include "ecs/Techs.h"
#include "ecs/Victory.h"
#include "ecs/components/Building.h"
#include "ecs/components/Garrison.h"
#include "ecs/components/Identity.h"
#include "ecs/components/Mortal.h"
#include "ecs/components/Owner.h"
#include "ecs/components/Transform.h"

namespace ECS::Spies {

namespace {

namespace R = Core::Rules;

// The villagers Spies' price counts: never fewer, never more.
constexpr int kFewestVillagers = 1;
constexpr int kMostVillagers = 150;
// The dat's research rate: the share of a technology's price a player pays.
constexpr int kResearchRate = 85;
// Treason's price in gold, each time.
constexpr int kTreasonGold = 400;

uint16_t Bit(Core::PlayerId player) { return static_cast<uint16_t>(1u << player); }

// The dat's Spies technologies, found once.
const std::vector<int>& SpiesTechs() {
    static const std::vector<int> techs = [] {
        std::vector<int> out;
        const int count = static_cast<int>(R::Get().techs.size());
        for (int id = 0; id < count; ++id) {
            if (Techs::Spies(id)) out.push_back(id);
        }
        return out;
    }();
    return techs;
}

bool InGame(const entt::registry& registry, Core::PlayerId player) {
    if (Players::Find(registry, player) == nullptr) return false;
    const Victory::Outcome* outcome = Victory::Read(registry);
    return outcome == nullptr || !outcome->Defeated(player);
}

} // namespace

void Setup(entt::registry& registry, const Core::GameSetup& setup) {
    if (!setup.regicide && !setup.death_match) return;
    for (int p = 1; p < Core::kMaxPlayers; ++p) {
        Players::Player* seat = Players::Find(registry, static_cast<Core::PlayerId>(p));
        if (seat == nullptr) continue;
        for (int id : SpiesTechs()) seat->techs[static_cast<size_t>(id)].state = Players::TechState::Disabled;
    }
}

void Update(entt::registry& registry) {
    const std::vector<int>& techs = SpiesTechs();
    if (techs.empty()) return;
    for (int p = 1; p < Core::kMaxPlayers; ++p) {
        const auto player = static_cast<Core::PlayerId>(p);
        Players::Player* seat = Players::Find(registry, player);
        if (seat == nullptr) continue;
        int villagers = 0;
        for (int q = 1; q < Core::kMaxPlayers; ++q) {
            const auto other = static_cast<Core::PlayerId>(q);
            if (other == player || !InGame(registry, other) || Diplomacy::Allied(registry, player, other)) {
                continue;
            }
            villagers += Players::Resource(*Players::Find(registry, other), R::Res::CivilianPopulation).ToInt();
        }
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
    }
}

bool CanTreason(const entt::registry& registry, Core::PlayerId player) {
    const auto* setup = registry.ctx().find<Core::GameSetup>();
    const Players::Player* seat = Players::Find(registry, player);
    return setup != nullptr && setup->regicide && seat != nullptr && InGame(registry, player) &&
           Players::Resource(*seat, R::Res::Gold) >= Core::Fixed32(kTreasonGold);
}

bool Treason(entt::registry& registry, entt::entity castle, Core::PlayerId player) {
    auto& cfg = Core::ConfigManager::Get();
    if (!registry.valid(castle) || !CanTreason(registry, player)) return false;
    const auto* owner = registry.try_get<Components::Owner>(castle);
    const auto* identity = registry.try_get<Components::Identity>(castle);
    const auto* building = registry.try_get<Components::Building>(castle);
    const auto* setup = registry.ctx().find<Core::GameSetup>();
    if (owner == nullptr || owner->player_id != player || identity == nullptr || building == nullptr ||
        building->state != Components::BuildingState::Complete ||
        identity->unit_id != cfg.GetUnitIdByName(setup->regicide_castle)) {
        return false;
    }
    Players::AddResource(*Players::Find(registry, player), R::Res::Gold, Core::Fixed32(-kTreasonGold));
    Events::Emit(registry, {.kind = Events::Kind::Treason, .player = player});

    // The side shown: the player and those it regards as allies.
    uint16_t side = Bit(player);
    for (int q = 1; q < Core::kMaxPlayers; ++q) {
        const auto other = static_cast<Core::PlayerId>(q);
        if (other != player && InGame(registry, other) &&
            Diplomacy::StanceOf(registry, player, other) == Diplomacy::Stance::Ally) {
            side = static_cast<uint16_t>(side | Bit(other));
        }
    }
    // Every King of a player it does not regard as an ally, where it stands
    // or the building it shelters in.
    const int king = cfg.GetUnitIdByName(setup->regicide_king);
    const int mark = cfg.GetUnitIdByName("FLARE4");
    auto kings = registry.view<const Components::Identity, const Components::Owner>();
    for (auto k : kings) {
        if (kings.get<const Components::Identity>(k).unit_id != king) continue;
        const Core::PlayerId whose = kings.get<const Components::Owner>(k).player_id;
        if (whose == player || Diplomacy::StanceOf(registry, player, whose) == Diplomacy::Stance::Ally) continue;
        if (registry.any_of<Components::Dying, Components::Corpse>(k)) continue;
        entt::entity standing = k;
        if (const auto* in = registry.try_get<Components::Garrisoned>(k)) standing = in->building;
        const auto* at = registry.valid(standing) ? registry.try_get<Components::Transform>(standing) : nullptr;
        if (at != nullptr) Flares::Mark(registry, player, side, at->position, mark);
    }
    return true;
}

} // namespace ECS::Spies
