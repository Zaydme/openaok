#include "hal/Platform.h"

#include <SDL2/SDL.h>

#ifdef __SWITCH__
#include <arpa/inet.h>
#include <switch.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#endif

namespace HAL::Platform {

#ifdef __SWITCH__

namespace {

// The folder the .nro is in, ending in a slash.
std::string g_root = "sdmc:/switch/openaok/";

// A message on the console's own text screen, until + is pressed.
void Tell(const char* message) {
    consoleInit(nullptr);
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    PadState pad;
    padInitializeDefault(&pad);
    std::printf("\n\n  OpenAoK\n\n  %s\n\n  Press + to return to the Homebrew Menu.\n", message);
    while (appletMainLoop()) {
        padUpdate(&pad);
        if (padGetButtonsDown(&pad) & HidNpadButton_Plus) break;
        consoleUpdate(nullptr);
    }
    consoleExit(nullptr);
}

bool Exists(const std::string& path) {
    struct stat info;
    return stat(path.c_str(), &info) == 0;
}

void Stop() { socketExit(); }

} // namespace

bool Start(int argc, char* argv[]) {
    if (argc > 0 && argv[0] != nullptr) {
        const std::string self = argv[0];
        const size_t slash = self.find_last_of('/');
        if (slash != std::string::npos) g_root = self.substr(0, slash + 1);
    }
    const AppletType type = appletGetAppletType();
    if (type != AppletType_Application && type != AppletType_SystemApplication) {
        Tell("OpenAoK needs all of the console's memory, which it does not have when\n"
             "  started from the Album. Hold R while starting any game, then choose\n"
             "  OpenAoK in the Homebrew Menu.");
        return false;
    }
    const std::string game = g_root + "game/";
    if (!Exists(game + "assets/extracted_data/rules.json")) {
        Tell("The game's data is not beside OpenAoK.nro. Make it on a computer from\n"
             "  your own CD with switch/package.py, and copy it here.");
        return false;
    }
    socketInitializeDefault();
    std::atexit(Stop);
    if (__nxlink_host.s_addr != 0) {
        nxlinkStdio();
    } else {
        const std::string log = g_root + "openaok.log";
        std::remove(log.c_str());
        std::freopen(log.c_str(), "a", stdout);
        std::freopen(log.c_str(), "a", stderr);
        std::setvbuf(stdout, nullptr, _IOLBF, 0);
    }
    const std::string run = game + "build";
    mkdir(run.c_str(), 0777);
    if (chdir(run.c_str()) != 0) {
        std::printf("[Platform] cannot enter %s\n", run.c_str());
        return false;
    }
    // The engine names its files from build/, as "../assets/...".
    if (!Exists("../assets/extracted_data/rules.json")) {
        std::printf("[Platform] %s cannot reach ../assets from %s\n", game.c_str(), run.c_str());
        return false;
    }
    return true;
}

std::string PrefPath(const char* kind) {
    const std::string dir = g_root + kind;
    mkdir(dir.c_str(), 0777);
    return Exists(dir) ? dir + "/" : std::string();
}

#else

bool Start(int, char*[]) { return true; }

std::string PrefPath(const char* kind) {
    char* base = SDL_GetPrefPath("OpenAoK", kind);
    if (base == nullptr) return std::string();
    std::string dir = base;
    SDL_free(base);
    return dir;
}

#endif

} // namespace HAL::Platform
