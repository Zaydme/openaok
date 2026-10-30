#include "verify/Harness.h"

#include <cstdio>

#include "core/Command.h"
#include "core/ConfigManager.h"
#include "core/Hotkeys.h"
#include "ecs/Garrisons.h"
#include "ecs/Orders.h"
#include "ecs/ScenarioWorld.h"
#include "ecs/Simulation.h"
#include "ecs/components/RallyPoint.h"
#include "ecs/components/Relic.h"
#include "ecs/components/Selectable.h"
#include "ecs/components/Transform.h"
#include "rendering/UIRenderer.h"

namespace Verify {

namespace {

using Kind = Rendering::UIRenderer::CommandKind;
constexpr Core::PlayerId kUs = 1;

ECS::Components::Vector2D At(int x, int y) {
    return {Core::Fixed32(x) + Core::Fixed32(1) / Core::Fixed32(2),
            Core::Fixed32(y) + Core::Fixed32(1) / Core::Fixed32(2)};
}

// The one thing selected.
void Select(entt::registry& reg, entt::entity only) {
    auto all = reg.view<ECS::Components::Selectable>();
    for (auto e : all) all.get<ECS::Components::Selectable>(e).is_selected = e == only;
}

// The button of `kind` the panel offers for what is selected, if any, and
// whether `key` presses it.
struct Found {
    bool offered = false;
    bool keyed = false;
    int position = -1;   // its place among the buttons offered, in slot order
};

Found Button(ECS::Simulation& sim, Kind kind, SDL_Keycode key) {
    Rendering::UIRenderer::Command cmds[Rendering::UIRenderer::kCommandSlots];
    const int n = Rendering::UIRenderer::BuildCommands(sim, 1024, 768, cmds,
                                                       Rendering::UIRenderer::kCommandSlots);
    SDL_KeyboardEvent press{};
    press.type = SDL_KEYDOWN;
    press.keysym.sym = key;
    Found out;
    for (int i = 0; i < n; ++i) {
        if (cmds[i].kind != kind) continue;
        out.offered = true;
        out.position = i;
        out.keyed = cmds[i].hotkey_group >= 0 &&
                    Core::Hotkeys::Is(press, cmds[i].hotkey_group, cmds[i].hotkey_item);
    }
    return out;
}

} // namespace

// The command buttons with the original's hotkeys: a Monk's Convert (C) and
// Heal (E), first and second; a Monk with a relic has neither; a loaded
// transport's Unload (L), an empty one has none; a Barracks' Set Gather Point
// (I); a trebuchet's Unpack (U) and Pack (P). A gather point set on the
// building itself is taken away.
bool CheckButtons() {
    const char* verdict = nullptr;
    ECS::Simulation sim;
    sim.Init();
    auto& reg = sim.GetRegistry();
    auto& grid = sim.GetGrid();
    auto& cfg = Core::ConfigManager::Get();
    sim.Update(16);
    const auto [sx, sy] = sim.player_starts[0];
    auto place = [&](const char* name, int dx, int dy) {
        return ECS::ScenarioWorld::Place(reg, grid, cfg.GetUnitIdByName(name), kUs,
                                         At(sx + dx, sy + dy), {}, 0);
    };
    const entt::entity monk = place("MONKX", 6, 0);
    const entt::entity bearer = place("MONKX", 6, 2);
    const entt::entity ship = place("XPORT", 8, 4);
    const entt::entity rider = place("VMBAS", 7, 4);
    const entt::entity barracks = place("BRKS", -8, 6);
    const entt::entity packed = place("PTREB", 4, -6);
    const entt::entity treb = place("TREBU", 6, -6);
    if (monk == entt::null || bearer == entt::null || ship == entt::null || rider == entt::null ||
        barracks == entt::null || packed == entt::null || treb == entt::null) {
        verdict = "the units and buildings could not be placed";
    }
    if (verdict == nullptr) {
        reg.emplace_or_replace<ECS::Components::RelicBearer>(bearer);
        Select(reg, monk);
        const Found convert = Button(sim, Kind::Convert, SDLK_c);
        const Found heal = Button(sim, Kind::Heal, SDLK_e);
        Select(reg, bearer);
        const Found bearer_heal = Button(sim, Kind::Heal, SDLK_e);
        Select(reg, ship);
        const Found empty = Button(sim, Kind::Unload, SDLK_l);
        ECS::Garrisons::PutInside(reg, rider, ship);
        const Found loaded = Button(sim, Kind::Unload, SDLK_l);
        Select(reg, barracks);
        const Found gather = Button(sim, Kind::GatherPoint, SDLK_i);
        Select(reg, packed);
        const Found unpack = Button(sim, Kind::Pack, SDLK_u);
        Select(reg, treb);
        const Found pack = Button(sim, Kind::Pack, SDLK_p);
        if (!convert.offered || !convert.keyed || convert.position != 0) {
            verdict = "a Monk has no Convert button first on C";
        } else if (!heal.offered || !heal.keyed || heal.position != 1) {
            verdict = "a Monk has no Heal button second on E";
        } else if (bearer_heal.offered) {
            verdict = "a Monk carrying a relic offers to heal";
        } else if (empty.offered) {
            verdict = "an empty transport offers to unload";
        } else if (!loaded.offered || !loaded.keyed) {
            verdict = "a loaded transport has no Unload button on L";
        } else if (!gather.offered || !gather.keyed) {
            verdict = "a Barracks has no Set Gather Point button on I";
        } else if (!unpack.offered || !unpack.keyed) {
            verdict = "a packed trebuchet's Unpack is not on U";
        } else if (!pack.offered || !pack.keyed) {
            verdict = "a trebuchet's Pack is not on P";
        }
    }
    if (verdict == nullptr) {
        const auto& at = reg.get<ECS::Components::Transform>(barracks).position;
        ECS::Orders::Execute(reg, grid, Core::RallyCommand{barracks, at.x + Core::Fixed32(4), at.y,
                                                           entt::null, kUs});
        const bool set = reg.all_of<ECS::Components::RallyPoint>(barracks);
        ECS::Orders::Execute(reg, grid, Core::RallyCommand{barracks, at.x, at.y, barracks, kUs});
        if (!set) verdict = "a gather point was not set";
        else if (reg.all_of<ECS::Components::RallyPoint>(barracks)) verdict = "a gather point set on its building stayed";
    }
    std::printf("buttons=%s\n", verdict == nullptr ? "ok" : verdict);
    return verdict == nullptr;
}

} // namespace Verify
