#include "ecs/Turning.h"

#include <algorithm>
#include <cstdlib>

#include "core/FixedMath.h"
#include "core/Rules.h"
#include "ecs/Players.h"
#include "ecs/components/Identity.h"
#include "ecs/components/Sprite.h"

namespace ECS::Turning {

namespace {

// The fewest angles a turn is reckoned in, whatever the graphic has.
constexpr int kLeastAngles = 8;

} // namespace

bool Toward(entt::registry& registry, entt::entity unit, Components::Vector2D at, int32_t dt_ms) {
    auto* sprite = registry.try_get<Components::Sprite>(unit);
    const auto* from = registry.try_get<Components::Transform>(unit);
    const auto* identity = registry.try_get<Components::Identity>(unit);
    if (sprite == nullptr || from == nullptr) return true;
    const Core::Fixed32 dx = at.x - from->position.x;
    const Core::Fixed32 dy = at.y - from->position.y;
    if (dx.value == 0 && dy.value == 0) return true;
    const uint8_t want = Core::FixedMath::WorldVectorToHeading(dx, dy);

    const Core::Rules::UnitRules* rules =
        identity != nullptr ? Players::RulesFor(registry, unit, identity->unit_id) : nullptr;
    if (rules == nullptr || rules->rotation.value <= 0) {
        sprite->heading = want;
        return true;
    }

    // One angle, in 256ths of a turn, and how far round the target lies, the
    // shorter way: positive clockwise on screen.
    const int step = 256 / std::max(kLeastAngles, sprite->angles);
    auto off = [&]() {
        const int d = (static_cast<int>(want) - static_cast<int>(sprite->heading) + 256) % 256;
        return d > 128 ? d - 256 : d;
    };
    if (sprite->turn_wait_ms > 0) sprite->turn_wait_ms = std::max(0, sprite->turn_wait_ms - dt_ms);
    if (std::abs(off()) * 2 <= step) return true;
    if (sprite->turn_wait_ms > 0) return false;
    const int turned = static_cast<int>(sprite->heading) + (off() > 0 ? step : -step);
    sprite->heading = static_cast<uint8_t>((turned + 256) % 256);
    sprite->turn_wait_ms = (rules->rotation * Core::Fixed32(1000)).ToInt();
    return std::abs(off()) * 2 <= step;
}

} // namespace ECS::Turning
