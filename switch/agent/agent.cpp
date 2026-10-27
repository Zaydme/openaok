// The test agent: OpenAoK run on a Switch from a computer, over Wi-Fi
// (switch/agent.py). It waits on a port for one command a connection:
//
//   PUT <size> <path>\n<bytes>   writes a file to the SD card
//   GET <path>\n                 answers <size>\n<bytes>
//   LIST <path>\n                answers <size> <d|f> <name>\n ... END\n
//   RUN <args>\n                 starts the game with <args>
//
// The game is started with --then naming the agent, so it hands back here
// when it ends, and with the computer's address for nxlink, so what it prints
// goes there.

#include <switch.h>

#include <arpa/inet.h>
#include <dirent.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

constexpr int kPort = 28290;
const char* kGame = "sdmc:/switch/openaok/OpenAoK.nro";

bool ReadExactly(int fd, void* buffer, size_t size) {
    auto* at = static_cast<char*>(buffer);
    while (size > 0) {
        const ssize_t got = recv(fd, at, size, 0);
        if (got <= 0) return false;
        at += got;
        size -= static_cast<size_t>(got);
    }
    return true;
}

bool WriteAll(int fd, const void* buffer, size_t size) {
    const auto* at = static_cast<const char*>(buffer);
    while (size > 0) {
        const ssize_t sent = send(fd, at, size, 0);
        if (sent <= 0) return false;
        at += sent;
        size -= static_cast<size_t>(sent);
    }
    return true;
}

bool Say(int fd, const std::string& text) { return WriteAll(fd, text.data(), text.size()); }

bool ReadLine(int fd, std::string& line) {
    line.clear();
    char c = 0;
    while (line.size() < 4096) {
        if (recv(fd, &c, 1, 0) != 1) return false;
        if (c == '\n') return true;
        line += c;
    }
    return false;
}

// Every folder on the way to `path`, as mkdir -p makes them.
void MakeFolders(const std::string& path) {
    for (size_t slash = path.find('/', 6); slash != std::string::npos; slash = path.find('/', slash + 1)) {
        mkdir(path.substr(0, slash).c_str(), 0777);
    }
}

void Put(int fd, size_t size, const std::string& path) {
    MakeFolders(path);
    const std::string part = path + ".part";
    FILE* file = std::fopen(part.c_str(), "wb");
    if (file == nullptr) {
        Say(fd, "ERR cannot write " + path + "\n");
        return;
    }
    static char block[1 << 16];
    bool ok = true;
    while (size > 0 && ok) {
        const size_t want = size < sizeof block ? size : sizeof block;
        ok = ReadExactly(fd, block, want) && std::fwrite(block, 1, want, file) == want;
        size -= want;
    }
    std::fclose(file);
    if (!ok) {
        std::remove(part.c_str());
        Say(fd, "ERR transfer\n");
        return;
    }
    std::remove(path.c_str());
    Say(fd, std::rename(part.c_str(), path.c_str()) == 0 ? "OK\n" : "ERR rename\n");
}

void Get(int fd, const std::string& path) {
    FILE* file = std::fopen(path.c_str(), "rb");
    struct stat info;
    if (file == nullptr || stat(path.c_str(), &info) != 0) {
        if (file != nullptr) std::fclose(file);
        Say(fd, "ERR no such file\n");
        return;
    }
    Say(fd, std::to_string(static_cast<long long>(info.st_size)) + "\n");
    static char block[1 << 16];
    size_t got = 0;
    while ((got = std::fread(block, 1, sizeof block, file)) > 0 && WriteAll(fd, block, got)) {
    }
    std::fclose(file);
}

void List(int fd, const std::string& path) {
    DIR* dir = opendir(path.c_str());
    if (dir == nullptr) {
        Say(fd, "ERR no such folder\n");
        return;
    }
    while (dirent* entry = readdir(dir)) {
        const std::string name = entry->d_name;
        if (name == "." || name == "..") continue;
        struct stat info;
        const std::string full = path + (path.back() == '/' ? "" : "/") + name;
        if (stat(full.c_str(), &info) != 0) continue;
        Say(fd, std::to_string(static_cast<long long>(info.st_size)) + (S_ISDIR(info.st_mode) ? " d " : " f ") +
                    name + "\n");
    }
    closedir(dir);
    Say(fd, "END\n");
}

// The game's command line: itself, the options asked for, the way back here,
// and nxlink's address of the computer, which libnx takes off the end.
std::string GameArguments(const std::string& options, const std::string& self, in_addr computer) {
    char host[32];
    std::snprintf(host, sizeof host, "%08x_NXLINK_", static_cast<unsigned>(computer.s_addr));
    return std::string("\"") + kGame + "\" " + options + " --then \"" + self + "\" " + host;
}

} // namespace

int main(int argc, char* argv[]) {
    consoleInit(nullptr);
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    PadState pad;
    padInitializeDefault(&pad);
    appletSetAutoSleepDisabled(true);
    socketInitializeDefault();
    const std::string self = argc > 0 && argv[0] != nullptr ? argv[0] : "sdmc:/switch/openaok-agent.nro";

    const int server = socket(AF_INET, SOCK_STREAM, 0);
    const int yes = 1;
    setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(kPort);
    address.sin_addr.s_addr = INADDR_ANY;
    const bool listening = bind(server, reinterpret_cast<sockaddr*>(&address), sizeof address) == 0 &&
                           listen(server, 2) == 0;
    fcntl(server, F_SETFL, fcntl(server, F_GETFL, 0) | O_NONBLOCK);
    in_addr own{};
    own.s_addr = gethostid();
    std::printf("\n  OpenAoK test agent\n\n  %s on %s:%d\n\n  Press + to leave.\n",
                listening ? "Waiting" : "Cannot listen", inet_ntoa(own), kPort);
    consoleUpdate(nullptr);

    bool run = false;
    while (appletMainLoop() && !run) {
        padUpdate(&pad);
        if (padGetButtonsDown(&pad) & HidNpadButton_Plus) break;
        sockaddr_in peer{};
        socklen_t peer_size = sizeof peer;
        const int client = accept(server, reinterpret_cast<sockaddr*>(&peer), &peer_size);
        if (client < 0) {
            svcSleepThread(20'000'000);
            continue;
        }
        fcntl(client, F_SETFL, fcntl(client, F_GETFL, 0) & ~O_NONBLOCK);
        std::string line;
        if (ReadLine(client, line)) {
            if (line.rfind("PUT ", 0) == 0) {
                const size_t space = line.find(' ', 4);
                if (space != std::string::npos) {
                    const std::string path = line.substr(space + 1);
                    std::printf("  put %s\n", path.c_str());
                    Put(client, std::strtoull(line.c_str() + 4, nullptr, 10), path);
                }
            } else if (line.rfind("GET ", 0) == 0) {
                Get(client, line.substr(4));
            } else if (line.rfind("LIST ", 0) == 0) {
                List(client, line.substr(5));
            } else if (line.rfind("RUN", 0) == 0) {
                const std::string options = line.size() > 4 ? line.substr(4) : std::string();
                const std::string arguments = GameArguments(options, self, peer.sin_addr);
                run = R_SUCCEEDED(envSetNextLoad(kGame, arguments.c_str()));
                Say(client, run ? "OK\n" : "ERR cannot start the game\n");
                std::printf("  run %s\n", options.c_str());
            } else {
                Say(client, "ERR unknown command\n");
            }
            consoleUpdate(nullptr);
        }
        close(client);
    }
    close(server);
    socketExit();
    consoleExit(nullptr);
    return 0;
}
