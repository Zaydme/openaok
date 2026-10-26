#include "verify/Harness.h"

#include <cstdio>
#include <string>
#include <vector>

#include <filesystem>

#include "core/CommandQueue.h"
#include "core/ConfigManager.h"
#include "core/GameSetup.h"
#include "ecs/Digest.h"
#include "ecs/Simulation.h"
#include "ecs/components/Identity.h"
#include "ecs/components/Owner.h"
#include "ecs/components/Transform.h"
#include "save/Recording.h"
#include "save/Save.h"

namespace Verify {

namespace {

constexpr int kTicksPerSecond = 1000 / 16;

// Two computer players, so the world is busy -- villagers on their errands,
// buildings going up, units in training, the random stream in use.
Core::GameSetup BusySetup() {
    Core::GameSetup setup = Core::DefaultSetup();
    for (int seat = 1; seat <= 2; ++seat) setup.players[seat].computer = true;
    return setup;
}

// The first of `player`'s entities that is the dat unit `name`.
entt::entity FirstOf(const entt::registry& reg, Core::PlayerId player, const char* name) {
    const int id = Core::ConfigManager::Get().GetUnitIdByName(name);
    auto view = reg.view<const ECS::Components::Identity, const ECS::Components::Owner>();
    for (auto e : view) {
        if (view.get<const ECS::Components::Identity>(e).unit_id == id &&
            view.get<const ECS::Components::Owner>(e).player_id == player) {
            return e;
        }
    }
    return entt::null;
}

} // namespace

// A game recorded -- a person's orders on one side, the computer on the
// other -- and played back from the file is the same game at every digest the
// recording noted; played back without its orders, it is not.
bool CheckRecording() {
    const char* verdict = "ok";
    auto ok = [&]() { return std::string(verdict) == "ok"; };
    Core::GameSetup setup = Core::DefaultSetup();
    setup.players[2].computer = true;

    Save::Recording recording;
    recording.setup = setup;
    ECS::Simulation played;
    played.Init(setup);
    auto& reg = played.GetRegistry();
    const entt::entity tc = FirstOf(reg, 1, "RTWC");
    const entt::entity scout = FirstOf(reg, 1, "SCOUT");
    const int villager = Core::ConfigManager::Get().GetUnitIdByName("VMBAS");
    auto& queue = Core::CommandQueue::Get();
    queue.PopAllCommands();
    for (int t = 0; t < 40 * kTicksPerSecond; ++t) {
        if (t == 30) {
            for (int n = 0; n < 3; ++n) queue.PushCommand(Core::TrainCommand{tc, villager, 1});
        }
        if (t == 200 && reg.valid(scout)) {
            const auto& at = reg.get<ECS::Components::Transform>(scout).position;
            queue.PushCommand(Core::MoveCommand{scout, at.x + Core::Fixed32(12),
                                                at.y - Core::Fixed32(9), 1});
        }
        Save::TakeTurn(recording, played.Tick() + 1);
        played.Update(16);
        Save::Witness(recording, played);
    }
    if (recording.turns.size() < 2 || recording.checks.size() < 5) {
        verdict = "the recording caught too little to test";
    }

    const auto path = std::filesystem::temp_directory_path() / "openaok_verify.mgl";
    Save::Recording read;
    if (ok() && (!Save::WriteRecording(recording, path.string()) ||
                 !Save::ReadRecording(path.string(), read))) {
        verdict = "the recording did not survive its file";
    }
    std::filesystem::remove(path);

    // Played back, with its orders and without.
    for (int pass = 0; pass < 2 && ok(); ++pass) {
        const bool with_orders = pass == 0;
        ECS::Simulation replay;
        replay.Init(read.setup);
        size_t turn = 0, check = 0;
        bool same = true;
        for (int t = 0; t < 40 * kTicksPerSecond; ++t) {
            if (with_orders) {
                Save::Replay(read, turn, replay.Tick() + 1);
            } else {
                queue.PopAllCommands();
            }
            replay.Update(16);
            same = same && Save::StillTheGame(read, check, replay);
        }
        if (with_orders && !same) verdict = "the recording played back is not the game recorded";
        if (with_orders && ECS::Digest::Of(replay.GetRegistry()) != ECS::Digest::Of(reg)) {
            verdict = "the recording played back ended somewhere else";
        }
        if (!with_orders && same) verdict = "playback without its orders still passed as the game";
    }
    queue.PopAllCommands();
    std::printf("recording=%s\n", verdict);
    return ok();
}

// A game saved part way through, loaded into a fresh world and run on, stays
// the game that never stopped, digest for digest; and a save the world it
// restores does not match is refused.
bool CheckSave() {
    const char* verdict = "ok";
    auto ok = [&]() { return std::string(verdict) == "ok"; };

    ECS::Simulation original;
    original.Init(BusySetup());
    for (int t = 0; t < 90 * kTicksPerSecond; ++t) original.Update(16);

    // Everything in the registry is something a save carries.
    const auto missing = Save::Uncovered(original.GetRegistry());
    if (!missing.empty()) {
        std::printf("save=a component the save does not carry: %s\n", missing.front().c_str());
        return false;
    }

    std::vector<uint8_t> bytes;
    if (!Save::Write(original, bytes)) verdict = "the world could not be written";

    ECS::Simulation loaded;
    std::string error;
    if (ok() && !Save::Read(loaded, bytes, &error)) {
        std::printf("save=a good save was refused: %s\n", error.c_str());
        return false;
    }
    if (ok() && loaded.Tick() != original.Tick()) verdict = "the clock came back wrong";
    if (ok() && ECS::Digest::Of(loaded.GetRegistry()) != ECS::Digest::Of(original.GetRegistry())) {
        verdict = "the loaded world is not the saved one";
    }

    // Both run on; they must not part company.
    for (int t = 0; ok() && t < 60 * kTicksPerSecond; ++t) {
        original.Update(16);
        loaded.Update(16);
        if (t % 50 == 0 &&
            ECS::Digest::Of(loaded.GetRegistry()) != ECS::Digest::Of(original.GetRegistry())) {
            verdict = "the loaded game drifted from the one that never stopped";
        }
    }
    if (ok()) {
        const auto& a = original.GetGrid().Tiles();
        const auto& b = loaded.GetGrid().Tiles();
        for (size_t i = 0; i < a.size() && ok(); ++i) {
            if (a[i].terrain != b[i].terrain || a[i].is_walkable != b[i].is_walkable ||
                a[i].blocked != b[i].blocked || a[i].passable != b[i].passable) {
                verdict = "the loaded map drifted from the one that never stopped";
            }
        }
    }

    // A save whose world does not come back as the digest it carries says is
    // refused; so is one cut short, and one from somewhere else.
    if (ok()) {
        std::vector<uint8_t> tampered = bytes;
        tampered[tampered.size() - 3] ^= 0x5A;
        ECS::Simulation bad;
        if (Save::Read(bad, tampered, nullptr)) verdict = "a damaged save was taken as good";
    }
    if (ok()) {
        std::vector<uint8_t> short_save(bytes.begin(), bytes.begin() + static_cast<long>(bytes.size() / 3));
        ECS::Simulation bad;
        if (Save::Read(bad, short_save, nullptr)) verdict = "a save cut short was taken as good";
    }
    if (ok()) {
        std::vector<uint8_t> other(bytes);
        other[0] = 'X';
        ECS::Simulation bad;
        if (Save::Read(bad, other, nullptr)) verdict = "something not a save was taken as one";
    }

    std::printf("save=%s\n", verdict);
    return ok();
}

} // namespace Verify
