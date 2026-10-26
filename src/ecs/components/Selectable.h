#pragma once

namespace ECS::Components {

struct Selectable {
    bool is_selected = false;
    // Radius for click detection in logical grid units
    int radius = 1;
    // Scenery is pointed at, never swept up.
    //
    // A tree, a gold mine and a sheep are all selectable -- the player clicks
    // one to see what it is and how much is left in it -- but dragging a box
    // across a forest must hand back the villagers in it, not the trees. Zero
    // marks something a click may pick and a box must ignore; it also loses a
    // tie under the cursor, so clicking a villager standing on grass picks the
    // villager.
    int priority = 1;
    // The control group it was put in with Ctrl and a number key, 1 to 9, or
    // 0 for none. A unit belongs to one group at most, and the number is
    // drawn beside it while it is selected. Interface state, like the
    // selection itself: nothing in the simulation reads it.
    int group = 0;
};

} // namespace ECS::Components
