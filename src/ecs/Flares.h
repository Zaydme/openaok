#pragma once
#include <cstdint>
#include <vector>

#include <entt/entt.hpp>

#include "core/Types.h"
#include "ecs/components/Transform.h"

// Signalling allies: the point a player clicks on the map or the minimap
// shows on its allies' minimaps as a flashing X, with a notification sound.
//
// An order, carried in lockstep like chat, and like chat written to a log the
// simulation never reads back (ecs/Chat): the minimap draws it and the sound
// plays flare.wav. Who sees it is the sender and every player it regards as
// an ally, decided as it is carried out.
//
// How long it shows is the dat's: its FLARE unit (112) holds 7 of resource 12
// -- the decay time -- and decays at 1 a second. Nothing is drawn on the
// field: that unit's graphic, 1751, is BLANK, an SLP-less graphic.
namespace ECS::Flares {

struct Flare {
    uint64_t tick = 0;        // the tick it went up
    Core::PlayerId from = 0;
    uint16_t to = 0;          // a bit per player who sees it
    Components::Vector2D at{};

    bool Seen(Core::PlayerId player) const { return (to >> player) & 1u; }
};

struct Log {
    // How long a flare shows, from the dat; the default is that figure.
    int32_t lifetime_ms = 7000;
    std::vector<Flare> flares;
};

void Setup(entt::registry& registry);

// Sends up `from`'s flare at a point. False from a player who is out of the
// game.
bool Send(entt::registry& registry, Core::PlayerId from, const Components::Vector2D& at);

// The log; null if the world was built without one.
const Log* Read(const entt::registry& registry);

// Whether a flare is still showing at `tick`.
bool Showing(const Log& log, const Flare& flare, uint64_t tick);

} // namespace ECS::Flares
