#include "verify/Harness.h"

#include <cstdio>
#include <string>

#include "core/ConfigManager.h"
#include "ecs/ScenarioWorld.h"
#include "ecs/Simulation.h"
#include "ecs/components/Selectable.h"
#include "rendering/UIRenderer.h"

namespace Verify {

namespace {

using UI = Rendering::UIRenderer;
constexpr int kBuildingsHelp = 41061;   // "Buildings": the villager's economic build menu
constexpr int kHouse = 70;

bool EndsWith(const std::string& text, const std::string& end) {
    return text.size() >= end.size() && text.compare(text.size() - end.size(), end.size(), end) == 0;
}

} // namespace

// The rollover help over a command button ends with the key that presses it,
// as the original's does: a villager's Buildings "(Hotkey: B)", and in that
// menu the House "(Hotkey: E)".
bool CheckHelp() {
    const char* verdict = nullptr;
    ECS::Simulation sim;
    sim.Init();
    auto& reg = sim.GetRegistry();
    auto& cfg = Core::ConfigManager::Get();
    sim.Update(16);
    const auto [sx, sy] = sim.player_starts[0];
    const entt::entity villager = ECS::ScenarioWorld::Place(
        reg, sim.GetGrid(), cfg.GetUnitIdByName("VMBAS"), 1,
        {Core::Fixed32(sx + 4), Core::Fixed32(sy + 4)}, {}, 0);
    std::string buildings, house;
    if (villager == entt::null) {
        verdict = "no villager placed";
    } else {
        auto all = reg.view<ECS::Components::Selectable>();
        for (auto e : all) all.get<ECS::Components::Selectable>(e).is_selected = e == villager;
        auto help = [&](auto match) {
            UI::Command cmds[UI::kCommandSlots];
            const int n = UI::BuildCommands(sim, 1024, 768, cmds, UI::kCommandSlots);
            for (int i = 0; i < n; ++i) {
                if (match(cmds[i])) return UI::CommandHelp(sim, cmds[i]);
            }
            return std::string();
        };
        buildings = help([](const UI::Command& c) { return c.name_string == kBuildingsHelp; });
        UI::OpenBuildMenu(sim, Core::ConfigManager::kInterfaceEconomic);
        house = help([](const UI::Command& c) {
            return c.kind == UI::CommandKind::Produce && c.places_building && c.unit_id == kHouse;
        });
        if (buildings.empty() || house.empty()) {
            verdict = "no help for the Buildings or the House button";
        } else if (!EndsWith(buildings, "(Hotkey: B)") || !EndsWith(house, "(Hotkey: E)")) {
            verdict = "a button's help does not end with its hotkey";
        }
    }
    std::printf("help=%s\n", verdict == nullptr ? "ok" : verdict);
    return verdict == nullptr;
}

} // namespace Verify
