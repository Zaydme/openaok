#pragma once

#include <cstdint>

#include <entt/entt.hpp>

#include "core/Rules.h"

namespace ECS::Felling {

// A tree is felled before it is cut, as the dat's hunt task has it: the
// lumberjack's task names a proceeding graphic, its felling stroke, and a
// working graphic, chopping the trunk on the ground. The felled tree is drawn
// as its dying graphic and keeps its wood; when the wood runs out it becomes
// its dead unit, the stump (ecs/Resources).

// Whether `target` is a tree still standing: one of the dat's trees (class
// 15) that has a felled graphic and has not been felled.
bool Standing(const entt::registry& registry, entt::entity target);

// How long the strokes that fell a tree take a lumberjack of `job`'s rules,
// in milliseconds: its attack's reload, for each stroke.
int32_t FellingMs(const Core::Rules::UnitRules* job);

// Lays `tree` down: from now on drawn as its dying graphic, in the shape its
// tile gives, without the standing tree's shadow.
void Fell(entt::registry& registry, entt::entity tree);

} // namespace ECS::Felling
