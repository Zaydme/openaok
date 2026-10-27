#include "ecs/Felling.h"

#include <algorithm>

#include "core/ConfigManager.h"
#include "ecs/components/Felled.h"
#include "ecs/components/Identity.h"
#include "ecs/components/Resource.h"
#include "ecs/components/Sprite.h"
#include "ecs/components/Transform.h"
#include "physics/Grid.h"

namespace ECS::Felling {

namespace {

namespace R = Core::Rules;

constexpr int kTreeClass = 15;

// How many strokes of the felling graphic bring a tree down, which the dat
// does not give.
constexpr int kStrokes = 1;

// A stroke's length when the rules give no reload.
constexpr int32_t kStrokeMs = 2000;

const R::UnitRules* TreeRules(const entt::registry& registry, entt::entity e) {
    const auto* identity = registry.try_get<Components::Identity>(e);
    if (identity == nullptr) return nullptr;
    const R::UnitRules* rules = R::Unit(identity->unit_id);
    if (rules == nullptr || rules->cls != kTreeClass || rules->dying_graphic < 0) return nullptr;
    return rules;
}

} // namespace

bool Standing(const entt::registry& registry, entt::entity target) {
    if (!registry.valid(target) || registry.all_of<Components::Felled>(target)) return false;
    if (!registry.all_of<Components::Resource>(target)) return false;
    return TreeRules(registry, target) != nullptr;
}

int32_t FellingMs(const R::UnitRules* job) {
    const int32_t stroke = job != nullptr && job->reload.value > 0
                               ? (job->reload * Core::Fixed32(1000)).ToInt()
                               : kStrokeMs;
    return stroke * kStrokes;
}

void Fell(entt::registry& registry, entt::entity tree) {
    const R::UnitRules* rules = TreeRules(registry, tree);
    if (rules == nullptr) return;
    const auto art = Core::ConfigManager::Get().GetGraphic(rules->dying_graphic);
    const auto& at = registry.get<Components::Transform>(tree).position;
    // The felled graphic's frames are shapes, one a tile, as a forest's are.
    const int shapes = std::max(1, art.angle_count);
    const int shape = (Physics::TileOf(at.x) + Physics::TileOf(at.y)) % shapes;
    registry.emplace_or_replace<Components::Felled>(tree, Components::Felled{shape});
    if (!art.Valid()) return;
    if (auto* sprite = registry.try_get<Components::Sprite>(tree)) {
        sprite->slp_id = art.slp_id;
        sprite->slp_frame = shape;
        sprite->current_frame = 0;
        // Its own shadow, or none: the felled trunk has no companion shadow.
        sprite->shadow_slp = -1;
        sprite->shadow_resolved = false;
    }
}

} // namespace ECS::Felling
