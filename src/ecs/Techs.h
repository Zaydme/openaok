#pragma once
#include <vector>

#include <entt/entt.hpp>

#include "core/Types.h"

// Where each player stands with each technology.
//
// The dat drives all of it. A technology has a list of required technologies
// and a count of how many of them must be done; one that no building researches
// is researched *automatically* the moment that count is met. That one rule
// builds the whole progression out of data:
//
//   - a finished building researches its own "shadow" technology (a Mill
//     researches "Shadow Mill -- Age One");
//   - "Shadow Node for Age Two" needs two of those, so it completes itself when
//     the second qualifying building goes up;
//   - the Feudal Age needs the Dark Age and that node, so the Town Center
//     offers it only after two Dark Age buildings stand;
//   - "Barracks (make avail)" needs the Dark Age, so it completes at the start
//     and its effect enables the Barracks.
//
// A technology with nothing listed and a count of zero has nothing to wait for
// and completes at the start: the Dark Age itself, and each civilisation's own
// bonuses. One with nothing listed and a count of one can only be triggered.
namespace ECS::Techs {

// Starts a player off: applies its civ's tech tree, researches everything that
// starts researched, then advances it to the lobby's starting age (0 for the
// Dark Age). With the lobby's All Techs the tech tree is not applied: every
// unit, building and technology is open to every civilisation -- while each
// civilisation's own bonuses and unique technologies stay its own, the dat
// tying those to their civ (TechRules::civ, ecs/Players).
void Start(entt::registry& registry, Core::PlayerId player, int starting_age,
           bool all_techs = false);

// Whether the player has researched `tech_id`.
bool Researched(const entt::registry& registry, Core::PlayerId player, int tech_id);

// Whether the player meets `tech_id`'s requirements and it is neither done,
// disabled, nor already being researched.
bool Available(const entt::registry& registry, Core::PlayerId player, int tech_id);

// The technologies `building_unit_id` offers the player right now, in id
// order. Drawing the command panel and queueing research both ask this.
std::vector<int> ResearchableAt(const entt::registry& registry, Core::PlayerId player,
                                int building_unit_id);

// Completes a technology: marks it done, applies its effect, then researches
// whatever that unlocks automatically. `by` is the building that researched
// it; null for one that comes of itself or from a scenario.
void Complete(entt::registry& registry, Core::PlayerId player, int tech_id,
              entt::entity by = entt::null);

// A building of `unit_id` has been finished: research its own technology, if
// it has one the player has not.
void BuildingFinished(entt::registry& registry, Core::PlayerId player, int unit_id);

// The player's age, 0 for the Dark Age: the dat's "current age" resource.
int Age(const entt::registry& registry, Core::PlayerId player);

// The technology that is age `age` (0 Dark .. 3 Imperial) for the player, as
// its civ table names it, or -1.
int AgeTech(const entt::registry& registry, Core::PlayerId player, int age);

// Whether `tech_id` is Spies -- Treason in a Regicide game: its effect sets
// the dat's resource 183, the sight of the player's enemies.
bool Spies(int tech_id);

// The next age the player can advance to, as a technology, or -1 once it is in
// the last one. The Town Center shows this button from the start, greyed
// until its requirements are met, unlike other technologies.
int NextAgeTech(const entt::registry& registry, Core::PlayerId player);

} // namespace ECS::Techs
