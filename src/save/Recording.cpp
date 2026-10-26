#include "save/Recording.h"

#include <cstring>
#include <fstream>
#include <iterator>

#include "core/CommandQueue.h"
#include "ecs/Digest.h"
#include "net/Wire.h"

namespace Save {

namespace {

constexpr char kMagic[8] = {'A', 'O', 'K', 'C', 'R', 'E', 'C', '1'};

} // namespace

void TakeTurn(Recording& recording, uint64_t tick) {
    auto& queue = Core::CommandQueue::Get();
    std::vector<Core::Command> commands = queue.PopAllCommands();
    if (!commands.empty()) recording.turns.push_back(Recording::Turn{tick, commands});
    for (const auto& command : commands) queue.PushCommand(command);
}

void Witness(Recording& recording, const ECS::Simulation& sim) {
    if (sim.Tick() % kCheckEvery != 0) return;
    recording.checks.push_back(Recording::Check{sim.Tick(), ECS::Digest::Of(sim.GetRegistry())});
}

void Replay(const Recording& recording, size_t& cursor, uint64_t tick) {
    auto& queue = Core::CommandQueue::Get();
    queue.PopAllCommands();
    while (cursor < recording.turns.size() && recording.turns[cursor].tick < tick) ++cursor;
    if (cursor < recording.turns.size() && recording.turns[cursor].tick == tick) {
        for (const auto& command : recording.turns[cursor].commands) queue.PushCommand(command);
        ++cursor;
    }
}

bool StillTheGame(const Recording& recording, size_t& cursor, const ECS::Simulation& sim) {
    while (cursor < recording.checks.size() && recording.checks[cursor].tick < sim.Tick()) ++cursor;
    if (cursor < recording.checks.size() && recording.checks[cursor].tick == sim.Tick()) {
        return recording.checks[cursor].digest == ECS::Digest::Of(sim.GetRegistry());
    }
    return true;
}

bool WriteRecording(const Recording& recording, const std::string& path) {
    Net::Wire::Writer w;
    w.bytes.insert(w.bytes.end(), std::begin(kMagic), std::end(kMagic));
    Net::Wire::Write(w, recording.setup);
    w.U8(recording.viewer);
    w.U32(static_cast<uint32_t>(recording.turns.size()));
    for (const auto& turn : recording.turns) {
        w.U64(turn.tick);
        w.U32(static_cast<uint32_t>(turn.commands.size()));
        for (const auto& command : turn.commands) Net::Wire::Write(w, command);
    }
    w.U32(static_cast<uint32_t>(recording.checks.size()));
    for (const auto& check : recording.checks) {
        w.U64(check.tick);
        w.U64(check.digest);
    }
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(w.bytes.data()),
              static_cast<std::streamsize>(w.bytes.size()));
    return static_cast<bool>(out);
}

bool ReadRecording(const std::string& path, Recording& recording) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)),
                                     std::istreambuf_iterator<char>());
    if (bytes.size() < sizeof(kMagic) || std::memcmp(bytes.data(), kMagic, sizeof(kMagic)) != 0) {
        return false;
    }
    Net::Wire::Reader r(bytes);
    r.at = sizeof(kMagic);
    recording = Recording{};
    if (!Net::Wire::Read(r, recording.setup)) return false;
    recording.viewer = r.U8();
    const uint32_t turns = r.U32();
    for (uint32_t i = 0; r.ok && i < turns; ++i) {
        Recording::Turn turn;
        turn.tick = r.U64();
        const uint32_t n = r.U32();
        for (uint32_t c = 0; r.ok && c < n; ++c) {
            Core::Command command;
            if (!Net::Wire::Read(r, command)) return false;
            turn.commands.push_back(command);
        }
        recording.turns.push_back(std::move(turn));
    }
    const uint32_t checks = r.U32();
    for (uint32_t i = 0; r.ok && i < checks; ++i) {
        Recording::Check check;
        check.tick = r.U64();
        check.digest = r.U64();
        recording.checks.push_back(check);
    }
    return r.ok;
}

} // namespace Save
