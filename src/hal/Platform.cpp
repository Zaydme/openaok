#include "hal/Platform.h"

#include <SDL2/SDL.h>

#ifdef __SWITCH__
#include <arpa/inet.h>
#include <switch.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <sstream>
#include <typeinfo>
#include <vector>
#include <zlib.h>
#endif

namespace HAL::Platform {

#ifdef __SWITCH__

namespace {

// The folder the .nro is in, ending in a slash.
std::string g_root = "sdmc:/switch/openaok/";
// Whether the network is open, for nxlink.
bool g_network = false;

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

// A file's CRC-32, or false when it cannot be read whole.
bool Checksum(const std::string& path, unsigned long& crc, long long& size) {
    FILE* file = std::fopen(path.c_str(), "rb");
    if (file == nullptr) return false;
    static unsigned char block[1 << 18];
    crc = crc32(0L, Z_NULL, 0);
    size = 0;
    size_t got = 0;
    while ((got = std::fread(block, 1, sizeof block, file)) > 0) {
        crc = crc32(crc, block, static_cast<uInt>(got));
        size += static_cast<long long>(got);
    }
    std::fclose(file);
    return true;
}

// The game's data against game/manifest.txt, which switch/package.py writes:
// every file's CRC-32, size and path. Read in full once after each copy, with
// its progress on the screen; the manifest's own time and size, kept in
// game/build/checked.txt, mark a copy already checked. False when a file is
// missing or damaged, after saying which.
bool CheckData(const std::string& game) {
    struct stat manifest_info;
    const std::string manifest = game + "manifest.txt";
    if (stat(manifest.c_str(), &manifest_info) != 0) return true;
    const std::string stamp_path = game + "build/checked.txt";
    std::ostringstream stamp;
    stamp << static_cast<long long>(manifest_info.st_mtime) << ' '
          << static_cast<long long>(manifest_info.st_size);
    {
        std::ifstream in(stamp_path);
        std::string last;
        if (std::getline(in, last) && last == stamp.str()) return true;
    }

    struct Entry {
        unsigned long crc;
        long long size;
        std::string path;
    };
    std::vector<Entry> entries;
    {
        std::ifstream in(manifest);
        std::string line;
        while (std::getline(in, line)) {
            std::istringstream row(line);
            Entry entry;
            row >> std::hex >> entry.crc >> std::dec >> entry.size;
            std::getline(row >> std::ws, entry.path);
            if (!entry.path.empty()) entries.push_back(entry);
        }
    }

    consoleInit(nullptr);
    std::printf("\n\n  OpenAoK\n\n  Checking the game's files, once after they are copied...\n\n");
    std::vector<std::string> bad;
    for (size_t i = 0; i < entries.size(); ++i) {
        unsigned long crc = 0;
        long long size = 0;
        if (!Checksum(game + entries[i].path, crc, size) || crc != entries[i].crc || size != entries[i].size) {
            bad.push_back(entries[i].path);
        }
        if (i % 16 == 0 || i + 1 == entries.size()) {
            std::printf("\r  %zu of %zu", i + 1, entries.size());
            consoleUpdate(nullptr);
        }
    }
    consoleExit(nullptr);
    if (bad.empty()) {
        mkdir((game + "build").c_str(), 0777);
        std::ofstream(stamp_path) << stamp.str() << '\n';
        return true;
    }
    std::string message = "These files are missing or damaged on the SD card:\n\n";
    for (size_t i = 0; i < bad.size() && i < 12; ++i) message += "    " + bad[i] + "\n";
    if (bad.size() > 12) message += "    and " + std::to_string(bad.size() - 12) + " more\n";
    message += "\n  Copy the folder to the card again with switch/package.py, then\n"
               "  turn the Switch off before taking the card out.";
    Tell(message.c_str());
    return false;
}

void Stop() {
    if (g_network) socketExit();
    g_network = false;
}

// An exception nothing caught: its name and message in the log before the
// game stops.
void Stopped() {
    if (std::exception_ptr thrown = std::current_exception()) {
        try {
            std::rethrow_exception(thrown);
        } catch (const std::exception& error) {
            std::fprintf(stderr, "[Platform] stopped by %s: %s\n", typeid(error).name(), error.what());
        } catch (...) {
            std::fprintf(stderr, "[Platform] stopped by an exception\n");
        }
    }
    // Ending here skips the exit handlers; the network must be closed, or
    // the Homebrew Menu cannot start again.
    Stop();
    std::abort();
}

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
    if (!CheckData(game)) return false;
    // The network is opened only for nxlink: a game that ends without closing
    // it leaves the Homebrew Menu unable to start.
    if (__nxlink_host.s_addr != 0) {
        socketInitializeDefault();
        g_network = true;
        std::atexit(Stop);
    }
    if (__nxlink_host.s_addr == 0 || nxlinkStdio() < 0) {
        const std::string log = g_root + "openaok.log";
        std::remove(log.c_str());
        std::freopen(log.c_str(), "a", stdout);
        std::freopen(log.c_str(), "a", stderr);
        // Line by line, and errors at once, so a game that stops short leaves
        // its last words in the log.
        std::setvbuf(stdout, nullptr, _IOLBF, 0);
        std::setvbuf(stderr, nullptr, _IONBF, 0);
    }
    std::set_terminate(Stopped);
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
