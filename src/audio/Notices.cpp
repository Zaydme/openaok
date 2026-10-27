#include "audio/Notices.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <system_error>

#include "audio/InterfaceSounds.h"
#include "ecs/Chat.h"
#include "ecs/Flares.h"
#include "ecs/Triggers.h"
#include "hal/Audio.h"

namespace Audio::Notices {

namespace {

// The taunts by number, 1 to 30: each file's name starts with its two-digit
// number and a space ("01 Yes.mp3").
std::string g_taunts[ECS::Chat::kTaunts + 1];

// The last simulation tick whose chat and flares have been heard.
uint64_t g_heard = 0;
// The last of a scenario's display lines heard (their own tick count).
uint64_t g_heard_display = 0;
// Where a scenario's narration and sounds lie: Sound/scenario, beside the
// taunts' folder.
std::string g_scenario_sounds;

// A scenario's sound, by the name its trigger gives ("j1a"): the MP3 of that
// name in Sound/scenario, the names lower case as the disc has them.
void PlayScenarioSound(const std::string& name) {
    if (name.empty() || g_scenario_sounds.empty()) return;
    std::string file = name;
    std::transform(file.begin(), file.end(), file.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    HAL::Audio::PlayMp3(g_scenario_sounds + file + ".mp3", 1.0f);
}

} // namespace

void SetTauntFolder(const std::string& folder) {
    g_scenario_sounds =
        (std::filesystem::path(folder).parent_path() / "Sound" / "scenario").string() + "/";
    for (auto& path : g_taunts) path.clear();
    std::error_code error;
    for (const auto& entry : std::filesystem::directory_iterator(folder, error)) {
        const std::string name = entry.path().filename().string();
        if (name.size() < 3 || name[0] < '0' || name[0] > '9' || name[1] < '0' || name[1] > '9' ||
            name[2] != ' ') {
            continue;
        }
        const int number = (name[0] - '0') * 10 + (name[1] - '0');
        if (number >= 1 && number <= ECS::Chat::kTaunts) g_taunts[number] = entry.path().string();
    }
}

void Reset() {
    g_heard = 0;
    g_heard_display = 0;
}

void CatchUp(uint64_t tick) {
    g_heard = tick;
    g_heard_display = UINT64_MAX;   // until the display log is next read
}

std::string TauntFile(int taunt) {
    return taunt >= 1 && taunt <= ECS::Chat::kTaunts ? g_taunts[taunt] : std::string();
}

void Hear(const entt::registry& registry, const ECS::Events::Event& event,
          Core::PlayerId listener) {
    (void)registry;
    switch (event.kind) {
    case ECS::Events::Kind::Traded:
        if (event.player != listener) return;
        HAL::Audio::Play(event.amount > 0 ? InterfaceSound::kBuy : InterfaceSound::kSell,
                         1.0f, 0.0f);
        break;
    case ECS::Events::Kind::Tribute:
        if (event.player != listener) return;
        HAL::Audio::Play(InterfaceSound::kTribute, 1.0f, 0.0f);
        break;
    case ECS::Events::Kind::Claimed:
        if (event.player != listener) return;
        HAL::Audio::Play(InterfaceSound::kCapturedSheep, 1.0f, 0.0f);
        break;
    case ECS::Events::Kind::Exhausted:
        if (event.player != listener) return;
        HAL::Audio::Play(InterfaceSound::kFarmDied, 1.0f, 0.0f);
        break;
    case ECS::Events::Kind::PickedUp:
        if (event.player != listener) return;
        HAL::Audio::Play(InterfaceSound::kRelicPickedUp, 1.0f, 0.0f);
        break;
    default:
        break;
    }
}

void HearMessages(const entt::registry& registry, Core::PlayerId listener) {
    const ECS::Events::Log* events = ECS::Events::Read(registry);
    if (events == nullptr) return;
    // A new world starts its clock again.
    if (events->tick < g_heard) g_heard = 0;
    if (const ECS::Chat::Log* chat = ECS::Chat::Read(registry)) {
        for (const ECS::Chat::Line& line : chat->lines) {
            if (line.tick <= g_heard || !line.Heard(listener)) continue;
            if (line.taunt > 0) {
                const std::string file = TauntFile(line.taunt);
                if (!file.empty()) HAL::Audio::PlayMp3(file, 1.0f);
            } else if (line.from != listener) {
                HAL::Audio::Play(InterfaceSound::kChatReceived, 1.0f, 0.0f);
            }
        }
    }
    if (const ECS::Flares::Log* flares = ECS::Flares::Read(registry)) {
        for (const ECS::Flares::Flare& flare : flares->flares) {
            if (flare.tick <= g_heard || !flare.Seen(listener)) continue;
            HAL::Audio::Play(InterfaceSound::kFlare, 1.0f, 0.0f);
        }
    }
    g_heard = events->tick;

    // A scenario's sounds: what a Play Sound effect plays, and the narration
    // a Display Instructions or Send Chat effect carries -- each an MP3 of
    // Sound/scenario.
    if (const ECS::Triggers::Display* display = ECS::Triggers::ReadDisplay(registry)) {
        uint64_t latest = 0;
        for (const ECS::Triggers::Shown& line : display->lines) latest = std::max(latest, line.tick);
        // A loaded game's: old news. A new world's: its clock starts again.
        if (g_heard_display == UINT64_MAX) g_heard_display = latest;
        else if (latest < g_heard_display) g_heard_display = 0;
        for (const ECS::Triggers::Shown& line : display->lines) {
            if (line.tick <= g_heard_display) continue;
            if (line.player != 0 && line.player != listener) continue;
            switch (line.kind) {
            case ECS::Triggers::Show::Sound:
            case ECS::Triggers::Show::Instructions:
            case ECS::Triggers::Show::Chat: PlayScenarioSound(line.sound); break;
            default: break;
            }
        }
        g_heard_display = latest;
    }
}

} // namespace Audio::Notices
