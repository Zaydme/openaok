#include "verify/Harness.h"

#include <cstdio>

#include "core/GameSetup.h"
#include "ecs/Simulation.h"

namespace Verify {

namespace {

// The colours a game begins with, from a lobby where the person in seat 1
// chose `chosen` (-1 for the seat's own) and three computer players sit in
// seats 2 to 4 in their seats' colours.
void Begin(int chosen, int out[4]) {
    Core::GameSetup setup = Core::DefaultSetup();
    setup.players[1].colour = chosen;
    for (int seat = 2; seat <= 4; ++seat) {
        setup.players[seat] = Core::PlayerSetup{true, seat, 0, true};
        setup.players[seat].colour = seat - 1;
    }
    ECS::Simulation sim;
    sim.Init(setup);
    for (int seat = 1; seat <= 4; ++seat) out[seat - 1] = sim.Setup().players[seat].colour;
}

} // namespace

// A computer player never plays in a person's colour: with the person in
// red, the first computer player takes blue and the others the next free
// ones; with the person in its seat's own, everyone keeps theirs.
bool CheckColours() {
    const char* verdict = nullptr;
    int red[4], own[4];
    Begin(1, red);
    Begin(-1, own);
    if (red[0] != 1) {
        verdict = "the person's chosen colour was not kept";
    } else if (red[1] == 1 || red[2] == 1 || red[3] == 1) {
        verdict = "a computer player plays in the person's colour";
    } else if (red[1] != 0 || red[2] != 2 || red[3] != 3) {
        verdict = "the computer players did not take the lowest free colours in seat order";
    } else if (own[0] != 0 || own[1] != 1 || own[2] != 2 || own[3] != 3) {
        verdict = "a seat's own colour changed with nobody choosing another";
    }
    std::printf("colours=%s\n", verdict == nullptr ? "ok" : verdict);
    std::printf("colours-figures red=%d,%d,%d,%d own=%d,%d,%d,%d\n", red[0], red[1], red[2], red[3],
                own[0], own[1], own[2], own[3]);
    return verdict == nullptr;
}

} // namespace Verify
