#pragma once
#include <SDL2/SDL.h>
#include <entt/entt.hpp>

#include "core/Types.h"
#include "ecs/components/Transform.h"

// The notices the minimap shows the player at this machine: five slots,
// walked by the last-notification key.
//
// A notice is a square on the minimap round where something happened that
// draws in and fades: two one-pixel outlines a pixel apart, 33 pixels across
// for the first 700 ms (palette index 71), then 23 (70) to 1000 ms, 17 (69) to
// 1300, 13 (68) to 1600, 9 (67) to 1900 and 5 (66) to 2400, then nothing --
// pale yellow running down to a dark orange, on the wall clock. It is drawn
// after the map's own objects and before the players'. Five at most, the
// oldest giving way; a notice about the thing the last one was about restarts
// that one where the thing now is.
//
// What raises one:
// * the attack alarm -- horn, bell or wolf -- and the conversion warning,
//   where the blow fell: Audio::Alarms decides when;
// * a flare the player is shown, where it went up;
// * a unit of the player's trained (notification 102), at the building that
//   trained it;
// * a building of the player's finished (105), walls aside;
// * a technology of the player's researched, an age among them (100), at the
//   building that researched it;
// * a unit of the player's converted away (123), where it stands.
// None of the notifications once the player is defeated.
//
// And, from the same news: what becomes the player's -- converted by its
// monks -- flashes green as an order's target does.
namespace Rendering::Alerts {

// A notice at a point of the world, about `subject` (null for none).
void Raise(entt::entity subject, const ECS::Components::Vector2D& at);

// Once a frame, after the sound has heard the frame's events: the news since
// the last.
void Update(const entt::registry& registry, Core::PlayerId viewer);

// On the minimap's well, projected as the map is.
void Draw(SDL_Renderer* sdl_r, const SDL_FRect& well, int map_w, int map_h);

// The last-notification key: where the notice it has reached is, the newest
// first and back from there, round again. False before any notice.
bool Next(ECS::Components::Vector2D& at);

// Forgets every notice, for a new world.
void Reset();

} // namespace Rendering::Alerts
