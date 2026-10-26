#pragma once
#include <SDL2/SDL.h>
#include "ecs/Simulation.h"

namespace Rendering {

class Renderer;

class EntityRenderer {
public:
    // The world's sprites come in two passes either side of the fog
    // (FogRenderer.h). This one gathers the frame's sprites and
    // draws the shadows and whatever lies on the ground -- the dat's layers 5
    // and under -- so the fog goes over them as over the ground.
    static void DrawGroundLevel(ECS::Simulation& sim, Renderer& renderer, SDL_Renderer* sdl_r, float alpha);
    // Everything standing, over the fog: whole, and dithered all over if it
    // stands on ground the player sees only in fog.
    static void DrawStanding(Renderer& renderer, SDL_Renderer* sdl_r);

    // The red arrows played once on the ground where a move is ordered
    // (graphic MOVEX), at world position (wx, wy).
    static void AddMoveMarker(float wx, float wy);
    // Forgets every marker, for a new world.
    static void ClearMarkers();

    // Flashes something's selection outline green for a second and a half:
    // what an order was given at, and what becomes the player's. An order's
    // flash ends any other; a conversion's leaves them be.
    static void Flash(const entt::registry& registry, entt::entity target, bool ends_others);
    static void ClearFlashes();

    // The frontmost thing whose art covers the point (vx, vy) of the view, in
    // the same unzoomed view pixels the frame was gathered in -- a building
    // (for any of its pieces) or a tree, wherever on it the point falls --
    // or null. Only art tall enough to keep an opaque mask answers; units
    // are picked by their distance from the click.
    static entt::entity PickAt(const entt::registry& registry, float vx, float vy);

    // How far a building's art stands from its origin tile, in world units:
    // a building is drawn about its footprint's middle, which for an even
    // footprint is half a tile up and left of the tile it was placed on.
    // Zero for anything that is not a building or a piece of one.
    static void ArtOffset(const entt::registry& registry, entt::entity entity, float& ox, float& oy);
};

} // namespace Rendering
