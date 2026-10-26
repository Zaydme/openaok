#include "ecs/Techs.h"

#include "ecs/Events.h"

#include "core/Rules.h"
#include "ecs/Effects.h"
#include "ecs/Players.h"
#include "ecs/Score.h"

namespace ECS::Techs {

namespace {

namespace R = Core::Rules;

// Where the civ table names each age's technology, in age order.
constexpr int kAgeTechResources[] = {R::Res::DarkAgeTech, R::Res::FeudalAgeTech,
                                     R::Res::CastleAgeTech, R::Res::ImperialAgeTech};
constexpr int kAges = 4;

bool RequirementsMet(const Players::Player& player, const R::TechRules& tech) {
    int done = 0;
    for (int i = 0; i < tech.required_listed; ++i) {
        const int required = tech.required[i];
        if (required >= 0 && required < static_cast<int>(player.techs.size()) &&
            player.techs[required].state == Players::TechState::Done) {
            ++done;
        }
    }
    return done >= tech.required_count;
}

// Marks done and applies, without looking for what it unlocks.
void Research(entt::registry& registry, Players::Player& player, int tech_id) {
    Players::TechStatus& status = player.techs[tech_id];
    if (status.state == Players::TechState::Done) return;
    status.state = Players::TechState::Done;
    if (const R::TechRules* tech = R::Tech(tech_id)) {
        Effects::Apply(registry, player.id, tech->effect);
    }
    // Every technology done passes here: the score counts it after its
    // effect.
    Score::Researched(registry, player.id, tech_id);
}

// Researches every technology no building researches whose requirements are
// now met, until nothing more completes -- one completing can satisfy another.
void Cascade(entt::registry& registry, Core::PlayerId player_id) {
    const auto& techs = R::Get().techs;
    bool progressed = true;
    while (progressed) {
        progressed = false;
        Players::Player* player = Players::Find(registry, player_id);
        if (player == nullptr) return;
        for (int id = 0; id < static_cast<int>(techs.size()); ++id) {
            const R::TechRules& tech = techs[id];
            if (!tech.valid || tech.location >= 0) continue;
            if (player->techs[id].state != Players::TechState::Locked) continue;
            if (!RequirementsMet(*player, tech)) continue;
            Research(registry, *player, id);
            progressed = true;
        }
    }
}

// Whether a command of a civ's tech tree shuts something All Techs opens: a
// unit or building disabled (Enable Unit, mode 0), or a technology disabled
// that a building researches or that only makes units available.
bool AllTechsLifts(const R::EffectCommand& command) {
    if (command.type == R::EffectType::EnableUnit) return command.b == 0;
    if (command.type != R::EffectType::DisableTech) return false;
    const R::TechRules* tech = R::Tech(command.d_int);
    if (tech == nullptr) return false;
    if (tech->location >= 0) return true;
    const R::Effect* effect = R::EffectById(tech->effect);
    if (effect == nullptr || effect->commands.empty()) return false;
    for (const R::EffectCommand& c : effect->commands) {
        if (c.type != R::EffectType::EnableUnit) return false;
    }
    return true;
}

} // namespace

void Start(entt::registry& registry, Core::PlayerId player_id, int starting_age,
           bool all_techs) {
    Players::Player* player = Players::Find(registry, player_id);
    if (player == nullptr) return;
    // The tech tree first: it disables what this civ never gets, including
    // other civilisations' bonuses that would otherwise start researched.
    // All Techs lifts what the tree shuts of what a player chooses -- a unit
    // or a building to make, a technology a building researches, and the
    // automatic technologies that only make units available ("Make Camels
    // Available", "Wall 2 (make avail)") -- and keeps the rest, the tree
    // switching off the bonuses that are other civilisations' (the Turks'
    // "C-Bonus, 1/2 Price Techs" is the bonus their tree alone leaves on).
    if (const R::CivRules* civ = R::Civ(player->civ); civ != nullptr) {
        if (!all_techs) {
            Effects::Apply(registry, player_id, civ->tech_tree);
        } else if (const R::Effect* own = R::EffectById(civ->tech_tree)) {
            for (const R::EffectCommand& command : own->commands) {
                if (!AllTechsLifts(command)) Effects::ApplyCommand(registry, player_id, command);
            }
        }
    }
    Cascade(registry, player_id);

    // Up to the lobby's starting age, researching each age in turn.
    for (int age = 0; age < kAges && age <= starting_age; ++age) {
        const int tech = AgeTech(registry, player_id, age);
        Players::Player* p = Players::Find(registry, player_id);
        if (p == nullptr || tech < 0 || tech >= static_cast<int>(p->techs.size())) break;
        if (p->techs[tech].state != Players::TechState::Disabled) {
            Research(registry, *p, tech);
            Cascade(registry, player_id);
        }
    }
}

bool Researched(const entt::registry& registry, Core::PlayerId player_id, int tech_id) {
    const Players::Player* player = Players::Find(registry, player_id);
    if (player == nullptr || tech_id < 0 ||
        tech_id >= static_cast<int>(player->techs.size())) {
        return false;
    }
    return player->techs[tech_id].state == Players::TechState::Done;
}

bool Available(const entt::registry& registry, Core::PlayerId player_id, int tech_id) {
    const Players::Player* player = Players::Find(registry, player_id);
    const R::TechRules* tech = R::Tech(tech_id);
    if (player == nullptr || tech == nullptr ||
        tech_id >= static_cast<int>(player->techs.size())) {
        return false;
    }
    if (player->techs[tech_id].state != Players::TechState::Locked) return false;
    return RequirementsMet(*player, *tech);
}

std::vector<int> ResearchableAt(const entt::registry& registry, Core::PlayerId player_id,
                                int building_unit_id) {
    std::vector<int> out;
    const auto& techs = R::Get().techs;
    for (int id = 0; id < static_cast<int>(techs.size()); ++id) {
        if (!techs[id].valid || techs[id].location != building_unit_id) continue;
        if (Available(registry, player_id, id)) out.push_back(id);
    }
    return out;
}

void Complete(entt::registry& registry, Core::PlayerId player_id, int tech_id) {
    Players::Player* player = Players::Find(registry, player_id);
    if (player == nullptr || tech_id < 0 ||
        tech_id >= static_cast<int>(player->techs.size())) {
        return;
    }
    if (player->techs[tech_id].state == Players::TechState::Disabled) return;
    const bool news = player->techs[tech_id].state != Players::TechState::Done;
    Research(registry, *player, tech_id);
    if (news) {
        Events::Emit(registry, {.kind = Events::Kind::Researched, .player = player_id,
                                .id = tech_id});
    }
    Cascade(registry, player_id);
}

void BuildingFinished(entt::registry& registry, Core::PlayerId player_id, int unit_id) {
    const R::UnitRules* unit = Players::UnitOf(registry, player_id, unit_id);
    if (unit == nullptr || unit->tech < 0) return;
    if (Researched(registry, player_id, unit->tech)) return;
    Complete(registry, player_id, unit->tech);
}

int Age(const entt::registry& registry, Core::PlayerId player_id) {
    const Players::Player* player = Players::Find(registry, player_id);
    if (player == nullptr) return 0;
    return Players::Resource(*player, R::Res::CurrentAge).ToInt();
}

int AgeTech(const entt::registry& registry, Core::PlayerId player_id, int age) {
    const Players::Player* player = Players::Find(registry, player_id);
    if (player == nullptr || age < 0 || age >= kAges) return -1;
    const int tech = Players::Resource(*player, kAgeTechResources[age]).ToInt();
    return R::Tech(tech) != nullptr ? tech : -1;
}

int NextAgeTech(const entt::registry& registry, Core::PlayerId player_id) {
    for (int age = 1; age < kAges; ++age) {
        const int tech = AgeTech(registry, player_id, age);
        if (tech >= 0 && !Researched(registry, player_id, tech)) return tech;
    }
    return -1;
}

} // namespace ECS::Techs
