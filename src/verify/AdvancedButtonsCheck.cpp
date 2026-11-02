#include "verify/Harness.h"

#include <cstdio>
#include <cstring>

#include "audio/Notices.h"
#include "audio/SoundTable.h"
#include "core/GameSetup.h"
#include "ecs/Simulation.h"
#include "ecs/Triggers.h"
#include "editor/EditorState.h"
#include "rendering/HudLayout.h"
#include "rendering/Messages.h"
#include "rendering/UIRenderer.h"
#include "scenario/Scenario.h"

namespace Verify {

namespace {

constexpr int kTicksPerSecond = 1000 / 16;

} // namespace

// A scenario whose triggers, with no conditions, Use Advanced Buttons and Play
// Sound with a dat sound and no file: the advanced commands, turned off, are
// on once the player's interface reads the scenario's display, and the sound
// played is the dat sound's file in the player's voice.
bool CheckAdvancedButtons() {
    const char* verdict = "ok";
    Scenario::File file = Editor::NewFile();
    Scenario::Trigger buttons;
    Scenario::Effect use;
    use.type = Scenario::EffectType::UseAdvancedButtons;
    buttons.effects.push_back(use);
    buttons.effect_order.push_back(0);
    file.triggers.push_back(buttons);
    // The first dat sound with a file to play.
    int sound_id = -1;
    for (int id = 0; id < 1000 && sound_id < 0; ++id) {
        if (Audio::SoundTable::Pick(id, 1) >= 0) sound_id = id;
    }
    Scenario::Trigger sound;
    Scenario::Effect play;
    play.type = Scenario::EffectType::PlaySound;
    play.player = 1;
    play.sound_id = sound_id;
    sound.effects.push_back(play);
    sound.effect_order.push_back(0);
    file.triggers.push_back(sound);

    Core::GameSetup setup = Core::DefaultSetup();
    Scenario::SetupFor(file, setup);
    ECS::Simulation sim;
    sim.InitFromFile(setup, file);
    if (Rendering::UIRenderer::AdvancedCommands()) {
        // The player's own choice: off before the scenario turns them on.
        Rendering::UIRenderer::ActivateMinimapButton(Rendering::kAdvancedCommands);
    }
    const bool off_before = !Rendering::UIRenderer::AdvancedCommands();
    for (int t = 0; t < 2 * kTicksPerSecond; ++t) sim.Update(16);
    Rendering::Messages::Update(sim.GetRegistry(), 1);
    const bool on_after = Rendering::UIRenderer::AdvancedCommands();
    bool sound_heard = false;
    if (const ECS::Triggers::Display* display = ECS::Triggers::ReadDisplay(sim.GetRegistry())) {
        for (const auto& line : display->lines) {
            if (line.kind != ECS::Triggers::Show::Sound) continue;
            sound_heard |= Audio::Notices::TriggerSoundFile(sim.GetRegistry(), line, 1) ==
                           Audio::SoundTable::Pick(sound_id, 1);
        }
    }
    if (!off_before) {
        verdict = "the advanced commands could not be turned off";
    } else if (!on_after) {
        verdict = "Use Advanced Buttons did not turn the advanced commands on";
    } else if (sound_id < 0) {
        verdict = "the dat has no sound to play";
    } else if (!sound_heard) {
        verdict = "Play Sound with no file did not play its dat sound";
    }
    std::printf("advancedbuttons=%s\n", verdict);
    return std::strcmp(verdict, "ok") == 0;
}

} // namespace Verify
