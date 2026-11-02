#include "verify/Harness.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "core/GameSetup.h"
#include "ecs/Cheats.h"
#include "ecs/Simulation.h"
#include "save/Save.h"

namespace Verify {

// A game with cheats allowed, player 1 with "aegis" on and player 2 with
// "natural wonders", saved and loaded: both still on, for those players only.
bool CheckSaveCheats() {
    const char* verdict = "ok";
    Core::GameSetup setup = Core::DefaultSetup();
    setup.cheats = true;
    ECS::Simulation original;
    original.Init(setup);
    original.Update(16);
    auto& reg = original.GetRegistry();
    if (!ECS::Cheats::Apply(reg, original.GetGrid(), 1, ECS::Cheats::kAegis) ||
        !ECS::Cheats::Apply(reg, original.GetGrid(), 2, ECS::Cheats::kNaturalWonders)) {
        verdict = "the cheats were refused";
    }
    std::vector<uint8_t> bytes;
    if (std::strcmp(verdict, "ok") == 0 && !Save::Write(original, bytes)) verdict = "the world could not be written";
    ECS::Simulation loaded;
    std::string error;
    if (std::strcmp(verdict, "ok") == 0 && !Save::Read(loaded, bytes, &error)) verdict = "the save was refused";
    if (std::strcmp(verdict, "ok") == 0) {
        const auto& back = loaded.GetRegistry();
        if (!ECS::Cheats::Aegis(back, 1) || !ECS::Cheats::CommandsNature(back, 2)) {
            verdict = "a cheat left on was off after loading";
        } else if (ECS::Cheats::Aegis(back, 2) || ECS::Cheats::CommandsNature(back, 1)) {
            verdict = "a cheat came back on for the wrong player";
        }
    }
    std::printf("savecheats=%s\n", verdict);
    return std::strcmp(verdict, "ok") == 0;
}

} // namespace Verify
