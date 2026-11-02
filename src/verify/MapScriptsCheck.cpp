#include "verify/Harness.h"

#include <cstdio>
#include <cstring>

#include "core/ConfigManager.h"
#include "core/GameSetup.h"

namespace Verify {

// Every map the lobby's Map Type list offers is played from its own random
// map script in gamedata.drs.
bool CheckMapScripts() {
    const char* missing = nullptr;
    for (const char* name : Core::kMapScripts) {
        if (missing == nullptr && Core::ConfigManager::Get().RmsScriptId(name) == 0) missing = name;
    }
    if (missing != nullptr) {
        std::printf("mapscripts=the lobby's \"%s\" finds no map script\n", missing);
        return false;
    }
    std::printf("mapscripts=ok\n");
    return true;
}

} // namespace Verify
