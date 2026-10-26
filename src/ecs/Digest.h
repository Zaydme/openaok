#pragma once
#include <cstdint>

#include <entt/entt.hpp>

// A digest of the world: every entity's identity, place, hit points and owner,
// in entity order, then each player's four stockpiles, the Market's prices and
// every player's stances, folded into one number. Two machines running the
// same game in lockstep compare these; the first turn they differ is the
// desync. The stockpiles and stances are in it because a tribute, a trade at
// the Market or a change of stance moves nothing else.
namespace ECS::Digest {

uint64_t Of(const entt::registry& registry);

} // namespace ECS::Digest
