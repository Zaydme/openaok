#pragma once

namespace ECS::Components {

// A tree that has been felled: it lies as its dying graphic, the felled trunk,
// until its wood runs out. `shape` is which of that graphic's shapes it shows.
struct Felled {
    int shape = 0;
};

} // namespace ECS::Components
