#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "core/Command.h"
#include "core/GameSetup.h"
#include "ecs/Simulation.h"

// Recorded games.
//
// The simulation is deterministic, so a recording holds only the lobby and the
// orders given in each tick. Playback builds the world from the lobby and
// hands each tick its orders; computer players think again and decide the
// same. Every kCheckEvery ticks the recording also notes the world's digest,
// so playback can tell when it diverges from the game recorded.
namespace Save {

struct Recording {
    Core::GameSetup setup;
    // Whose view it was recorded from: the one playback opens on.
    Core::PlayerId viewer = 1;
    struct Turn {
        uint64_t tick = 0;
        std::vector<Core::Command> commands;
    };
    std::vector<Turn> turns;
    struct Check {
        uint64_t tick = 0;
        uint64_t digest = 0;
    };
    std::vector<Check> checks;
};

// How often the digest is noted, in ticks.
constexpr uint64_t kCheckEvery = 256;

// The orders the tick about to run will execute -- what is in the command
// queue now. They are put back in the order they came, so the tick runs as
// it would have.
void TakeTurn(Recording& recording, uint64_t tick);
// After a tick: the digest, when one is due.
void Witness(Recording& recording, const ECS::Simulation& sim);

// Playback: puts the orders recorded for `tick` in the command queue, after
// throwing out anything the watcher's own input put there. `cursor` keeps
// the place between calls; start it at 0.
void Replay(const Recording& recording, size_t& cursor, uint64_t tick);
// Whether the world after `sim`'s latest tick is the one recorded, where the
// recording noted it; true between notes. `cursor` as for Replay.
bool StillTheGame(const Recording& recording, size_t& cursor, const ECS::Simulation& sim);

bool WriteRecording(const Recording& recording, const std::string& path);
bool ReadRecording(const std::string& path, Recording& recording);

} // namespace Save
