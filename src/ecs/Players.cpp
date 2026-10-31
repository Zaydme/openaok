#include "ecs/Players.h"

#include <algorithm>

#include "ecs/components/Owner.h"

namespace ECS::Players {

namespace {

// Enough room for every resource slot a civ table holds and every slot an
// effect names.
size_t ResourceSlots() {
    size_t slots = 0;
    for (const auto& civ : Core::Rules::Get().civs) {
        slots = std::max(slots, civ.resources.size());
    }
    return std::max<size_t>(slots, 256);
}

} // namespace

void Setup(entt::registry& registry, const Core::GameSetup& setup) {
    const auto& rules = Core::Rules::Get();
    Table table;
    table.population_limit = setup.population_limit;

    for (int seat = 0; seat < Core::kMaxPlayers; ++seat) {
        const Core::PlayerSetup& chair = setup.players[seat];
        if (!chair.active) continue;
        const Core::Rules::CivRules* civ = Core::Rules::Civ(chair.civ);
        if (civ == nullptr) continue;

        Player& player = table.players[seat];
        player.active = true;
        player.id = static_cast<Core::PlayerId>(seat);
        player.civ = chair.civ;
        player.team = chair.team;

        player.resources.assign(ResourceSlots(), Core::Fixed32(0));
        std::copy(civ->resources.begin(), civ->resources.end(),
                  player.resources.begin());
        if (seat != Core::PLAYER_GAIA) {
            for (int r : {Core::Rules::Res::Food, Core::Rules::Res::Wood,
                          Core::Rules::Res::Stone, Core::Rules::Res::Gold}) {
                player.resources[r] =
                    Core::Fixed32(Core::StartingStock(setup.resources, r));
            }
        }

        // The unit rules, as this civ starts them: the same numbers for every
        // civ, enabled where its own table enables them.
        player.units = rules.units;
        for (auto& unit : player.units) unit.enabled = false;
        for (int id : civ->enabled) {
            if (id >= 0 && id < static_cast<int>(player.units.size())) {
                player.units[id].enabled = true;
            }
        }
        player.art.resize(player.units.size());
        for (size_t id = 0; id < player.art.size(); ++id) {
            player.art[id] = static_cast<int>(id);
        }

        player.techs.resize(rules.techs.size());
        for (size_t id = 0; id < rules.techs.size(); ++id) {
            const Core::Rules::TechRules& tech = rules.techs[id];
            TechStatus& status = player.techs[id];
            if (!tech.valid) {
                status.state = TechState::Disabled;
                continue;
            }
            std::copy(std::begin(tech.costs), std::end(tech.costs),
                      std::begin(status.costs));
            status.cost_count = tech.cost_count;
            status.time = tech.time;
            // Another civilisation's own technology is never this one's.
            if (tech.civ >= 0 && tech.civ != chair.civ) {
                status.state = TechState::Disabled;
            }
        }
    }

    registry.ctx().insert_or_assign<Table>(std::move(table));
}

Player* Find(entt::registry& registry, Core::PlayerId player) {
    if (player >= Core::kMaxPlayers) return nullptr;
    auto* table = registry.ctx().find<Table>();
    if (table == nullptr) return nullptr;
    Player& p = table->players[player];
    return p.active ? &p : nullptr;
}

const Player* Find(const entt::registry& registry, Core::PlayerId player) {
    if (player >= Core::kMaxPlayers) return nullptr;
    const auto* table = registry.ctx().find<Table>();
    if (table == nullptr) return nullptr;
    const Player& p = table->players[player];
    return p.active ? &p : nullptr;
}

const Table* TableOf(const entt::registry& registry) { return registry.ctx().find<Table>(); }

const Core::Rules::UnitRules* UnitOf(const Table* table, Core::PlayerId player, int unit_id) {
    if (unit_id < 0) return nullptr;
    if (table != nullptr && player < Core::kMaxPlayers && table->players[player].active) {
        const Player& p = table->players[player];
        if (unit_id < static_cast<int>(p.units.size()) && p.units[unit_id].valid) {
            return &p.units[unit_id];
        }
        return nullptr;
    }
    return Core::Rules::Unit(unit_id);
}

const Core::Rules::UnitRules* UnitOf(const entt::registry& registry,
                                     Core::PlayerId player, int unit_id) {
    if (unit_id < 0) return nullptr;
    return UnitOf(player < Core::kMaxPlayers ? TableOf(registry) : nullptr, player, unit_id);
}

int ArtOf(const entt::registry& registry, Core::PlayerId player, int unit_id) {
    if (const Player* p = Find(registry, player)) {
        if (unit_id >= 0 && unit_id < static_cast<int>(p->art.size())) {
            return p->art[unit_id];
        }
    }
    return unit_id;
}

Core::PlayerId RulesPlayer(const entt::registry& registry, entt::entity entity) {
    if (const auto* maker = registry.try_get<Components::RulesOf>(entity)) return maker->player;
    const auto* owner = registry.try_get<Components::Owner>(entity);
    return owner != nullptr ? owner->player_id : Core::PLAYER_GAIA;
}

const Core::Rules::UnitRules* RulesFor(const entt::registry& registry, entt::entity entity,
                                       int unit_id) {
    return UnitOf(registry, RulesPlayer(registry, entity), unit_id);
}

int PopulationLimit(const entt::registry& registry) {
    const auto* table = registry.ctx().find<Table>();
    return table != nullptr ? table->population_limit : 75;
}

Core::Fixed32 Resource(const Player& player, int dat_resource) {
    if (dat_resource < 0 || dat_resource >= static_cast<int>(player.resources.size())) {
        return Core::Fixed32(0);
    }
    return player.resources[dat_resource];
}

void AddResource(Player& player, int dat_resource, Core::Fixed32 amount) {
    if (dat_resource < 0 || dat_resource >= static_cast<int>(player.resources.size())) {
        return;
    }
    player.resources[dat_resource] = player.resources[dat_resource] + amount;
}

void SetResource(Player& player, int dat_resource, Core::Fixed32 amount) {
    if (dat_resource < 0 || dat_resource >= static_cast<int>(player.resources.size())) {
        return;
    }
    player.resources[dat_resource] = amount;
}

int Stock(const Player& player, Core::ResourceType type) {
    return Resource(player, Core::Rules::DatResource(type)).ToInt();
}

void AddStock(Player& player, Core::ResourceType type, int amount) {
    AddResource(player, Core::Rules::DatResource(type), Core::Fixed32(amount));
}

bool CanPay(const Player& player, const Core::Rules::Cost* costs, int count) {
    for (int i = 0; i < count; ++i) {
        if (!costs[i].paid || costs[i].amount <= 0) continue;
        if (Resource(player, costs[i].resource) < Core::Fixed32(costs[i].amount)) {
            return false;
        }
    }
    return true;
}

void Pay(Player& player, const Core::Rules::Cost* costs, int count, int sign) {
    for (int i = 0; i < count; ++i) {
        if (!costs[i].paid || costs[i].amount == 0) continue;
        AddResource(player, costs[i].resource,
                    Core::Fixed32(sign * static_cast<int>(costs[i].amount)));
    }
}

void Contribute(entt::registry& registry, Core::PlayerId player, int unit_id,
                uint8_t modes, int sign) {
    Player* p = Find(registry, player);
    if (p == nullptr) return;
    const Core::Rules::UnitRules* unit = UnitOf(registry, player, unit_id);
    if (unit == nullptr) return;
    for (int i = 0; i < unit->storage_count; ++i) {
        const Core::Rules::Storage& store = unit->storage[i];
        if ((store.mode & modes) == 0) continue;
        AddResource(*p, store.resource,
                    sign >= 0 ? store.amount : Core::Fixed32(0) - store.amount);
    }
}

} // namespace ECS::Players
