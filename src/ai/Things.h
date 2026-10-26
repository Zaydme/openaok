#pragma once
#include <entt/entt.hpp>

#include "ai/Census.h"
#include "ai/Language.h"
#include "ai/Mind.h"
#include "ai/World.h"
#include "core/Rules.h"
#include "core/Types.h"

// What a <unit>, <building> or <research-item> word means for one player now.
//
// A unit word names a member of a line; the player makes it from the line's
// first slot, which wears the member once the upgrades are researched -- so
// `train man-at-arms` trains slot 74 once slot 74 is a Man-at-Arms, and
// cannot before. A line word (`knight-line`) takes whatever the line's slot
// is by the upgrades researched. The ages are the player's civ table's own
// technologies, and my-unique-unit the unit its castle's elite technology
// upgrades.
namespace AI::Things {

// The civilisation's unique unit, its elite form and the technology between.
struct Unique {
    int unit = -1;
    int elite = -1;
    int tech = -1;
};
Unique UniqueOf(int civ);

// The line a unit or building word stands for, base first, and the member
// it names -- -1 for the whole line.
struct Line {
    int ids[Lang::kMaxLine] = {-1, -1, -1, -1, -1};
    int count = 0;
    int member = -1;
};
Line LineOf(const Lang::Thing& thing, int civ);

// The slot to train or build now for `thing`, or -1 when the player cannot
// make it at all now -- its civilisation lacks it, its technology is not
// researched, or its line has moved past it.
int Slot(const entt::registry& registry, const World& world, Core::PlayerId player,
         const Lang::Thing& thing, int civ);

// The technology a research word is for the player, or -1.
int Tech(const entt::registry& registry, Core::PlayerId player, const Lang::Thing& thing, int civ);

// Whether the player's stock -- less escrow unless `with_escrow` -- covers
// the paid slots of a price.
bool Affords(const Census& census, const Mind& mind, const Core::Rules::Cost* costs, int count,
             bool with_escrow);

// A finished building of `unit_id` with nothing queued, or null.
entt::entity IdleAt(const Census& census, int unit_id);

// The player's price for a technology.
const Core::Rules::Cost* TechCosts(const entt::registry& registry, Core::PlayerId player,
                                   int tech, int& count);

} // namespace AI::Things
