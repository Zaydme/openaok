#pragma once
#include <cstdint>

namespace ECS::Components {

// One thing a building has been told to make: a unit to train or a
// technology to research. Both share the one queue, so a Town Center
// researching Loom trains no villagers until it is done.
struct QueueItem {
    int id = -1;          // dat unit id, or dat tech id when `tech`
    bool tech = false;
};

// What a building has been told to make, in order. A fixed array, inline
// with the building: a queue is short and bounded.
struct ProductionQueue {
    // How many items one building's queue holds.
    static constexpr int kCapacity = 15;

    QueueItem items[kCapacity] = {};   // front first
    int count = 0;

    // How far the front item is through, in milliseconds of simulation time.
    // The dat states train and research times in whole seconds.
    int32_t progress_ms = 0;

    // The front item has started. For a unit, population is booked from this
    // moment, so two buildings cannot each start a unit into the same last
    // free slot.
    bool training = false;

    // The front unit is waiting for population room: "you need more houses".
    bool housed = false;
};

} // namespace ECS::Components
