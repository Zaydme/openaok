#include "InputManager.h"
#include "hal/Pad.h"
#include "hal/Touch.h"
#include "hal/Window.h"
#include "audio/Voices.h"
#include "rendering/HudLayout.h"
#include "rendering/Alerts.h"
#include "rendering/Renderer.h"
#include "rendering/EntityRenderer.h"
#include "rendering/Messages.h"
#include "rendering/UIRenderer.h"
#include "rendering/Ground.h"
#include "rendering/Placement.h"
#include "ecs/Animals.h"
#include "ecs/BuildingGeometry.h"
#include "core/IsometricMath.h"
#include "core/CommandQueue.h"
#include "ecs/components/Transform.h"
#include "ecs/components/Selectable.h"
#include "ecs/components/Resource.h"
#include "ecs/components/Worker.h"
#include "ecs/components/Health.h"
#include "ecs/components/Combat.h"
#include "ecs/components/Monk.h"
#include "ecs/components/Building.h"
#include "ecs/components/Identity.h"
#include "ecs/components/Owner.h"
#include "ecs/components/Path.h"
#include "core/ConfigManager.h"
#include "core/Hotkeys.h"
#include "core/Strings.h"
#include "ecs/Garrisons.h"
#include "ecs/Relics.h"
#include "ecs/Trade.h"
#include "ecs/Vision.h"
#include "ecs/Walls.h"
#include "ecs/Repairs.h"
#include "core/Profile.h"
#include "core/Rules.h"
#include "ecs/Diplomacy.h"
#include "ecs/Missiles.h"
#include "ecs/Packing.h"
#include "ecs/DropSites.h"
#include "ecs/Monks.h"
#include <algorithm>
#include <cmath>

namespace {

// An order given at something: the unit the order is spoken for answers it,
// and the thing the order was given at flashes green for a second and a
// half. Nothing flashes for an order on the ground.
void Answered(const entt::registry& registry, entt::entity unit, Audio::Voices::Order order,
              entt::entity target) {
    Audio::Voices::Acknowledge(registry, unit, order, target, Core::LocalPlayer());
    if (target != entt::null) Rendering::EntityRenderer::Flash(registry, target, true);
}

// The dat's classes the cursor's choice turns on.
constexpr int kTradeBoatClass = 2;
constexpr int kDeepFishClass = 5;
constexpr int kSiegeClass = 13;
constexpr int kTradeCartClass = 19;
constexpr int kTransportClass = 20;
constexpr int kFishingShipClass = 21;
constexpr int kWarshipClass = 22;
constexpr int kWallClass = 27;
constexpr int kFishClass = 31;
constexpr int kShoreFishClass = 33;
constexpr int kGateClass = 39;
constexpr int kRelicClass = 42;
constexpr int kFarmClass = 49;
constexpr int kPackedClass = 51;
constexpr int kUnpackedSiegeClass = 54;
constexpr int kSiegeShipClass = 55;
constexpr int kSheepClass = 58;
constexpr int kKingClass = 59;

// Whether a building can shoot now: a tower or a Castle, or a Town Center with
// a garrison to loose its arrows. One that can is sent at what it is pointed
// at; one that cannot takes the click as its gather point.
bool Shoots(const entt::registry& registry, entt::entity building) {
    return registry.all_of<ECS::Components::Combat>(building) &&
           ECS::Missiles::VolleyOf(registry, building).Total() > 0;
}

// Whether a right-click on one of the player's buildings sends whoever it can
// shelter inside: Alt held, or One-Click Garrisoning on in the Options.
bool GarrisonKey() {
    return (SDL_GetModState() & KMOD_ALT) != 0 || Core::Profile::Current().one_click_garrison;
}

} // namespace

namespace Core {

void InputManager::MinimapJump(Rendering::Renderer& renderer, ECS::Simulation& sim,
                               int fx, int fy) {
    // The mini-map is the panel's, at the panel's size.
    const auto panel = Rendering::UIRenderer::PanelSpace(last_win_w, last_win_h);
    int mx = 0, my = 0;
    panel.ToSpace(fx, fy, mx, my);
    const SDL_FRect well = Rendering::UIRenderer::MinimapRect(panel.w, last_win_h);
    auto& grid = sim.GetGrid();
    float gx = 0.0f, gy = 0.0f;
    if (!Rendering::UIRenderer::MinimapUnproject(well, static_cast<float>(mx),
                                                 static_cast<float>(my),
                                                 grid.GetWidth(), grid.GetHeight(),
                                                 gx, gy)) {
        return;  // inside the well, but off the map diamond
    }
    const auto screen_pos = IsometricMath::WorldToScreen(Fixed32(gx), Fixed32(gy));
    renderer.target_camera_x = screen_pos.x - (last_win_w / 2.0f) / renderer.target_camera_zoom;
    renderer.target_camera_y = screen_pos.y - (last_win_h / 2.0f) / renderer.target_camera_zoom;
    // A minimap jump is a teleport: it skips the camera smoothing.
    renderer.camera_x = renderer.target_camera_x;
    renderer.camera_y = renderer.target_camera_y;
}

void InputManager::GivePendingOrder(ECS::Simulation& sim, float x, float y) {
    auto& registry = sim.GetRegistry();
    const PendingOrder order = pending_order;
    pending_order = PendingOrder::None;
    // A signal to the allies goes up wherever it was pointed, whatever is
    // selected.
    if (order == PendingOrder::Flare) {
        CommandQueue::Get().PushCommand(FlareCommand{Fixed32(x), Fixed32(y), Core::LocalPlayer()});
        return;
    }

    // The player's selected units: everyone for a garrison or a repair, only
    // fighting units for the standing orders.
    const bool fighters_only = order == PendingOrder::Patrol || order == PendingOrder::Guard ||
                               order == PendingOrder::Follow;
    std::vector<entt::entity> units;
    auto picked = registry.view<ECS::Components::Selectable, ECS::Components::Owner>();
    for (auto e : picked) {
        if (!picked.get<ECS::Components::Selectable>(e).is_selected) continue;
        if (picked.get<ECS::Components::Owner>(e).player_id != Core::LocalPlayer()) continue;
        if (registry.all_of<ECS::Components::Building>(e)) continue;
        if (fighters_only && !registry.all_of<ECS::Components::Combat>(e)) continue;
        if (order == PendingOrder::Repair && !registry.all_of<ECS::Components::Worker>(e)) continue;
        units.push_back(e);
    }
    if (units.empty()) return;

    if (order == PendingOrder::Patrol) {
        CommandQueue::Get().PushCommand(
            PatrolCommand{units, Fixed32(x), Fixed32(y), Core::LocalPlayer()});
        Answered(registry, units.front(), Audio::Voices::Order::Move, entt::null);
        return;
    }

    // A building of the player's under the click, by its footprint.
    auto building_at = [&]() {
        auto own = registry.view<ECS::Components::Transform, ECS::Components::Building,
                                 ECS::Components::Identity, ECS::Components::Owner>();
        for (auto b : own) {
            if (own.get<ECS::Components::Owner>(b).player_id != Core::LocalPlayer()) continue;
            const auto* info = ConfigManager::Get().GetUnitInterface(
                own.get<ECS::Components::Identity>(b).unit_id);
            const float reach = (info && info->footprint > 0)
                              ? static_cast<float>(info->footprint) * 0.5f + 0.25f : 0.75f;
            const auto& p = own.get<ECS::Components::Transform>(b).position;
            if (std::abs(p.x.ToFloat() - x) <= reach && std::abs(p.y.ToFloat() - y) <= reach) {
                return b;
            }
        }
        return entt::entity{entt::null};
    };

    if (order == PendingOrder::Garrison) {
        const entt::entity shelter = building_at();
        if (shelter == entt::null) return;
        entt::entity first = entt::null;
        for (auto e : units) {
            if (!ECS::Garrisons::Accepts(registry, shelter, e)) continue;
            CommandQueue::Get().PushCommand(GarrisonCommand{e, shelter, Core::LocalPlayer()});
            if (first == entt::null) first = e;
        }
        if (first != entt::null) {
            Answered(registry, first, Audio::Voices::Order::Move, shelter);
        }
        return;
    }
    if (order == PendingOrder::Repair) {
        // A building of the player's -- still going up, it is built; finished
        // and damaged, mended -- or failing that a ship or trebuchet the
        // villagers can mend, under the click.
        entt::entity target = building_at();
        if (target == entt::null) {
            constexpr float kReach = 0.75f;
            auto bodies = registry.view<ECS::Components::Transform, ECS::Components::Health>();
            for (auto e : bodies) {
                if (registry.all_of<ECS::Components::Building>(e)) continue;
                if (!ECS::Repairs::CanRepair(registry, units.front(), e)) continue;
                const auto& p = bodies.get<ECS::Components::Transform>(e).position;
                const float dx = p.x.ToFloat() - x, dy = p.y.ToFloat() - y;
                if (dx * dx + dy * dy <= kReach * kReach) {
                    target = e;
                    break;
                }
            }
        }
        if (target == entt::null) return;
        CommandQueue::Get().PushCommand(RepairCommand{units, target, Core::LocalPlayer()});
        Answered(registry, units.front(), Audio::Voices::Order::Work, target);
        return;
    }

    // Guard or follow: the unit nearest the click, within reach of it.
    constexpr float kPickRadius = 0.75f;
    entt::entity charge = entt::null;
    float nearest = kPickRadius * kPickRadius;
    auto bodies = registry.view<ECS::Components::Transform, ECS::Components::Selectable,
                                ECS::Components::Health>();
    for (auto e : bodies) {
        if (registry.all_of<ECS::Components::Building>(e)) continue;
        if (bodies.get<ECS::Components::Selectable>(e).priority <= 0) continue;
        if (!ECS::Vision::Shows(registry, Core::LocalPlayer(), e)) continue;
        const auto& p = bodies.get<ECS::Components::Transform>(e).position;
        const float dx = p.x.ToFloat() - x;
        const float dy = p.y.ToFloat() - y;
        if (dx * dx + dy * dy <= nearest) {
            nearest = dx * dx + dy * dy;
            charge = e;
        }
    }
    if (charge == entt::null) return;
    if (order == PendingOrder::Guard) {
        CommandQueue::Get().PushCommand(GuardCommand{units, charge, Core::LocalPlayer()});
    } else {
        CommandQueue::Get().PushCommand(FollowCommand{units, charge, Core::LocalPlayer()});
    }
    Answered(registry, units.front(), Audio::Voices::Order::Move, charge);
}

void InputManager::NextIdleVillager(ECS::Simulation& sim, Rendering::Renderer& renderer) {
    auto& registry = sim.GetRegistry();
    // The player's villagers with nothing to do, in a fixed order, starting
    // after the one the button found last.
    std::vector<entt::entity> idle;
    auto workers = registry.view<ECS::Components::Worker, ECS::Components::Owner,
                                 ECS::Components::Transform>();
    for (auto e : workers) {
        if (workers.get<ECS::Components::Owner>(e).player_id != Core::LocalPlayer()) continue;
        const auto& worker = workers.get<ECS::Components::Worker>(e);
        if (worker.state != UnitState::Idle) continue;
        if (const auto* path = registry.try_get<ECS::Components::Path>(e);
            path != nullptr && path->has_goal) {
            continue;
        }
        idle.push_back(e);
    }
    if (idle.empty()) return;
    std::sort(idle.begin(), idle.end());
    auto next = std::upper_bound(idle.begin(), idle.end(), last_idle);
    if (next == idle.end()) next = idle.begin();
    last_idle = *next;

    auto sel = registry.view<ECS::Components::Selectable>();
    for (auto e : sel) sel.get<ECS::Components::Selectable>(e).is_selected = (e == last_idle);
    Audio::Voices::Selected(registry, Core::LocalPlayer());
    const auto& at = registry.get<ECS::Components::Transform>(last_idle).position;
    LookAt(renderer, at.x.ToFloat(), at.y.ToFloat());
}

void InputManager::LookAt(Rendering::Renderer& renderer, float wx, float wy) {
    const auto screen = IsometricMath::WorldToScreen(Fixed32(wx), Fixed32(wy));
    renderer.target_camera_x = screen.x - last_win_w / 2.0f;
    renderer.target_camera_y = screen.y - last_win_h / 2.0f;
    renderer.camera_x = renderer.target_camera_x;
    renderer.camera_y = renderer.target_camera_y;
}

void InputManager::NextIdleMilitary(ECS::Simulation& sim, Rendering::Renderer& renderer) {
    auto& registry = sim.GetRegistry();
    // The player's fighting units standing about: no target, nowhere to go,
    // no standing order -- the villager button's question asked of soldiers.
    std::vector<entt::entity> idle;
    auto fighters = registry.view<ECS::Components::Combat, ECS::Components::Owner,
                                  ECS::Components::Transform>();
    for (auto e : fighters) {
        if (fighters.get<ECS::Components::Owner>(e).player_id != Core::LocalPlayer()) continue;
        if (registry.any_of<ECS::Components::Worker, ECS::Components::Building>(e)) continue;
        const auto& combat = fighters.get<ECS::Components::Combat>(e);
        if (combat.state != ECS::Components::CombatState::Idle || combat.target != entt::null) {
            continue;
        }
        if (const auto* path = registry.try_get<ECS::Components::Path>(e);
            path != nullptr && path->has_goal) {
            continue;
        }
        idle.push_back(e);
    }
    if (idle.empty()) return;
    std::sort(idle.begin(), idle.end());
    auto next = std::upper_bound(idle.begin(), idle.end(), last_idle_military);
    if (next == idle.end()) next = idle.begin();
    last_idle_military = *next;
    auto sel = registry.view<ECS::Components::Selectable>();
    for (auto e : sel) sel.get<ECS::Components::Selectable>(e).is_selected = (e == *next);
    Audio::Voices::Selected(registry, Core::LocalPlayer());
    const auto& at = registry.get<ECS::Components::Transform>(*next).position;
    LookAt(renderer, at.x.ToFloat(), at.y.ToFloat());
}

void InputManager::GoToBuilding(ECS::Simulation& sim, Rendering::Renderer& renderer,
                                const char* name) {
    auto& registry = sim.GetRegistry();
    auto& cfg = ConfigManager::Get();
    std::vector<entt::entity> found;
    auto buildings = registry.view<ECS::Components::Building, ECS::Components::Identity,
                                   ECS::Components::Owner, ECS::Components::Transform>();
    for (auto e : buildings) {
        if (buildings.get<ECS::Components::Owner>(e).player_id != Core::LocalPlayer()) continue;
        const auto* ui = cfg.GetUnitInterface(buildings.get<ECS::Components::Identity>(e).unit_id);
        if (ui == nullptr || ui->name != name) continue;
        if (!registry.all_of<ECS::Components::Selectable>(e)) continue;
        found.push_back(e);
    }
    if (found.empty()) return;
    std::sort(found.begin(), found.end());
    auto next = std::upper_bound(found.begin(), found.end(), last_visited);
    if (next == found.end()) next = found.begin();
    last_visited = *next;
    auto sel = registry.view<ECS::Components::Selectable>();
    for (auto e : sel) sel.get<ECS::Components::Selectable>(e).is_selected = (e == *next);
    const auto& at = registry.get<ECS::Components::Transform>(*next).position;
    LookAt(renderer, at.x.ToFloat(), at.y.ToFloat());
}

void InputManager::AssignGroup(ECS::Simulation& sim, int group) {
    auto& registry = sim.GetRegistry();
    auto sel = registry.view<ECS::Components::Selectable, ECS::Components::Owner>();
    bool any = false;
    for (auto e : sel) {
        const auto& s = sel.get<ECS::Components::Selectable>(e);
        any = any || (s.is_selected &&
                      sel.get<ECS::Components::Owner>(e).player_id == Core::LocalPlayer());
    }
    if (!any) return;
    // The group is made afresh: whoever was in it and is not selected leaves,
    // and the selected leave whatever group they were in.
    for (auto e : sel) {
        auto& s = sel.get<ECS::Components::Selectable>(e);
        const bool mine = sel.get<ECS::Components::Owner>(e).player_id == Core::LocalPlayer();
        if (s.is_selected && mine) {
            s.group = group;
        } else if (s.group == group) {
            s.group = 0;
        }
    }
}

void InputManager::SelectGroup(ECS::Simulation& sim, int group, bool add,
                               Rendering::Renderer& renderer) {
    auto& registry = sim.GetRegistry();
    // The same number twice in quick succession looks at the group.
    const uint32_t now = SDL_GetTicks();
    const bool again = group == last_group && now - last_group_at < kDoubleTapMs;
    last_group = group;
    last_group_at = now;
    auto sel = registry.view<ECS::Components::Selectable, ECS::Components::Owner>();
    bool any = false;
    for (auto e : sel) {
        // A unit in a building is out of sight and out of the group's reach.
        any = any || (sel.get<ECS::Components::Selectable>(e).group == group &&
                      registry.all_of<ECS::Components::Transform>(e));
    }
    if (!any) return;
    float sx = 0.0f, sy = 0.0f;
    int n = 0;
    for (auto e : sel) {
        auto& s = sel.get<ECS::Components::Selectable>(e);
        const bool in = s.group == group && registry.all_of<ECS::Components::Transform>(e);
        s.is_selected = in || (add && s.is_selected);
        if (in) {
            const auto& at = registry.get<ECS::Components::Transform>(e).position;
            sx += at.x.ToFloat();
            sy += at.y.ToFloat();
            ++n;
        }
    }
    if (again && n > 0) LookAt(renderer, sx / n, sy / n);
    Audio::Voices::Selected(registry, Core::LocalPlayer());
}

bool InputManager::GameCommand(int item, ECS::Simulation& sim, Rendering::Renderer& renderer) {
    auto& registry = sim.GetRegistry();
    namespace HK = Hotkeys;
    switch (item) {
    case HK::kIdleVillager:
    case HK::kIdleVillagerMouse:
        NextIdleVillager(sim, renderer);
        return true;
    case HK::kIdleMilitary:
        NextIdleMilitary(sim, renderer);
        return true;
    case HK::kLastNotification:
    case HK::kLastNotificationMouse: {
        // The minimap's notices, newest first and back.
        ECS::Components::Vector2D at{};
        if (Rendering::Alerts::Next(at)) LookAt(renderer, at.x.ToFloat(), at.y.ToFloat());
        return true;
    }
    case HK::kGoToSelected: {
        // The middle of the selection.
        float sx = 0.0f, sy = 0.0f;
        int n = 0;
        auto sel = registry.view<ECS::Components::Selectable, ECS::Components::Transform>();
        for (auto e : sel) {
            if (!sel.get<ECS::Components::Selectable>(e).is_selected) continue;
            const auto& at = sel.get<ECS::Components::Transform>(e).position;
            sx += at.x.ToFloat();
            sy += at.y.ToFloat();
            ++n;
        }
        if (n > 0) LookAt(renderer, sx / n, sy / n);
        return true;
    }
    case HK::kChatBackward:
        Rendering::Messages::Review(1);
        return true;
    case HK::kChatForward:
        Rendering::Messages::Review(-1);
        return true;
    case HK::kFlare:
        pending_order = PendingOrder::Flare;
        return true;
    case HK::kGameTime:
        Rendering::UIRenderer::ToggleClock();
        return true;
    case HK::kStatistics:
        Rendering::UIRenderer::ActivateMinimapButton(Rendering::kStatistics);
        return true;
    case HK::kMinimapNormal:
        Rendering::UIRenderer::ActivateMinimapButton(Rendering::kNormalMode);
        return true;
    case HK::kMinimapCombat:
        Rendering::UIRenderer::ActivateMinimapButton(Rendering::kCombatMode);
        return true;
    case HK::kMinimapEconomic:
        Rendering::UIRenderer::ActivateMinimapButton(Rendering::kEconomicMode);
        return true;
    default:
        break;
    }
    // The buildings with a go-to key, by their dat names.
    static const struct { int item; const char* name; } kGoTo[] = {
        {HK::kGoToTownCenter, "Town Center"}, {HK::kGoToTownCenterMouse, "Town Center"},
        {HK::kGoToMarket, "Market"}, {HK::kGoToBarracks, "Barracks"},
        {HK::kGoToArcheryRange, "Archery Range"}, {HK::kGoToStable, "Stable"},
        {HK::kGoToSiegeWorkshop, "Siege Workshop"}, {HK::kGoToDock, "Dock"},
        {HK::kGoToMonastery, "Monastery"}, {HK::kGoToBlacksmith, "Blacksmith"},
        {HK::kGoToMill, "Mill"}, {HK::kGoToUniversity, "University"},
        {HK::kGoToCastle, "Castle"}, {HK::kGoToMiningCamp, "Mining Camp"},
        {HK::kGoToLumberCamp, "Lumber Camp"},
    };
    for (const auto& g : kGoTo) {
        if (g.item == item) {
            GoToBuilding(sim, renderer, g.name);
            return true;
        }
    }
    return false;
}

bool InputManager::HandleHotkey(const SDL_KeyboardEvent& key, ECS::Simulation& sim,
                                Rendering::Renderer& renderer) {
    const Uint16 mod = key.keysym.mod;
    const SDL_Keycode sym = key.keysym.sym;
    // Control groups: fixed, not in the hotkey table.
    if (sym >= SDLK_1 && sym <= SDLK_9 && (mod & KMOD_ALT) == 0) {
        const int group = static_cast<int>(sym - SDLK_0);
        if (Hotkeys::CtrlHeld(mod)) {
            AssignGroup(sim, group);
        } else {
            SelectGroup(sim, group, (mod & KMOD_SHIFT) != 0, renderer);
        }
        return true;
    }
    // The selection's own buttons first: a key means one thing per group, and
    // the grid on show says which group is in force -- B is the economic build
    // menu among a villager's commands and the Barracks inside that menu.
    Rendering::UIRenderer::Command commands[Rendering::UIRenderer::kCommandSlots];
    const int count = Rendering::UIRenderer::BuildCommands(
        sim, last_win_w, last_win_h, commands, Rendering::UIRenderer::kCommandSlots);
    for (int i = 0; i < count; ++i) {
        const auto& cmd = commands[i];
        if (cmd.hotkey_group < 0 || cmd.hotkey_item < 0) continue;
        if (!Hotkeys::Is(key, cmd.hotkey_group, cmd.hotkey_item)) continue;
        RunCommand(sim, cmd, (mod & KMOD_SHIFT) ? 5 : 1);
        return true;
    }
    const int item = Hotkeys::ItemOf(key, Hotkeys::kGameCommands);
    return item >= 0 && GameCommand(item, sim, renderer);
}

bool InputManager::HandleMouseKey(int mouse_key, ECS::Simulation& sim,
                                  Rendering::Renderer& renderer) {
    for (int item : {Hotkeys::kIdleVillagerMouse, Hotkeys::kLastNotificationMouse,
                     Hotkeys::kGoToTownCenterMouse}) {
        if (Hotkeys::IsMouse(mouse_key, Hotkeys::kGameCommands, item)) {
            return GameCommand(item, sim, renderer);
        }
    }
    return false;
}

namespace {

// The player's dead farm under the point (x, y), if any: the Farm that stood
// there (`farm_unit`) and the tile it stood on, to build it again.
bool DeadFarmAt(ECS::Simulation& sim, float x, float y, int& farm_unit, int& tile_x, int& tile_y) {
    auto& registry = sim.GetRegistry();
    const auto& rules = Core::Rules::Get();
    auto remains = registry.view<const ECS::Components::Identity, const ECS::Components::Owner,
                                 const ECS::Components::Transform>(entt::exclude<ECS::Components::Building>);
    for (auto e : remains) {
        if (remains.get<const ECS::Components::Owner>(e).player_id != Core::LocalPlayer()) continue;
        const int dead = remains.get<const ECS::Components::Identity>(e).unit_id;
        // The Farm whose remains these are.
        int farm = -1;
        for (int u = 0; u < static_cast<int>(rules.units.size()) && farm < 0; ++u) {
            if (rules.units[u].valid && rules.units[u].dead_unit == dead && rules.units[u].cls == 49) farm = u;
        }
        if (farm < 0) continue;
        const auto& at = remains.get<const ECS::Components::Transform>(e).position;
        const auto ground = ECS::Buildings::FootprintBounds(at, farm);
        if (x < ground.min_x.ToFloat() || x > ground.max_x.ToFloat() ||
            y < ground.min_y.ToFloat() || y > ground.max_y.ToFloat()) {
            continue;
        }
        farm_unit = farm;
        tile_x = Physics::TileOf(at.x);
        tile_y = Physics::TileOf(at.y);
        return true;
    }
    return false;
}

} // namespace

void InputManager::UpdateIntent(ECS::Simulation& sim, Rendering::Renderer& renderer) {
    intent = Intent::None;
    const int mx = current_mouse_x;
    const int my = current_mouse_y;
    if (mx < 0 || my < 0 || !mouse_in_window) return;
    if (Rendering::UIRenderer::OverInterface(last_win_w, last_win_h, mx, my)) return;
    // An order waiting for its target hints what to point at (3073: "Click
    // an area to patrol there."), with the cursor of its button.
    switch (pending_order) {
    case PendingOrder::Patrol:
        intent = Intent::Patrol;
        Rendering::UIRenderer::SetWorldHint(3073);
        return;
    case PendingOrder::Follow:
        intent = Intent::Follow;
        Rendering::UIRenderer::SetWorldHint(3072);
        return;
    case PendingOrder::Guard:
        intent = Intent::Guard;
        Rendering::UIRenderer::SetWorldHint(3071);
        return;
    case PendingOrder::Garrison:
        intent = Intent::Garrison;
        Rendering::UIRenderer::SetWorldHint(3092);
        return;
    case PendingOrder::Repair:
        intent = Intent::Place;
        Rendering::UIRenderer::SetWorldHint(3052);
        return;
    case PendingOrder::Flare:
        intent = Intent::Signal;
        Rendering::UIRenderer::SetWorldHint(3097);
        return;
    case PendingOrder::None: break;
    }
    if (placing_building) {
        intent = Intent::Place;
        return;
    }

    auto& registry = sim.GetRegistry();
    const auto wc = Rendering::Ground::Pick(sim.GetGrid(), renderer.camera_x + mx,
                                            renderer.camera_y + my);
    const float x = static_cast<float>(wc.first);
    const float y = static_cast<float>(wc.second);

    // What the selection is made of: villagers, fishing ships (workers that
    // gather as themselves), whatever can fight -- a building that can shoot
    // now, a packed trebuchet that sets up to -- monks, and whether a monk
    // has the faith to convert.
    bool villagers = false, boats = false, fighters = false, monks = false, faithful = false;
    auto picked = registry.view<ECS::Components::Selectable, ECS::Components::Owner>();
    for (auto e : picked) {
        if (!picked.get<ECS::Components::Selectable>(e).is_selected) continue;
        if (picked.get<ECS::Components::Owner>(e).player_id != Core::LocalPlayer()) continue;
        if (registry.all_of<ECS::Components::Building>(e)) {
            if (Shoots(registry, e)) fighters = true;
            continue;
        }
        if (const auto* worker = registry.try_get<ECS::Components::Worker>(e)) {
            if (worker->gatherer >= 0) boats = true;
            else villagers = true;
        }
        if (const auto* monk = registry.try_get<ECS::Components::Monk>(e)) {
            monks = true;
            faithful = faithful || monk->faith >= Fixed32(100);
        } else if (registry.all_of<ECS::Components::Combat>(e) || ECS::Packing::Packed(registry, e)) {
            fighters = true;
        }
    }
    const bool workers = villagers || boats;
    const bool anyone = workers || fighters || monks;
    // Whether any selected unit of the player's passes `test`.
    auto any = [&](auto test) {
        for (auto e : picked) {
            if (!picked.get<ECS::Components::Selectable>(e).is_selected) continue;
            if (picked.get<ECS::Components::Owner>(e).player_id != Core::LocalPlayer()) continue;
            if (test(e)) return true;
        }
        return false;
    };

    // A dead farm, for villagers to rebuild (LANGUAGE.DLL 3930).
    if (villagers) {
        int farm_unit = -1, fx = 0, fy = 0;
        if (DeadFarmAt(sim, x, y, farm_unit, fx, fy)) {
            intent = Intent::Work;
            Rendering::UIRenderer::SetWorldHint(3930);
            return;
        }
    }

    // The thing nearest the cursor, as a right-click would find it.
    constexpr float kReach = 0.75f;
    entt::entity nearest = entt::null;
    float best = kReach * kReach;
    auto things = registry.view<ECS::Components::Transform, ECS::Components::Selectable>();
    for (auto e : things) {
        if (!ECS::Vision::Shows(registry, Core::LocalPlayer(), e)) continue;
        if (Rendering::EntityRenderer::PickedByArt(registry, e)) continue;
        const auto& p = things.get<ECS::Components::Transform>(e).position;
        float reach = kReach;
        if (registry.all_of<ECS::Components::Building>(e)) {
            const auto* info = registry.all_of<ECS::Components::Identity>(e)
                ? ConfigManager::Get().GetUnitInterface(
                      registry.get<ECS::Components::Identity>(e).unit_id) : nullptr;
            reach = info != nullptr && info->footprint > 0 ? info->footprint * 0.5f : kReach;
        }
        const float dx = p.x.ToFloat() - x;
        const float dy = p.y.ToFloat() - y;
        if (std::abs(dx) > reach || std::abs(dy) > reach) continue;
        const float d = dx * dx + dy * dy;
        if (nearest == entt::null || d < best) { best = d; nearest = e; }
    }
    // What is under the pointer by its art, as a click finds it, before
    // anything near it on the ground. Units are found only by their art.
    {
        const float zoom = renderer.camera_zoom > 0.0f ? renderer.camera_zoom : 1.0f;
        const entt::entity art = Rendering::EntityRenderer::PickAt(registry, mx / zoom, my / zoom);
        if (art != entt::null && ECS::Vision::Shows(registry, Core::LocalPlayer(), art)) nearest = art;
    }
    // Nothing there: the arrow, whatever is selected.
    if (nearest == entt::null) return;

    const auto* owner = registry.try_get<ECS::Components::Owner>(nearest);
    const Core::PlayerId whose = owner != nullptr ? owner->player_id : Core::PLAYER_GAIA;
    const bool mine = whose == Core::LocalPlayer();
    const bool gaia = whose == Core::PLAYER_GAIA;
    const bool friendly = !gaia && (mine || ECS::Diplomacy::StanceOf(registry, Core::LocalPlayer(), whose) ==
                                                ECS::Diplomacy::Stance::Ally);
    const auto* resource = registry.try_get<ECS::Components::Resource>(nearest);
    const auto* site = registry.try_get<ECS::Components::Building>(nearest);
    const auto* health = registry.try_get<ECS::Components::Health>(nearest);
    const bool wounded = health != nullptr && health->current_hp > 0 && health->current_hp < health->max_hp;
    const auto* identity = registry.try_get<ECS::Components::Identity>(nearest);
    const Core::Rules::UnitRules* rules = identity != nullptr ? Core::Rules::Unit(identity->unit_id) : nullptr;
    const int cls = rules != nullptr ? rules->cls : -1;
    auto one_of = [cls](std::initializer_list<int> classes) {
        return std::find(classes.begin(), classes.end(), cls) != classes.end();
    };
    const bool ship = one_of({kTransportClass, kFishingShipClass, kWarshipClass});
    // The hover hint strings, by resource type.
    auto resource_hint = [&](bool selected) {
        const int base = selected ? 3900 : 3820;
        switch (resource->type) {
        case ResourceType::Wood: return selected ? base + 8 : base + 2;
        case ResourceType::Gold: return selected ? base + 12 : base + 5;
        case ResourceType::Stone: return selected ? base + 11 : base + 4;
        default: return selected ? base + 9 : base + 3;
        }
    };

    // A ship or siege engine in need of repair, whoever's: villagers mend it
    // -- but not a transport the selection can board.
    if (villagers && site == nullptr && wounded &&
        (ship || one_of({kSiegeClass, kPackedClass, kUnpackedSiegeClass, kSiegeShipClass}))) {
        const bool boards = cls == kTransportClass && mine &&
                            any([&](entt::entity e) { return ECS::Garrisons::Accepts(registry, nearest, e); });
        if (!boards) {
            intent = Intent::Work;
            Rendering::UIRenderer::SetWorldHint(3933);
            return;
        }
    }
    // A relic: a monk picks it up.
    if (cls == kRelicClass) {
        if (monks) {
            intent = Intent::Monk;
            Rendering::UIRenderer::SetWorldHint(3922);
        }
        return;
    }
    // Gaia's resources: a villager or a fishing ship goes to work on them. Of
    // the fish, a villager takes only what is by the shore.
    if (resource != nullptr && gaia && site == nullptr && health == nullptr) {
        const bool fish = one_of({kDeepFishClass, kFishClass, kShoreFishClass});
        if (fish && (boats || (villagers && cls == kShoreFishClass))) {
            intent = Intent::Work;
            Rendering::UIRenderer::SetWorldHint(3907);
        } else if (!fish && villagers) {
            intent = Intent::Work;
            Rendering::UIRenderer::SetWorldHint(resource_hint(true));
        } else if (!anyone) {
            Rendering::UIRenderer::SetWorldHint(resource_hint(false));
        }
        return;
    }
    // Sheep, Gaia's or the player's: villagers herd them in for food.
    if (cls == kSheepClass && (mine || gaia)) {
        if (villagers) {
            intent = Intent::Work;
            Rendering::UIRenderer::SetWorldHint(3929);
        }
        return;
    }
    // Gaia's animals: villagers hunt them; anyone else armed just attacks.
    if (gaia && health != nullptr && site == nullptr) {
        if (villagers) {
            intent = Intent::Work;
            Rendering::UIRenderer::SetWorldHint(3910);
        } else if (fighters) {
            intent = Intent::Attack;
            Rendering::UIRenderer::SetWorldHint(3960);
        }
        return;
    }
    if (mine && site != nullptr) {
        if (villagers && site->state == ECS::Components::BuildingState::UnderConstruction) {
            intent = Intent::Work;
            Rendering::UIRenderer::SetWorldHint(3955);
            return;
        }
        // A Farm of the player's: villagers farm it.
        if (resource != nullptr) {
            if (villagers) {
                intent = Intent::Work;
                Rendering::UIRenderer::SetWorldHint(3927);
            }
            return;
        }
        // Finished, two passes, the garrison first when One-Click
        // Garrisoning is on: dropping off, a relic put away, repair; and
        // going inside -- shown with the door only while Alt is held unless
        // the option is on, as a right-click takes it (GarrisonKey).
        const bool one_click = Core::Profile::Current().one_click_garrison;
        auto work_pass = [&]() {
            int hint = 0;
            if (any([&](entt::entity e) { return ECS::DropSites::Takes(registry, e, nearest); })) {
                hint = 3923;
            } else if (any([&](entt::entity e) { return ECS::Relics::CanDeposit(registry, e, nearest); })) {
                hint = 3932;
            } else if (villagers && wounded) {
                hint = cls == kWallClass ? 3913 : 3905;
            }
            if (hint == 0) return false;
            intent = Intent::Work;
            Rendering::UIRenderer::SetWorldHint(hint);
            return true;
        };
        auto garrison_pass = [&]() {
            if (!any([&](entt::entity e) { return ECS::Garrisons::Accepts(registry, nearest, e); })) return false;
            Rendering::UIRenderer::SetWorldHint(one_click ? 3934 : 3914);
            if (GarrisonKey()) intent = Intent::Garrison;
            return true;
        };
        if (one_click) {
            if (!garrison_pass()) work_pass();
        } else if (!work_pass()) {
            garrison_pass();
        }
        return;
    }
    // The player's own units, and its allies': a transport the selection
    // boards; a wounded unit for a monk to heal -- not the one monk
    // selected hovering itself.
    if (friendly && site == nullptr && health != nullptr) {
        if (mine && cls == kTransportClass &&
            any([&](entt::entity e) { return ECS::Garrisons::Accepts(registry, nearest, e); })) {
            intent = Intent::Board;
            Rendering::UIRenderer::SetWorldHint(3906);
        } else if (monks && wounded && !ship &&
                   !one_of({kSiegeClass, kPackedClass, kUnpackedSiegeClass, kSiegeShipClass}) &&
                   any([&](entt::entity e) { return e != nearest && registry.all_of<ECS::Components::Monk>(e); })) {
            intent = Intent::Monk;
            Rendering::UIRenderer::SetWorldHint(3900);
        }
        return;
    }
    // A neutral's or an enemy's: struck by whatever can strike; failing that,
    // converted by a monk with its faith full -- never a wall, a gate, a Farm
    // or a King, and only what it may convert. Trade carts and boats show
    // nothing.
    if (!friendly && !gaia && health != nullptr && health->current_hp > 0) {
        if (one_of({kTradeBoatClass, kTradeCartClass})) return;
        int hint = site != nullptr ? (cls == kWallClass ? 3903 : 3902) : ship ? 3904 : 3901;
        if (fighters || villagers) {
            intent = Intent::Attack;
            Rendering::UIRenderer::SetWorldHint(hint);
        } else if (faithful && !one_of({kWallClass, kGateClass, kFarmClass, kKingClass}) &&
                   any([&](entt::entity e) {
                       const auto* monk = registry.try_get<ECS::Components::Monk>(e);
                       return monk != nullptr && monk->faith >= Fixed32(100) &&
                              ECS::Monks::CanConvert(registry, e, nearest);
                   })) {
            intent = Intent::Monk;
            Rendering::UIRenderer::SetWorldHint(site != nullptr ? 3952 : 3951);
        }
    }
}

void InputManager::Update(Rendering::Renderer& renderer, float dt_seconds, int win_w, int win_h) {
    last_win_w = win_w;
    last_win_h = win_h;
    
    // Pan speed is in screen pixels per second, so panning feels the same at
    // any zoom; dividing by zoom converts it to world units.
    const float pan_speed = 800.0f * dt_seconds / renderer.target_camera_zoom;
    float cx = renderer.target_camera_x;
    float cy = renderer.target_camera_y;

    // On a touch screen (hal/Touch): two fingers drag the map under them,
    // straight away rather than eased; and a building waiting to be placed,
    // or an order waiting for its target, is aimed with the finger. A wall is
    // not: it is laid by a drag.
    float drag_x = 0.0f, drag_y = 0.0f;
    HAL::Touch::TakePan(drag_x, drag_y);
    cx -= drag_x / renderer.target_camera_zoom;
    cy -= drag_y / renderer.target_camera_zoom;
    renderer.camera_x -= drag_x / renderer.camera_zoom;
    renderer.camera_y -= drag_y / renderer.camera_zoom;
    HAL::Touch::SetAiming((placing_building && !ECS::Walls::IsWall(building_to_place)) ||
                          pending_order != PendingOrder::None);

    // Edge scrolling wants a mouse: a finger leaves the pointer where it
    // lifted, which may well be on an edge.
    if (mouse_in_window && HAL::Touch::MouseAttached()) {
        const int edge = 15;
        if (current_mouse_x <= edge) cx -= pan_speed;
        if (current_mouse_x >= win_w - edge) cx += pan_speed;
        if (current_mouse_y <= edge) cy -= pan_speed;
        if (current_mouse_y >= win_h - edge) cy += pan_speed;
    }

    // The controller's right stick scrolls as the arrow keys do, as far as it
    // leans (hal/Pad).
    float stick_x = 0.0f, stick_y = 0.0f;
    HAL::Pad::Scroll(stick_x, stick_y);
    cx += stick_x * pan_speed;
    cy += stick_y * pan_speed;

    if (pan_left) cx -= pan_speed;
    if (pan_right) cx += pan_speed;
    if (pan_up) cy -= pan_speed;
    if (pan_down) cy += pan_speed;

    renderer.target_camera_x = cx;
    renderer.target_camera_y = cy;

    // Exponential smoothing: 1 - exp(-k * dt) gives the same curve at any
    // frame rate.
    const float k = 12.0f;
    const float blend = 1.0f - std::exp(-k * dt_seconds);
    renderer.camera_x += (renderer.target_camera_x - renderer.camera_x) * blend;
    renderer.camera_y += (renderer.target_camera_y - renderer.camera_y) * blend;
    renderer.camera_zoom += (renderer.target_camera_zoom - renderer.camera_zoom) * blend;
}

void InputManager::RunCommand(ECS::Simulation& sim, const Rendering::UIRenderer::Command& cmd,
                              int times) {
    auto& registry = sim.GetRegistry();
    // What cannot be paid for is refused with a message saying why. A unit
    // with no room in the houses is still queued (it waits for one), but the
    // player is told.
    if (const int refusal = Rendering::UIRenderer::Refusal(sim, cmd); refusal >= 0) {
        Rendering::Messages::Post(Core::Strings::Get(refusal));
        if (refusal != Rendering::UIRenderer::kNeedHouses) return;
    }
    switch (Rendering::UIRenderer::ActivateCommand(cmd)) {
    case Rendering::UIRenderer::Action::PlaceBuilding:
        placing_building = true;
        building_to_place = cmd.unit_id;
        break;
    case Rendering::UIRenderer::Action::DeleteBuilding: {
        // Queued as a command: the building, or every selected unit,
        // is deleted inside the tick.
        const entt::entity building =
            Rendering::UIRenderer::SelectedBuilding(sim);
        if (building != entt::null) {
            CommandQueue::Get().PushCommand(
                DeleteCommand{building, Core::LocalPlayer()});
            break;
        }
        auto picked = registry.view<ECS::Components::Selectable,
                                    ECS::Components::Owner>();
        for (auto e : picked) {
            if (!picked.get<ECS::Components::Selectable>(e).is_selected) continue;
            if (picked.get<ECS::Components::Owner>(e).player_id !=
                Core::LocalPlayer()) {
                continue;
            }
            CommandQueue::Get().PushCommand(DeleteCommand{e, Core::LocalPlayer()});
        }
        break;
    }
    case Rendering::UIRenderer::Action::Garrison:
        pending_order = PendingOrder::Garrison;
        break;
    case Rendering::UIRenderer::Action::Repair:
        pending_order = PendingOrder::Repair;
        break;
    case Rendering::UIRenderer::Action::LockGate: {
        // Every selected gate of the player's, locked or unlocked as the
        // button says.
        auto picked = registry.view<ECS::Components::Selectable,
                                    ECS::Components::Owner>();
        for (auto e : picked) {
            if (!picked.get<ECS::Components::Selectable>(e).is_selected) continue;
            if (picked.get<ECS::Components::Owner>(e).player_id != Core::LocalPlayer()) {
                continue;
            }
            CommandQueue::Get().PushCommand(
                GateCommand{e, cmd.choice == 1, Core::LocalPlayer()});
        }
        break;
    }
    case Rendering::UIRenderer::Action::Pack: {
        std::vector<entt::entity> engines;
        auto picked = registry.view<ECS::Components::Selectable,
                                    ECS::Components::Owner>();
        for (auto e : picked) {
            if (!picked.get<ECS::Components::Selectable>(e).is_selected) continue;
            if (picked.get<ECS::Components::Owner>(e).player_id != Core::LocalPlayer()) {
                continue;
            }
            engines.push_back(e);
        }
        if (!engines.empty()) {
            CommandQueue::Get().PushCommand(
                PackCommand{engines, cmd.choice == 1, Core::LocalPlayer()});
        }
        break;
    }
    case Rendering::UIRenderer::Action::TrainUnit: {
        // Queued as a command: whether the building may take it
        // (finished, room in the queue, the player can pay) is
        // decided inside the tick by the production module.
        const entt::entity building =
            Rendering::UIRenderer::SelectedBuilding(sim);
        if (building != entt::null) {
            for (int n = 0; n < times; ++n) {
                CommandQueue::Get().PushCommand(TrainCommand{
                    building, cmd.unit_id, Core::LocalPlayer()});
            }
        }
        break;
    }
    case Rendering::UIRenderer::Action::SetStance:
    case Rendering::UIRenderer::Action::Stop: {
        // Applies to every selected unit of the player's.
        auto picked = registry.view<ECS::Components::Selectable,
                                    ECS::Components::Owner>();
        for (auto e : picked) {
            if (!picked.get<ECS::Components::Selectable>(e).is_selected) continue;
            if (picked.get<ECS::Components::Owner>(e).player_id !=
                Core::LocalPlayer()) {
                continue;
            }
            if (cmd.kind == Rendering::UIRenderer::CommandKind::Stance) {
                CommandQueue::Get().PushCommand(StanceCommand{
                    e, static_cast<uint8_t>(cmd.choice), Core::LocalPlayer()});
            } else {
                CommandQueue::Get().PushCommand(
                    StopCommand{e, Core::LocalPlayer()});
            }
        }
        break;
    }
    case Rendering::UIRenderer::Action::Ungarrison:
    case Rendering::UIRenderer::Action::Treason: {
        const entt::entity castle = Rendering::UIRenderer::SelectedBuilding(sim);
        if (castle != entt::null) CommandQueue::Get().PushCommand(TreasonCommand{castle, Core::LocalPlayer()});
        break;
    }
    case Rendering::UIRenderer::Action::TownBell: {
        const entt::entity building =
            Rendering::UIRenderer::SelectedBuilding(sim);
        if (building == entt::null) break;
        if (cmd.kind == Rendering::UIRenderer::CommandKind::Ungarrison) {
            CommandQueue::Get().PushCommand(
                UngarrisonCommand{building, Core::LocalPlayer()});
        } else {
            CommandQueue::Get().PushCommand(
                TownBellCommand{building, Core::LocalPlayer()});
        }
        break;
    }
    case Rendering::UIRenderer::Action::Patrol:
        pending_order = PendingOrder::Patrol;
        break;
    case Rendering::UIRenderer::Action::Guard:
        pending_order = PendingOrder::Guard;
        break;
    case Rendering::UIRenderer::Action::Follow:
        pending_order = PendingOrder::Follow;
        break;
    case Rendering::UIRenderer::Action::Research: {
        const entt::entity building =
            Rendering::UIRenderer::SelectedBuilding(sim);
        if (building != entt::null) {
            CommandQueue::Get().PushCommand(ResearchCommand{
                building, cmd.tech_id, Core::LocalPlayer()});
        }
        break;
    }
    case Rendering::UIRenderer::Action::Sell:
    case Rendering::UIRenderer::Action::Buy: {
        // A lot of 100 at the selected Market; whether it can be paid for is
        // decided inside the tick.
        const entt::entity market = Rendering::UIRenderer::SelectedBuilding(sim);
        if (market != entt::null) {
            CommandQueue::Get().PushCommand(MarketCommand{
                market, static_cast<uint8_t>(cmd.choice), 1,
                cmd.kind == Rendering::UIRenderer::CommandKind::Buy, Core::LocalPlayer()});
        }
        break;
    }
    case Rendering::UIRenderer::Action::None:
        break;
    }
}

void InputManager::ProcessEvent(const SDL_Event& event, ECS::Simulation& sim, Rendering::Renderer& renderer) {
    auto& registry = sim.GetRegistry();
    // The current frame size: Update keeps it too, but is skipped while the
    // camera is held still.
    if (const HAL::Window* window = HAL::MainWindow()) {
        last_win_w = window->GetWidth();
        last_win_h = window->GetHeight();
    }
    float cam_x = renderer.camera_x;
    float cam_y = renderer.camera_y;
    float zoom = renderer.camera_zoom;

    if (event.type == SDL_MOUSEMOTION) {
        if (minimap_dragging) {
            MinimapJump(renderer, sim, event.motion.x, event.motion.y);
        }
        current_mouse_x = event.motion.x;
        current_mouse_y = event.motion.y;
        mouse_in_window = true;
    }
    else if (event.type == SDL_WINDOWEVENT) {
        // Fullscreen, the pointer is held on the picture (hal/Window); if it
        // still leaves the window (the strip beside a MacBook's camera notch),
        // the map goes on scrolling toward where it was last seen. Only
        // losing focus stops it.
        const HAL::Window* main_window = HAL::MainWindow();
        const bool fullscreen = main_window != nullptr && main_window->Fullscreen();
        if (event.window.event == SDL_WINDOWEVENT_ENTER) {
            mouse_in_window = true;
        } else if (event.window.event == SDL_WINDOWEVENT_LEAVE && !fullscreen) {
            mouse_in_window = false;
        } else if (event.window.event == SDL_WINDOWEVENT_FOCUS_LOST) {
            mouse_in_window = false;
        }
    }
    else if (event.type == SDL_KEYDOWN) {
        auto key = event.key.keysym.sym;
        if (key == SDLK_LEFT) pan_left = true;
        if (key == SDLK_RIGHT) pan_right = true;
        if (key == SDLK_UP) pan_up = true;
        if (key == SDLK_DOWN) pan_down = true;
        if (key == SDLK_ESCAPE) {
            // Escape backs out of whatever is half done: a building waiting
            // to be placed, an order waiting for its target, a build menu.
            if (placing_building || pending_order != PendingOrder::None) {
                placing_building = false;
                wall_drag = false;
                pending_order = PendingOrder::None;
            } else {
                Rendering::UIRenderer::OpenBuildMenu(sim, 0);
            }
            return;
        }
        if (event.key.repeat == 0) HandleHotkey(event.key, sim, renderer);
    }
    else if (event.type == SDL_KEYUP) {
        auto key = event.key.keysym.sym;
        if (key == SDLK_LEFT) pan_left = false;
        if (key == SDLK_RIGHT) pan_right = false;
        if (key == SDLK_UP) pan_up = false;
        if (key == SDLK_DOWN) pan_down = false;
    }
    else if (event.type == SDL_MOUSEWHEEL) {
        // The wheel acts as two keys: up and down.
        if (event.wheel.y > 0) HandleMouseKey(Hotkeys::kWheelUp, sim, renderer);
        if (event.wheel.y < 0) HandleMouseKey(Hotkeys::kWheelDown, sim, renderer);
    }
    else if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_MIDDLE) {
        HandleMouseKey(Hotkeys::kMiddleButton, sim, renderer);
    }
    else if (event.type == SDL_MOUSEBUTTONDOWN) {
        float ax = static_cast<float>(event.button.x) / zoom;
        float ay = static_cast<float>(event.button.y) / zoom;

        if (event.button.button == SDL_BUTTON_LEFT) {
            int mx = event.button.x;
            int my = event.button.y;
            // The interface's parts each at their own size: the click in the
            // top bar's terms, and in the panel's.
            const auto top = Rendering::UIRenderer::TopSpace(last_win_w, last_win_h);
            const auto panel = Rendering::UIRenderer::PanelSpace(last_win_w, last_win_h);
            int tx = 0, ty = 0, px = 0, py = 0;
            top.ToSpace(mx, my, tx, ty);
            panel.ToSpace(mx, my, px, py);

            // The top bar's buttons, and those round the mini-map.
            if (const int bar = Rendering::UIRenderer::BarButtonAt(top.w, tx, ty);
                bar >= 0) {
                Rendering::UIRenderer::RequestDialog(bar);
                return;
            }
            if (const int mm = Rendering::UIRenderer::MinimapButtonAt(panel.w, px, py);
                mm >= 0) {
                if (mm == Rendering::kSignalAllies) {
                    pending_order = PendingOrder::Flare;
                } else if (Rendering::UIRenderer::ActivateMinimapButton(mm)) {
                    NextIdleVillager(sim, renderer);
                }
                return;
            }

            // A card in a selected group: clicking one narrows the selection to
            // it; with Shift it leaves the group instead; with Ctrl the group
            // narrows to every unit of its kind.
            {
                Rendering::UIRenderer::SelectionCard cards[Rendering::UIRenderer::kMaxSelectionCards];
                const int shown = Rendering::UIRenderer::BuildSelectionCards(
                    sim, panel.w, last_win_h, cards, Rendering::UIRenderer::kMaxSelectionCards);
                // Topmost card first: a card overlaps the one to its right.
                for (int i = 0; i < shown; ++i) {
                    const SDL_Rect& r = cards[i].rect;
                    if (px < r.x || px >= r.x + r.w || py < r.y || py >= r.y + r.h) continue;
                    const SDL_Keymod mods = SDL_GetModState();
                    const entt::entity picked = cards[i].unit;
                    const auto* kind = registry.try_get<ECS::Components::Identity>(picked);
                    auto sel = registry.view<ECS::Components::Selectable>();
                    for (auto e : sel) {
                        auto& s = sel.get<ECS::Components::Selectable>(e);
                        if (!s.is_selected) continue;
                        if (mods & KMOD_SHIFT) {
                            if (e == picked) s.is_selected = false;
                        } else if (mods & KMOD_CTRL) {
                            const auto* id = registry.try_get<ECS::Components::Identity>(e);
                            s.is_selected = kind != nullptr && id != nullptr &&
                                            id->unit_id == kind->unit_id;
                        } else {
                            s.is_selected = e == picked;
                        }
                    }
                    Audio::Voices::Selected(registry, Core::LocalPlayer());
                    return;
                }
            }

            // The minimap owns the click if the cursor is over its well. The
            // well and the projection both come from the renderer.
            const SDL_FRect well = Rendering::UIRenderer::MinimapRect(panel.w, last_win_h);
            if (px >= well.x && px < well.x + well.w &&
                py >= well.y && py < well.y + well.h) {
                // A flare can be pointed on the mini-map as well as the world.
                float gx = 0.0f, gy = 0.0f;
                if (pending_order == PendingOrder::Flare &&
                    Rendering::UIRenderer::MinimapUnproject(well, static_cast<float>(px),
                                                            static_cast<float>(py),
                                                            sim.GetGrid().GetWidth(),
                                                            sim.GetGrid().GetHeight(), gx, gy)) {
                    GivePendingOrder(sim, gx, gy);
                    return;
                }
                minimap_dragging = true;
                MinimapJump(renderer, sim, mx, my);
                return; // Consume event
            }

            // A unit in a building's queue: clicking it takes it back out, with
            // a full refund. Hit-tested against the very slots the panel drew.
            {
                Rendering::UIRenderer::QueueSlot slots[Rendering::UIRenderer::kMaxQueueSlots];
                const int shown = Rendering::UIRenderer::BuildQueueSlots(
                    sim, panel.w, last_win_h, slots,
                    Rendering::UIRenderer::kMaxQueueSlots);
                for (int i = 0; i < shown; ++i) {
                    const auto& r = slots[i].rect;
                    if (px < r.x || px >= r.x + r.w || py < r.y || py >= r.y + r.h) continue;
                    const entt::entity building =
                        Rendering::UIRenderer::SelectedBuilding(sim);
                    if (building != entt::null) {
                        CommandQueue::Get().PushCommand(CancelTrainCommand{
                            building, slots[i].slot, Core::LocalPlayer()});
                    }
                    return;   // consumed
                }
            }

            // Command buttons, hit-tested against the very list the panel drew.
            Rendering::UIRenderer::Command commands[Rendering::UIRenderer::kCommandSlots];
            const int command_count = Rendering::UIRenderer::BuildCommands(
                sim, panel.w, last_win_h, commands,
                Rendering::UIRenderer::kCommandSlots);
            for (int i = 0; i < command_count; ++i) {
                const auto& cmd = commands[i];
                if (px < cmd.rect.x || px >= cmd.rect.x + cmd.rect.w) continue;
                if (py < cmd.rect.y || py >= cmd.rect.y + cmd.rect.h) continue;
                // Shift asks for five.
                RunCommand(sim, cmd, (SDL_GetModState() & KMOD_SHIFT) ? 5 : 1);
                return; // Consume, whether or not the button could be used
            }

            // Anything else over the panels belongs to the interface, not the
            // world behind it: a click on the frame must not also select units.
            if (Rendering::UIRenderer::OverInterface(last_win_w, last_win_h, mx, my)) {
                return;
            }

            // A standing order waiting for its target takes this click.
            if (pending_order != PendingOrder::None) {
                const auto wc = Rendering::Ground::Pick(sim.GetGrid(), ax + cam_x, ay + cam_y);
                GivePendingOrder(sim, static_cast<float>(wc.first), static_cast<float>(wc.second));
                return;
            }

            // With a building chosen from the build menu, the next click on the
            // world places it.
            if (placing_building) {
                const auto wc = Rendering::Ground::Pick(sim.GetGrid(), ax + cam_x, ay + cam_y);
                // The tile under the cursor: tiles are centred on whole
                // numbers. Anything but a wall goes down with its middle there.
                int gx = static_cast<int>(std::floor(wc.first + 0.5));
                int gy = static_cast<int>(std::floor(wc.second + 0.5));
                if (!ECS::Walls::IsWall(building_to_place) && !ECS::Walls::IsGate(building_to_place)) {
                    Rendering::Placement::OriginTile(building_to_place, wc.first, wc.second, gx, gy);
                }
                // A wall: the press starts the line, the release lays it.
                if (ECS::Walls::IsWall(building_to_place)) {
                    wall_drag = true;
                    wall_from_x = gx;
                    wall_from_y = gy;
                    return;
                }
                // Where the outline shows the ground taken, nothing is laid
                // out: the building stays on the cursor.
                if (!ECS::Walls::SiteOpen(registry, sim.GetGrid(), Core::LocalPlayer(),
                                          building_to_place, gx, gy)) {
                    return;
                }
                auto builders = registry.view<ECS::Components::Selectable,
                                              ECS::Components::Worker>();
                // Every selected villager joins in.
                bool ordered = false;
                for (auto builder : builders) {
                    if (!builders.get<ECS::Components::Selectable>(builder).is_selected) {
                        continue;
                    }
                    CommandQueue::Get().PushCommand(BuildCommand{
                        builder, building_to_place, gx, gy, Core::LocalPlayer()});
                    ordered = true;
                }
                if (ordered) Audio::Voices::Placed(registry, building_to_place, Core::LocalPlayer());
                placing_building = false;
                return;
            }

            is_dragging = true;
            drag_start_x = static_cast<int>(ax);
            drag_start_y = static_cast<int>(ay);
            drag_screen_x = event.button.x;
            drag_screen_y = event.button.y;
        }
        else if (event.button.button == SDL_BUTTON_RIGHT) {
            auto wc = Rendering::Ground::Pick(sim.GetGrid(), ax + cam_x, ay + cam_y);
            // On the mini-map, an order goes to the point of the map it shows:
            // the selected units move to it (with Shift, a waypoint), a
            // building's gather point goes there. Nothing is picked as a target.
            bool on_minimap = false;
            {
                const auto panel = Rendering::UIRenderer::PanelSpace(last_win_w, last_win_h);
                const SDL_FRect well = Rendering::UIRenderer::MinimapRect(panel.w, last_win_h);
                float gx = 0.0f, gy = 0.0f;
                int px = 0, py = 0;
                panel.ToSpace(event.button.x, event.button.y, px, py);
                if (px >= well.x && px < well.x + well.w && py >= well.y && py < well.y + well.h &&
                    Rendering::UIRenderer::MinimapUnproject(well, static_cast<float>(px),
                                                            static_cast<float>(py),
                                                            sim.GetGrid().GetWidth(),
                                                            sim.GetGrid().GetHeight(), gx, gy)) {
                    wc = {gx, gy};
                    on_minimap = true;
                }
            }
            // Otherwise orders are given to the world, not through the panel art.
            if (!on_minimap && Rendering::UIRenderer::OverInterface(last_win_w, last_win_h,
                                                                    event.button.x, event.button.y)) {
                return;
            }
            // A standing order waiting for its target takes a right-click too.
            if (pending_order != PendingOrder::None) {
                GivePendingOrder(sim, static_cast<float>(wc.first), static_cast<float>(wc.second));
                return;
            }
            // Where the click is aimed, not the tile it fell in: the targets
            // below are picked by their distance from it. A click on a
            // unit's, building's or tree's art aims at that object.
            float click_x = static_cast<float>(wc.first);
            float click_y = static_cast<float>(wc.second);
            if (!on_minimap) {
                const entt::entity art = Rendering::EntityRenderer::PickAt(registry, ax, ay);
                if (art != entt::null && ECS::Vision::Shows(registry, Core::LocalPlayer(), art)) {
                    const auto& at = registry.get<ECS::Components::Transform>(art).position;
                    click_x = at.x.ToFloat();
                    click_y = at.y.ToFloat();
                }
            }
            int gx = static_cast<int>(std::floor(wc.first + 0.5));
            int gy = static_cast<int>(std::floor(wc.second + 0.5));

            // How near the cursor has to be to grab something: a little over
            // half a tile.
            constexpr float kPickRadius = 0.75f;
            const float pick_reach = on_minimap ? -1.0f : kPickRadius * kPickRadius;
            auto near_click = [click_x, click_y, on_minimap](const ECS::Components::Transform& t,
                                                             float radius) {
                if (on_minimap) return false;
                const float dx = click_x - t.position.x.ToFloat();
                const float dy = click_y - t.position.y.ToFloat();
                return dx * dx + dy * dy <= radius * radius;
            };

            // Nearest, not first found: two things overlapping under the
            // cursor should resolve to whichever the player is pointing at.
            auto res_view = registry.view<ECS::Components::Transform, ECS::Components::Resource>();
            entt::entity target_res = entt::null;
            float nearest_res = pick_reach;
            for (auto r : res_view) {
                if (!ECS::Vision::Shows(registry, Core::LocalPlayer(), r)) continue;
                auto& t = res_view.get<ECS::Components::Transform>(r);
                const float dx = click_x - t.position.x.ToFloat();
                const float dy = click_y - t.position.y.ToFloat();
                float d2 = dx * dx + dy * dy;
                // A Farm is under the click anywhere on its field, as far as
                // anything right at the cursor goes.
                if (on_minimap) continue;
                if (const auto* id = registry.try_get<ECS::Components::Identity>(r);
                    id != nullptr && registry.all_of<ECS::Components::Building>(r) &&
                    ECS::Buildings::FootprintBounds(t.position, id->unit_id)
                            .DistanceFrom({Fixed32(click_x), Fixed32(click_y)})
                            .value == 0) {
                    d2 = std::min(d2, pick_reach);
                }
                if (d2 <= nearest_res) { nearest_res = d2; target_res = r; }
            }

            // A dead farm of the player's: the selected villagers put a new
            // Farm up on it ("Right-click to rebuild this Farm.").
            if (!on_minimap) {
                int farm_unit = -1, fx = 0, fy = 0;
                if (DeadFarmAt(sim, click_x, click_y, farm_unit, fx, fy)) {
                    bool ordered = false;
                    auto crew = registry.view<ECS::Components::Selectable, ECS::Components::Worker,
                                              ECS::Components::Owner>();
                    for (auto member : crew) {
                        if (!crew.get<ECS::Components::Selectable>(member).is_selected) continue;
                        if (crew.get<ECS::Components::Owner>(member).player_id != Core::LocalPlayer()) continue;
                        if (crew.get<ECS::Components::Worker>(member).gatherer >= 0) continue;
                        CommandQueue::Get().PushCommand(
                            BuildCommand{member, farm_unit, fx, fy, Core::LocalPlayer()});
                        ordered = true;
                    }
                    if (ordered) {
                        Audio::Voices::Placed(registry, farm_unit, Core::LocalPlayer());
                        return;
                    }
                }
            }

            // An unfinished building the player owns is a job, not a target:
            // right-clicking it puts every selected villager to work on it.
            entt::entity resume = entt::null;
            auto site_view = registry.view<ECS::Components::Transform,
                                           ECS::Components::Building,
                                           ECS::Components::Identity>();
            for (auto site : site_view) {
                if (site_view.get<ECS::Components::Building>(site).state !=
                    ECS::Components::BuildingState::UnderConstruction) {
                    continue;
                }
                const auto& t = site_view.get<ECS::Components::Transform>(site);
                const auto* info = ConfigManager::Get().GetUnitInterface(
                    site_view.get<ECS::Components::Identity>(site).unit_id);
                // A building's grab area is its footprint, as a radius from
                // its middle.
                const float footprint = (info && info->footprint > 0)
                                      ? static_cast<float>(info->footprint) : 1.0f;
                if (near_click(t, footprint * 0.5f + kPickRadius)) {
                    resume = site;
                    break;
                }
            }
            if (resume != entt::null) {
                // A build order at the site's own tile joins it, at no cost.
                const auto& at = registry.get<ECS::Components::Transform>(resume).position;
                const int site_unit = registry.get<ECS::Components::Identity>(resume).unit_id;
                auto crew = registry.view<ECS::Components::Selectable,
                                          ECS::Components::Worker>();
                entt::entity first = entt::null;
                for (auto member : crew) {
                    if (!crew.get<ECS::Components::Selectable>(member).is_selected) {
                        continue;
                    }
                    CommandQueue::Get().PushCommand(BuildCommand{
                        member, site_unit, at.x.ToInt(), at.y.ToInt(), Core::LocalPlayer()});
                    if (first == entt::null) first = member;
                }
                if (first != entt::null) {
                    Answered(registry, first, Audio::Voices::Order::Work, resume);
                    return;
                }
            }

            // One of the player's own finished buildings, taken unit by unit:
            // with the garrison key (GarrisonKey), whoever it would shelter
            // goes in; a worker carrying something it accepts drops that off,
            // however little, and stands. If anyone does either, the rest
            // mend it where they can or walk to it.
            if (!on_minimap) {
                entt::entity own = entt::null;
                auto mine = registry.view<ECS::Components::Transform, ECS::Components::Building,
                                          ECS::Components::Identity, ECS::Components::Owner>();
                for (auto b : mine) {
                    if (mine.get<ECS::Components::Owner>(b).player_id != Core::LocalPlayer()) continue;
                    if (mine.get<ECS::Components::Building>(b).state !=
                        ECS::Components::BuildingState::Complete) {
                        continue;
                    }
                    const auto* info = ConfigManager::Get().GetUnitInterface(
                        mine.get<ECS::Components::Identity>(b).unit_id);
                    const float footprint = (info && info->footprint > 0)
                                          ? static_cast<float>(info->footprint) : 1.0f;
                    if (near_click(mine.get<ECS::Components::Transform>(b), footprint * 0.5f)) {
                        own = b;
                        break;
                    }
                }
                if (own != entt::null) {
                    const bool shelter = GarrisonKey();
                    std::vector<entt::entity> rest;
                    entt::entity first = entt::null;
                    auto picked = registry.view<ECS::Components::Selectable, ECS::Components::Owner>();
                    for (auto e : picked) {
                        if (!picked.get<ECS::Components::Selectable>(e).is_selected) continue;
                        if (picked.get<ECS::Components::Owner>(e).player_id != Core::LocalPlayer()) continue;
                        if (registry.all_of<ECS::Components::Building>(e)) continue;
                        if (shelter && ECS::Garrisons::Accepts(registry, own, e)) {
                            CommandQueue::Get().PushCommand(GarrisonCommand{e, own, Core::LocalPlayer()});
                        } else if (ECS::DropSites::Takes(registry, e, own)) {
                            CommandQueue::Get().PushCommand(DropOffCommand{e, own, Core::LocalPlayer()});
                        } else {
                            rest.push_back(e);
                            continue;
                        }
                        if (first == entt::null) first = e;
                    }
                    if (first != entt::null) {
                        std::vector<entt::entity> menders;
                        for (entt::entity e : rest) {
                            if (ECS::Repairs::CanRepair(registry, e, own)) {
                                menders.push_back(e);
                            } else {
                                CommandQueue::Get().PushCommand(MoveCommand{
                                    e, Fixed32(click_x), Fixed32(click_y), Core::LocalPlayer()});
                            }
                        }
                        if (!menders.empty()) {
                            CommandQueue::Get().PushCommand(RepairCommand{menders, own, Core::LocalPlayer()});
                        }
                        Answered(registry, first, Audio::Voices::Order::Move, own);
                        return;
                    }
                }
            }

            // Something of the player's side damaged -- a building, a ship, a
            // trebuchet -- that the selected villagers can mend: they repair it.
            {
                std::vector<entt::entity> menders;
                auto crew = registry.view<ECS::Components::Selectable, ECS::Components::Worker,
                                          ECS::Components::Owner>();
                for (auto member : crew) {
                    if (!crew.get<ECS::Components::Selectable>(member).is_selected) continue;
                    if (crew.get<ECS::Components::Owner>(member).player_id != Core::LocalPlayer()) {
                        continue;
                    }
                    menders.push_back(member);
                }
                entt::entity mend = entt::null;
                if (!menders.empty()) {
                    auto hurt = registry.view<ECS::Components::Transform, ECS::Components::Health,
                                              ECS::Components::Identity>();
                    for (auto h : hurt) {
                        if (!ECS::Repairs::CanRepair(registry, menders.front(), h)) continue;
                        const bool building = registry.all_of<ECS::Components::Building>(h);
                        const auto* info = building ? ConfigManager::Get().GetUnitInterface(
                            hurt.get<ECS::Components::Identity>(h).unit_id) : nullptr;
                        const float reach = (info && info->footprint > 0)
                            ? static_cast<float>(info->footprint) * 0.5f : kPickRadius;
                        if (near_click(hurt.get<ECS::Components::Transform>(h), reach)) {
                            mend = h;
                            break;
                        }
                    }
                }
                if (mend != entt::null) {
                    CommandQueue::Get().PushCommand(
                        RepairCommand{menders, mend, Core::LocalPlayer()});
                    Answered(registry, menders.front(), Audio::Voices::Order::Work, mend);
                    return;
                }
            }

            // A relic errand: a monk sent at a relic fetches it; one carrying a
            // relic, sent at its monastery, puts it there.
            {
                entt::entity first = entt::null;
                entt::entity errand_target = entt::null;
                auto picked = registry.view<ECS::Components::Selectable>();
                auto things = registry.view<ECS::Components::Transform, ECS::Components::Identity>();
                for (auto e : picked) {
                    if (!picked.get<ECS::Components::Selectable>(e).is_selected) continue;
                    for (auto t : things) {
                        const auto& at = things.get<ECS::Components::Transform>(t);
                        const bool building = registry.all_of<ECS::Components::Building>(t);
                        const auto* info = building ? ConfigManager::Get().GetUnitInterface(
                            things.get<ECS::Components::Identity>(t).unit_id) : nullptr;
                        const float reach = (info && info->footprint > 0)
                            ? static_cast<float>(info->footprint) * 0.5f : kPickRadius;
                        if (!near_click(at, reach)) continue;
                        if (ECS::Relics::CanFetch(registry, e, t) ||
                            ECS::Relics::CanDeposit(registry, e, t)) {
                            CommandQueue::Get().PushCommand(RelicCommand{e, t, Core::LocalPlayer()});
                            if (first == entt::null) { first = e; errand_target = t; }
                            break;
                        }
                    }
                }
                if (first != entt::null) {
                    Answered(registry, first, Audio::Voices::Order::Move, errand_target);
                    return;
                }
            }

            // A trade unit sent at a foreign Market or Dock trades with it.
            {
                entt::entity first = entt::null;
                entt::entity partner = entt::null;
                auto picked = registry.view<ECS::Components::Selectable>();
                auto sites = registry.view<ECS::Components::Transform, ECS::Components::Building,
                                           ECS::Components::Identity>();
                for (auto e : picked) {
                    if (!picked.get<ECS::Components::Selectable>(e).is_selected) continue;
                    for (auto b : sites) {
                        const auto* info = ConfigManager::Get().GetUnitInterface(
                            sites.get<ECS::Components::Identity>(b).unit_id);
                        const float reach = (info && info->footprint > 0)
                            ? static_cast<float>(info->footprint) * 0.5f : kPickRadius;
                        if (!near_click(sites.get<ECS::Components::Transform>(b), reach)) continue;
                        if (!ECS::Trade::CanTrade(registry, e, b)) continue;
                        CommandQueue::Get().PushCommand(TradeCommand{e, b, Core::LocalPlayer()});
                        if (first == entt::null) { first = e; partner = b; }
                        break;
                    }
                }
                if (first != entt::null) {
                    Answered(registry, first, Audio::Voices::Order::Move, partner);
                    return;
                }
            }

            // One of the player's own transport ships: units board it.
            {
                entt::entity shelter = entt::null;
                auto ships = registry.view<ECS::Components::Transform, ECS::Components::Owner>();
                for (auto s : ships) {
                    if (ships.get<ECS::Components::Owner>(s).player_id != Core::LocalPlayer()) continue;
                    if (!ECS::Garrisons::IsTransport(registry, s)) continue;
                    if (near_click(ships.get<ECS::Components::Transform>(s), kPickRadius)) {
                        shelter = s;
                        break;
                    }
                }
                if (shelter != entt::null) {
                    entt::entity first = entt::null;
                    auto picked = registry.view<ECS::Components::Selectable>();
                    for (auto e : picked) {
                        if (!picked.get<ECS::Components::Selectable>(e).is_selected) continue;
                        if (!ECS::Garrisons::Accepts(registry, shelter, e)) continue;
                        CommandQueue::Get().PushCommand(
                            GarrisonCommand{e, shelter, Core::LocalPlayer()});
                        if (first == entt::null) first = e;
                    }
                    if (first != entt::null) {
                        Answered(registry, first, Audio::Voices::Order::Move, shelter);
                        return;
                    }
                }
            }

            auto hp_view = registry.view<ECS::Components::Transform, ECS::Components::Health>();
            entt::entity target_enemy = entt::null;
            float nearest_enemy = pick_reach;
            for (auto h : hp_view) {
                // Nothing hidden in the fog can be targeted.
                if (!ECS::Vision::Shows(registry, Core::LocalPlayer(), h)) continue;
                // Never one's own: that takes a force-attack.
                if (const auto* owner = registry.try_get<ECS::Components::Owner>(h)) {
                    if (owner->player_id == Core::LocalPlayer()) continue;
                }
                auto& t = hp_view.get<ECS::Components::Transform>(h);
                const float dx = click_x - t.position.x.ToFloat();
                const float dy = click_y - t.position.y.ToFloat();
                const float d2 = dx * dx + dy * dy;
                if (d2 <= nearest_enemy) { nearest_enemy = d2; target_enemy = h; }
            }

            // One of the player's own wounded, for a monk to heal.
            entt::entity target_heal = entt::null;
            {
                float nearest_heal = pick_reach;
                for (auto h : hp_view) {
                    const auto* owner = registry.try_get<ECS::Components::Owner>(h);
                    if (owner == nullptr || owner->player_id != Core::LocalPlayer()) continue;
                    if (registry.all_of<ECS::Components::Building>(h)) continue;
                    const auto& health = hp_view.get<ECS::Components::Health>(h);
                    if (health.current_hp >= health.max_hp) continue;
                    auto& t = hp_view.get<ECS::Components::Transform>(h);
                    const float dx = click_x - t.position.x.ToFloat();
                    const float dy = click_y - t.position.y.ToFloat();
                    const float d2 = dx * dx + dy * dy;
                    if (d2 <= nearest_heal) { nearest_heal = d2; target_heal = h; }
                }
            }

            auto sel_view = registry.view<ECS::Components::Selectable>();

            std::vector<entt::entity> marchers;
            // The first unit given an order answers it, once for the group.
            entt::entity speaker = entt::null;
            Audio::Voices::Order spoken = Audio::Voices::Order::Move;
            entt::entity spoken_at = entt::null;
            for (auto e : sel_view) {
                if (!sel_view.get<ECS::Components::Selectable>(e).is_selected) continue;

                // A building that can shoot is sent at an enemy or an animal;
                // otherwise it takes a right-click as its rally point: where
                // the units it trains go, or what villagers it trains start on.
                if (registry.all_of<ECS::Components::Building>(e)) {
                    const auto* theirs = registry.valid(target_enemy)
                                             ? registry.try_get<ECS::Components::Owner>(target_enemy) : nullptr;
                    const bool foe = theirs == nullptr || theirs->player_id == Core::PLAYER_GAIA ||
                                     ECS::Diplomacy::StanceOf(registry, Core::LocalPlayer(), theirs->player_id) !=
                                         ECS::Diplomacy::Stance::Ally;
                    if (registry.valid(target_enemy) && target_enemy != e && foe && Shoots(registry, e)) {
                        CommandQueue::Get().PushCommand(AttackCommand{e, target_enemy, Core::LocalPlayer()});
                        Rendering::EntityRenderer::Flash(registry, target_enemy, true);
                    } else {
                        CommandQueue::Get().PushCommand(RallyCommand{
                            e, Fixed32(click_x), Fixed32(click_y), target_res,
                            Core::LocalPlayer()});
                    }
                    continue;
                }
                
                if (placing_building && registry.any_of<ECS::Components::Worker>(e)) {
                    int px = gx, py = gy;
                    if (!ECS::Walls::IsWall(building_to_place) && !ECS::Walls::IsGate(building_to_place)) {
                        Rendering::Placement::OriginTile(building_to_place, wc.first, wc.second, px, py);
                    }
                    if (!ECS::Walls::SiteOpen(registry, sim.GetGrid(), Core::LocalPlayer(),
                                              building_to_place, px, py)) {
                        continue;
                    }
                    CommandQueue::Get().PushCommand(BuildCommand{
                        e, building_to_place, px, py, Core::LocalPlayer()});
                    placing_building = false; // Turn off placement after issuing order
                    wall_drag = false;
                } else if (registry.valid(target_res) && registry.any_of<ECS::Components::Worker>(e)) {
                    // A villager gathers what is there: an animal is hunted,
                    // not attacked.
                    CommandQueue::Get().PushCommand(
                        GatherCommand{e, target_res, Core::LocalPlayer()});
                    if (speaker == entt::null) {
                        speaker = e;
                        spoken = Audio::Voices::Order::Work;
                        spoken_at = target_res;
                    }
                } else if (registry.valid(target_heal) && registry.any_of<ECS::Components::Monk>(e) &&
                           e != target_heal) {
                    // A monk sent at one of its own side's wounded heals it.
                    CommandQueue::Get().PushCommand(
                        HealCommand{e, target_heal, Core::LocalPlayer()});
                    if (speaker == entt::null) {
                        speaker = e;
                        spoken = Audio::Voices::Order::Move;
                        spoken_at = target_heal;
                    }
                } else if (registry.valid(target_enemy) &&
                           (registry.any_of<ECS::Components::Combat, ECS::Components::Monk>(e) ||
                            ECS::Packing::Packed(registry, e)) &&
                           e != target_enemy) {
                    CommandQueue::Get().PushCommand(
                        AttackCommand{e, target_enemy, Core::LocalPlayer()});
                    if (speaker == entt::null) {
                        speaker = e;
                        spoken = Audio::Voices::Order::Attack;
                        spoken_at = target_enemy;
                    }
                } else {
                    marchers.push_back(e);
                }
            }
            if (speaker == entt::null && !marchers.empty()) speaker = marchers.front();
            if (speaker != entt::null) {
                Answered(registry, speaker, spoken, spoken_at);
            }
            // A move: one unit walks to the point; a group marches there in
            // the formation chosen, laid out by the simulation. Arrows mark
            // the spot.
            if (!marchers.empty()) {
                Rendering::EntityRenderer::AddMoveMarker(static_cast<float>(wc.first),
                                                         static_cast<float>(wc.second));
            }
            if (!marchers.empty() && (SDL_GetModState() & KMOD_SHIFT)) {
                // Shift: a waypoint, after wherever they are going now.
                CommandQueue::Get().PushCommand(WaypointCommand{
                    marchers, Fixed32(static_cast<float>(wc.first)),
                    Fixed32(static_cast<float>(wc.second)),
                    Rendering::UIRenderer::CurrentFormation(), Core::LocalPlayer()});
            } else if (marchers.size() == 1) {
                CommandQueue::Get().PushCommand(MoveCommand{
                    marchers.front(), Fixed32(static_cast<float>(wc.first)),
                    Fixed32(static_cast<float>(wc.second)), Core::LocalPlayer()});
            } else if (marchers.size() > 1) {
                CommandQueue::Get().PushCommand(FormationMoveCommand{
                    marchers, Fixed32(static_cast<float>(wc.first)),
                    Fixed32(static_cast<float>(wc.second)),
                    Rendering::UIRenderer::CurrentFormation(), Core::LocalPlayer()});
            }
        }
    }
    else if (event.type == SDL_MOUSEBUTTONUP) {
        if (event.button.button == SDL_BUTTON_LEFT && minimap_dragging) {
            minimap_dragging = false;
            return;  // the press was consumed by the minimap, so is the release
        }
        // A wall dragged out: laid from the tile the drag began on to this
        // one, by every selected villager, in one order.
        if (event.button.button == SDL_BUTTON_LEFT && wall_drag) {
            wall_drag = false;
            placing_building = false;
            const auto wc = Rendering::Ground::Pick(
                sim.GetGrid(), static_cast<float>(event.button.x) / zoom + cam_x,
                static_cast<float>(event.button.y) / zoom + cam_y);
            const int gx = static_cast<int>(std::floor(wc.first + 0.5));
            const int gy = static_cast<int>(std::floor(wc.second + 0.5));
            std::vector<entt::entity> workers;
            auto builders = registry.view<ECS::Components::Selectable, ECS::Components::Worker,
                                          ECS::Components::Owner>();
            for (auto builder : builders) {
                if (!builders.get<ECS::Components::Selectable>(builder).is_selected) continue;
                if (builders.get<ECS::Components::Owner>(builder).player_id != Core::LocalPlayer()) {
                    continue;
                }
                workers.push_back(builder);
            }
            if (!workers.empty()) {
                CommandQueue::Get().PushCommand(WallCommand{workers, building_to_place, wall_from_x,
                                                            wall_from_y, gx, gy, Core::LocalPlayer()});
                Audio::Voices::Placed(registry, building_to_place, Core::LocalPlayer());
            }
            return;
        }
        if (event.button.button == SDL_BUTTON_LEFT && is_dragging) {
            is_dragging = false;
            float dex = static_cast<float>(event.button.x) / zoom;
            float dey = static_cast<float>(event.button.y) / zoom;

            int x1 = std::min(drag_start_x, static_cast<int>(dex));
            int x2 = std::max(drag_start_x, static_cast<int>(dex));
            int y1 = std::min(drag_start_y, static_cast<int>(dey));
            int y2 = std::max(drag_start_y, static_cast<int>(dey));

            const bool click = (x2 - x1 < 5) && (y2 - y1 < 5);
            auto view = registry.view<ECS::Components::Transform, ECS::Components::Selectable>();

            if (click) {
                // A click picks exactly one thing: what its art is under the
                // pointer, or else the nearest building or object on the
                // ground there, and among equals the frontmost. Units are
                // found only by their art.
                const auto wc = Rendering::Ground::Pick(sim.GetGrid(), dex + cam_x, dey + cam_y);
                entt::entity best = entt::null;
                float best_dist = 0.0f;
                float best_depth = 0.0f;
                int best_rank = -1;
                for (auto e : view) {
                    // What the player cannot see cannot be clicked.
                    if (!ECS::Vision::Shows(registry, Core::LocalPlayer(), e)) continue;
                    if (Rendering::EntityRenderer::PickedByArt(registry, e)) continue;
                    const auto& t = view.get<ECS::Components::Transform>(e);
                    const float ux = t.position.x.ToFloat();
                    const float uy = t.position.y.ToFloat();
                    const float ddx = ux - static_cast<float>(wc.first);
                    const float ddy = uy - static_cast<float>(wc.second);
                    const float d = ddx * ddx + ddy * ddy;
                    // Within the pick radius, or on a building's footprint: a
                    // Farm's field is ground, not art.
                    const auto* identity = registry.try_get<ECS::Components::Identity>(e);
                    const bool building = registry.all_of<ECS::Components::Building>(e);
                    const bool on_footprint =
                        building && identity != nullptr &&
                        ECS::Buildings::FootprintBounds(t.position, identity->unit_id)
                                .DistanceFrom({Fixed32(static_cast<float>(wc.first)),
                                               Fixed32(static_cast<float>(wc.second))}) == Fixed32(0);
                    if (d > 1.0f && !on_footprint) continue;
                    const float depth = ux + uy;         // larger means nearer the viewer
                    const int rank =
                        view.get<ECS::Components::Selectable>(e).priority;
                    // A higher selection priority wins regardless of
                    // distance; among equals the nearer, then the frontmost.
                    const bool nearer = d < best_dist - 0.02f;
                    const bool level = std::abs(d - best_dist) <= 0.02f;
                    const bool better = best == entt::null || rank > best_rank ||
                                        (rank == best_rank && (nearer || (level && depth > best_depth)));
                    if (better) {
                        best = e;
                        best_dist = d;
                        best_depth = depth;
                        best_rank = rank;
                    }
                }
                if (const entt::entity art = Rendering::EntityRenderer::PickAt(registry, dex, dey);
                    art != entt::null && registry.all_of<ECS::Components::Selectable>(art) &&
                    ECS::Vision::Shows(registry, Core::LocalPlayer(), art)) {
                    best = art;
                }
                const SDL_Keymod mods = SDL_GetModState();
                const bool adding = (mods & (KMOD_CTRL | KMOD_SHIFT | KMOD_GUI)) != 0;
                auto own = [&](entt::entity e) {
                    const auto* o = registry.try_get<ECS::Components::Owner>(e);
                    return o != nullptr && o->player_id == Core::LocalPlayer();
                };
                if (best != entt::null && event.button.clicks >= 2 && own(best) &&
                    registry.all_of<ECS::Components::Identity>(best)) {
                    // A double-click takes every one of the player's units of
                    // that kind on the screen. Villagers are one kind whatever
                    // their sex or job (men and women are two dat units).
                    const int kind = registry.get<ECS::Components::Identity>(best).unit_id;
                    auto villager = [&](entt::entity e) {
                        const auto* w = registry.try_get<ECS::Components::Worker>(e);
                        return w != nullptr && w->gatherer < 0;
                    };
                    const bool villagers = villager(best);
                    int taken = 0;
                    for (auto e : view) {
                        auto& sel = view.get<ECS::Components::Selectable>(e);
                        sel.is_selected = false;
                        if (taken >= kMaxSelection || !own(e)) continue;
                        const auto* id = registry.try_get<ECS::Components::Identity>(e);
                        if (id == nullptr) continue;
                        if (villagers ? !villager(e) : id->unit_id != kind) continue;
                        const auto& t = view.get<ECS::Components::Transform>(e);
                        auto sp = IsometricMath::WorldToScreen(t.position.x, t.position.y);
                        const float asx = sp.x - cam_x;
                        const float asy = sp.y - cam_y;
                        if (asx < 0 || asy < 0 || asx >= last_win_w / zoom || asy >= last_win_h / zoom) {
                            continue;
                        }
                        sel.is_selected = true;
                        ++taken;
                    }
                } else if (adding && best != entt::null && own(best) &&
                           !registry.all_of<ECS::Components::Building>(best)) {
                    // Ctrl- or Shift-click puts one of the player's units into
                    // the selection, or takes it back out.
                    int count = 0;
                    for (auto e : view) {
                        auto& sel = view.get<ECS::Components::Selectable>(e);
                        if (sel.is_selected && (!own(e) || registry.all_of<ECS::Components::Building>(e))) {
                            sel.is_selected = false;
                        }
                        count += sel.is_selected ? 1 : 0;
                    }
                    auto& picked = view.get<ECS::Components::Selectable>(best);
                    if (picked.is_selected) {
                        picked.is_selected = false;
                    } else if (count < kMaxSelection) {
                        picked.is_selected = true;
                    }
                } else {
                    for (auto e : view) {
                        view.get<ECS::Components::Selectable>(e).is_selected = (e == best);
                    }
                }
                Audio::Voices::Selected(registry, Core::LocalPlayer());
            } else {
                // A box takes the player's own units, and its buildings only
                // when it caught no units at all. Ctrl or Shift adds to what
                // is already selected. Forty at most.
                const SDL_Keymod mods = SDL_GetModState();
                const bool adding = (mods & (KMOD_CTRL | KMOD_SHIFT | KMOD_GUI)) != 0;
                std::vector<entt::entity> units, buildings;
                for (auto e : view) {
                    // Scenery is never swept up by a box; the player's own
                    // sheep and turkeys are.
                    if (view.get<ECS::Components::Selectable>(e).priority <= 0 &&
                        !ECS::Animals::Herdable(registry, e)) {
                        continue;
                    }
                    const auto* o = registry.try_get<ECS::Components::Owner>(e);
                    if (o == nullptr || o->player_id != Core::LocalPlayer()) continue;
                    const auto& t = view.get<ECS::Components::Transform>(e);
                    auto sp = IsometricMath::WorldToScreen(t.position.x, t.position.y);
                    const float asx = sp.x - cam_x;
                    // Where it is drawn: on a hill, higher up the screen.
                    const float asy = sp.y - cam_y -
                        Rendering::Ground::HeightPx(sim.GetGrid(), t.position.x.ToFloat(),
                                                    t.position.y.ToFloat());
                    if (asx < x1 || asx > x2 || asy < y1 || asy > y2) continue;
                    (registry.all_of<ECS::Components::Building>(e) ? buildings : units).push_back(e);
                }
                const auto& caught = units.empty() ? buildings : units;
                int count = 0;
                for (auto e : view) {
                    auto& sel = view.get<ECS::Components::Selectable>(e);
                    if (!adding || (!units.empty() && registry.all_of<ECS::Components::Building>(e))) {
                        sel.is_selected = false;
                    }
                    count += sel.is_selected ? 1 : 0;
                }
                for (auto e : caught) {
                    auto& sel = registry.get<ECS::Components::Selectable>(e);
                    if (sel.is_selected || count >= kMaxSelection) continue;
                    sel.is_selected = true;
                    ++count;
                }
                Audio::Voices::Selected(registry, Core::LocalPlayer());
            }
        }
    }
}

} // namespace Core
