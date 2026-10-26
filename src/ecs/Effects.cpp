#include "ecs/Effects.h"

#include <algorithm>
#include <vector>

#include "core/ConfigManager.h"
#include "ecs/Buildings.h"
#include "ecs/Players.h"
#include "ecs/components/Animation.h"
#include "ecs/components/Building.h"
#include "ecs/components/Health.h"
#include "ecs/components/Identity.h"
#include "ecs/components/Owner.h"
#include "ecs/components/Sight.h"
#include "ecs/systems/AnimationSystem.h"

namespace ECS::Effects {

namespace {

namespace R = Core::Rules;

enum class Op { Set, Add, Multiply };

// Attributes are stored back into the field's own type, so a fractional
// result in a whole-number field is cut toward zero: a 0.33 multiplier on a
// 30-wood House gives 9.
int Truncate(Core::Fixed32 value) {
    if (value.value >= 0) return value.ToInt();
    return -Core::Fixed32(-value.value, true).ToInt();
}

Core::Fixed32 Combine(Core::Fixed32 value, Op op, Core::Fixed32 d) {
    switch (op) {
    case Op::Set:      return d;
    case Op::Add:      return value + d;
    case Op::Multiply: return value * d;
    }
    return value;
}

int CombineInt(int value, Op op, Core::Fixed32 d) {
    return Truncate(Combine(Core::Fixed32(value), op, d));
}

// Attack and armour pack a class into D as class * 256 + amount.
void ModifyClassList(R::ClassAmount* list, int& count, Op op,
                     const R::EffectCommand& command) {
    if (op == Op::Multiply) {
        for (int i = 0; i < count; ++i) {
            list[i].amount = static_cast<int16_t>(
                CombineInt(list[i].amount, op, command.d));
        }
        return;
    }
    const int cls = command.d_int / 256;
    const int amount = command.d_int % 256;
    for (int i = 0; i < count; ++i) {
        if (list[i].cls != cls) continue;
        list[i].amount = static_cast<int16_t>(
            op == Op::Set ? amount : list[i].amount + amount);
        return;
    }
    // A class the unit does not have yet. Setting one gives it the class;
    // adding to one it lacks changes nothing.
    if (op == Op::Set && count < R::kMaxClassEntries) {
        list[count].cls = static_cast<int16_t>(cls);
        list[count].amount = static_cast<int16_t>(amount);
        ++count;
    }
}

int CostResourceFor(int attribute) {
    switch (attribute) {
    case R::Attribute::FoodCost:  return R::Res::Food;
    case R::Attribute::WoodCost:  return R::Res::Wood;
    case R::Attribute::GoldCost:  return R::Res::Gold;
    case R::Attribute::StoneCost: return R::Res::Stone;
    default:                      return -1;
    }
}

void ModifyCosts(R::UnitRules& unit, int resource, Op op, Core::Fixed32 d) {
    for (int i = 0; i < unit.cost_count; ++i) {
        R::Cost& cost = unit.costs[i];
        if (!cost.paid) continue;
        if (resource >= 0 && cost.resource != resource) continue;
        cost.amount = static_cast<int16_t>(std::max(0, CombineInt(cost.amount, op, d)));
    }
}

// Changes one attribute of one unit slot.
void ModifyAttribute(R::UnitRules& unit, Op op, const R::EffectCommand& c) {
    const Core::Fixed32 d = c.d;
    switch (c.c) {
    case R::Attribute::HitPoints:       unit.hp = std::max(1, CombineInt(unit.hp, op, d)); break;
    case R::Attribute::LineOfSight:     unit.los = Combine(unit.los, op, d); break;
    case R::Attribute::GarrisonCapacity: unit.garrison = CombineInt(unit.garrison, op, d); break;
    case R::Attribute::SizeX:           unit.size_x = Combine(unit.size_x, op, d); break;
    case R::Attribute::SizeY:           unit.size_y = Combine(unit.size_y, op, d); break;
    case R::Attribute::Speed:           unit.speed = Combine(unit.speed, op, d); break;
    case R::Attribute::RotationSpeed:   unit.rotation = Combine(unit.rotation, op, d); break;
    case R::Attribute::Armour:          ModifyClassList(unit.armours, unit.armour_count, op, c); break;
    case R::Attribute::Attack:          ModifyClassList(unit.attacks, unit.attack_count, op, c); break;
    case R::Attribute::ReloadTime:      unit.reload = Combine(unit.reload, op, d); break;
    case R::Attribute::Accuracy:        unit.accuracy = CombineInt(unit.accuracy, op, d); break;
    case R::Attribute::MaxRange:        unit.max_range = Combine(unit.max_range, op, d); break;
    case R::Attribute::WorkRate:        unit.work_rate = Combine(unit.work_rate, op, d); break;
    case R::Attribute::CarryCapacity:   unit.carry = CombineInt(unit.carry, op, d); break;
    case R::Attribute::BaseArmour:      unit.base_armour = CombineInt(unit.base_armour, op, d); break;
    case R::Attribute::ProjectileUnit:  unit.projectile = CombineInt(unit.projectile, op, d); break;
    case R::Attribute::TerrainDefense:  unit.terrain_defense = c.d_int; break;
    case R::Attribute::SmartProjectiles: unit.missile.smart = static_cast<int8_t>(c.d_int); break;
    case R::Attribute::MinRange:        unit.min_range = Combine(unit.min_range, op, d); break;
    case R::Attribute::StorageAmount:
        if (unit.storage_count > 0) unit.storage[0].amount = Combine(unit.storage[0].amount, op, d);
        break;
    case R::Attribute::BlastWidth:      unit.blast_width = Combine(unit.blast_width, op, d); break;
    case R::Attribute::SearchRadius:    unit.search_radius = Combine(unit.search_radius, op, d); break;
    case R::Attribute::AllCosts:        ModifyCosts(unit, -1, op, d); break;
    case R::Attribute::TrainTime:       unit.train_time = std::max(0, CombineInt(unit.train_time, op, d)); break;
    case R::Attribute::TotalMissiles:   unit.missiles = Combine(unit.missiles, op, d); break;
    case R::Attribute::FoodCost:
    case R::Attribute::WoodCost:
    case R::Attribute::GoldCost:
    case R::Attribute::StoneCost:       ModifyCosts(unit, CostResourceFor(c.c), op, d); break;
    case R::Attribute::MaxTotalMissiles: unit.max_missiles = CombineInt(unit.max_missiles, op, d); break;
    case R::Attribute::GarrisonHealRate: unit.garrison_heal = Combine(unit.garrison_heal, op, d); break;
    default: break;
    }
}

// The entities standing for `unit_id` that play by the player's table --
// its own, and what it made that another side has since converted, which
// goes on with its maker's technology (Components::RulesOf) -- collected so
// the caller can change them: rebuilding a building's art creates entities,
// which a live view must not see.
std::vector<entt::entity> EntitiesOf(entt::registry& registry, Core::PlayerId player,
                                     int unit_id) {
    std::vector<entt::entity> out;
    auto view = registry.view<Components::Identity, Components::Owner>();
    for (auto entity : view) {
        if (Players::RulesPlayer(registry, entity) != player) continue;
        if (view.get<Components::Identity>(entity).unit_id != unit_id) continue;
        out.push_back(entity);
    }
    return out;
}

// Brings the units already on the map into line with a slot's new rules: hit
// points follow the change in maximum -- a villager at 10 of 25 researching
// Loom stands at 25 of 40 -- and sight follows line of sight.
void RefreshVitals(entt::registry& registry, entt::entity entity, int old_max,
                   const R::UnitRules& now) {
    if (auto* health = registry.try_get<Components::Health>(entity)) {
        if (now.hp != old_max) {
            const int raised = health->current_hp + (now.hp - old_max);
            health->max_hp = now.hp;
            health->current_hp = std::clamp(raised, health->current_hp > 0 ? 1 : 0, now.hp);
        }
    }
    if (auto* sight = registry.try_get<Components::Sight>(entity)) {
        sight->radius = std::max(1, now.los.ToInt());
    }
}

void ModifyUnitSlot(entt::registry& registry, Players::Player& player, int unit_id,
                    Op op, const R::EffectCommand& command) {
    R::UnitRules& unit = player.units[unit_id];
    const int old_max = unit.hp;
    ModifyAttribute(unit, op, command);
    if (command.c != R::Attribute::HitPoints &&
        command.c != R::Attribute::LineOfSight) {
        return;
    }
    for (auto entity : EntitiesOf(registry, player.id, unit_id)) {
        RefreshVitals(registry, entity, old_max, unit);
    }
}

void ModifyUnits(entt::registry& registry, Players::Player& player, Op op,
                 const R::EffectCommand& command) {
    const int slots = static_cast<int>(player.units.size());
    if (command.a >= 0) {
        if (command.a < slots && player.units[command.a].valid) {
            ModifyUnitSlot(registry, player, command.a, op, command);
        }
        return;
    }
    if (command.b < 0) return;
    for (int id = 0; id < slots; ++id) {
        if (!player.units[id].valid || player.units[id].cls != command.b) continue;
        ModifyUnitSlot(registry, player, id, op, command);
    }
}

// One unit becomes another: the player's slot `from` takes the numbers and art
// of `to`, keeping its own id -- which is what the rest of the data refers to,
// so a Feudal Age Town Center still trains villagers, still takes their
// deliveries and still researches the Castle Age, all of which name unit 109.
//
// `upgraded`, when given, defers the buildings' art: the slots are noted and
// the buildings redrawn once the whole effect is in force (see Apply).
void Upgrade(entt::registry& registry, Players::Player& player, int from, int to,
             std::vector<int>* upgraded) {
    const int slots = static_cast<int>(player.units.size());
    if (from < 0 || from >= slots || to < 0 || to >= slots) return;
    if (!player.units[to].valid) return;

    const int old_max = player.units[from].hp;
    const bool enabled = player.units[from].enabled;
    player.units[from] = player.units[to];
    player.units[from].enabled = enabled;
    player.art[from] = player.art[to];

    auto& cfg = Core::ConfigManager::Get();
    for (auto entity : EntitiesOf(registry, player.id, from)) {
        RefreshVitals(registry, entity, old_max, player.units[from]);
        if (registry.all_of<Components::Building>(entity)) {
            if (upgraded == nullptr) Buildings::RefreshArt(registry, entity);
        } else if (auto* anim = registry.try_get<Components::Animation>(entity)) {
            Systems::AnimationSystem::LoadSet(*anim, cfg.GetUnitDatName(player.art[from]));
        }
    }
    if (upgraded != nullptr) upgraded->push_back(from);
}

// The player's buildings drawn from a slot of `upgraded`: one of them itself,
// or a building composed of pieces one of them names -- a Town Center's
// annexes are units of their own (618..621), which an age upgrades after the
// Town Center.
void RefreshBuildingArt(entt::registry& registry, Core::PlayerId player,
                        const std::vector<int>& upgraded) {
    auto& cfg = Core::ConfigManager::Get();
    auto named = [&](int unit_id) {
        return unit_id >= 0 && std::find(upgraded.begin(), upgraded.end(), unit_id) != upgraded.end();
    };
    std::vector<entt::entity> redraw;
    auto view = registry.view<Components::Building, Components::Identity>();
    for (auto entity : view) {
        if (Players::RulesPlayer(registry, entity) != player) continue;
        const int unit_id = view.get<Components::Identity>(entity).unit_id;
        bool affected = named(unit_id);
        if (!affected) {
            const auto* pieces = cfg.GetAnnexes(Players::ArtOf(registry, player, unit_id));
            if (pieces == nullptr) pieces = cfg.GetAnnexes(unit_id);
            for (size_t i = 0; pieces != nullptr && i < pieces->size() && !affected; ++i) {
                affected = named((*pieces)[i].unit_id) || named((*pieces)[i].counterpart_of);
            }
        }
        if (affected) redraw.push_back(entity);
    }
    for (entt::entity entity : redraw) Buildings::RefreshArt(registry, entity);
}

Players::TechStatus* TechOf(Players::Player& player, int tech_id) {
    if (tech_id < 0 || tech_id >= static_cast<int>(player.techs.size())) return nullptr;
    return &player.techs[tech_id];
}

} // namespace

namespace {

void ApplyOne(entt::registry& registry, Core::PlayerId player_id, const R::EffectCommand& c,
              std::vector<int>* upgraded) {
    Players::Player* player = Players::Find(registry, player_id);
    if (player == nullptr) return;

    switch (c.type) {
    case R::EffectType::SetAttribute:
        ModifyUnits(registry, *player, Op::Set, c);
        break;
    case R::EffectType::AddAttribute:
        ModifyUnits(registry, *player, Op::Add, c);
        break;
    case R::EffectType::MultiplyAttribute:
        ModifyUnits(registry, *player, Op::Multiply, c);
        break;
    case R::EffectType::Resource:
        if (c.b == 0) {
            Players::SetResource(*player, c.a, c.d);
        } else {
            Players::AddResource(*player, c.a, c.d);
        }
        break;
    case R::EffectType::MultiplyResource:
        Players::SetResource(*player, c.a, Players::Resource(*player, c.a) * c.d);
        break;
    case R::EffectType::EnableUnit:
        if (c.a >= 0 && c.a < static_cast<int>(player->units.size())) {
            player->units[c.a].enabled = c.b != 0;
        }
        break;
    case R::EffectType::UpgradeUnit:
        Upgrade(registry, *player, c.a, c.b, upgraded);
        break;
    case R::EffectType::TechCost:
        if (auto* tech = TechOf(*player, c.a)) {
            for (int i = 0; i < tech->cost_count; ++i) {
                R::Cost& cost = tech->costs[i];
                if (cost.resource != c.b) continue;
                cost.amount = static_cast<int16_t>(std::max(0,
                    CombineInt(cost.amount, c.c == 0 ? Op::Set : Op::Add, c.d)));
            }
        }
        break;
    case R::EffectType::TechTime:
        if (auto* tech = TechOf(*player, c.a)) {
            tech->time = std::max(0, CombineInt(tech->time, c.c == 0 ? Op::Set : Op::Add, c.d));
        }
        break;
    case R::EffectType::DisableTech:
        if (auto* tech = TechOf(*player, c.d_int)) {
            if (tech->state != Players::TechState::Done) {
                tech->state = Players::TechState::Disabled;
            }
        }
        break;
    default:
        break;
    }
}

} // namespace

void ApplyCommand(entt::registry& registry, Core::PlayerId player_id,
                  const R::EffectCommand& c) {
    ApplyOne(registry, player_id, c, nullptr);
}

void Apply(entt::registry& registry, Core::PlayerId player, int effect_id) {
    const R::Effect* effect = R::EffectById(effect_id);
    if (effect == nullptr) return;
    // A building's art waits for the whole effect: an age upgrades the Town
    // Center and then, separately, the pieces it is drawn from.
    std::vector<int> upgraded;
    for (const R::EffectCommand& command : effect->commands) {
        ApplyOne(registry, player, command, &upgraded);
    }
    if (!upgraded.empty()) RefreshBuildingArt(registry, player, upgraded);
}

} // namespace ECS::Effects
