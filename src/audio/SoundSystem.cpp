#include "audio/SoundSystem.h"

#include "audio/Alarms.h"
#include "audio/Notices.h"
#include "audio/InterfaceSounds.h"
#include "audio/Voices.h"
#include "audio/WorldSounds.h"
#include "ecs/Events.h"
#include "hal/Audio.h"

#include <filesystem>

namespace Audio {

namespace {

// The last simulation tick whose events have been heard.
uint64_t g_heard = 0;

} // namespace

bool Start(const std::string& sounds_drs, const HAL::DrsArchive* interface_archive) {
    if (!HAL::Audio::Open()) return false;
    HAL::Audio::OpenArchive(sounds_drs);
    if (interface_archive != nullptr) HAL::Audio::UseArchive(*interface_archive);
    WorldSounds::Reset();
    Alarms::Reset();
    Notices::Reset();
    // The taunts lie loose beside the archives: the Taunt folder in
    // Data/sounds.drs's parent.
    Notices::SetTauntFolder(
        (std::filesystem::path(sounds_drs).parent_path().parent_path() / "Taunt").string());
    g_heard = 0;
    return true;
}

void Stop() {
    HAL::Audio::Close();
}

void CatchUp(const entt::registry& registry) {
    if (const ECS::Events::Log* log = ECS::Events::Read(registry)) {
        g_heard = log->tick;
        Notices::CatchUp(log->tick);
    }
}

void Update(const entt::registry& registry, const Listener& listener, Core::PlayerId viewer) {
    WorldSounds::Update(registry, listener, viewer);
    if (const ECS::Events::Log* log = ECS::Events::Read(registry)) {
        // A new world starts its clock again.
        if (log->tick < g_heard) g_heard = 0;
        for (const ECS::Events::Event& event : log->events) {
            if (event.tick <= g_heard) continue;
            Alarms::Hear(registry, event, viewer);
            Voices::Hear(registry, event, viewer);
            Notices::Hear(registry, event, viewer);
            // A gate of the viewer's locked or unlocked: gatel.wav, gateu.wav.
            if (event.player == viewer && (event.kind == ECS::Events::Kind::GateLocked ||
                                           event.kind == ECS::Events::Kind::GateUnlocked)) {
                HAL::Audio::Play(event.kind == ECS::Events::Kind::GateLocked
                                     ? InterfaceSound::kGateLocked
                                     : InterfaceSound::kGateUnlocked,
                                 1.0f, 0.0f);
            }
        }
        g_heard = log->tick;
    }
    Notices::HearMessages(registry, viewer);
}

} // namespace Audio
