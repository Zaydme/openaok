#include "rendering/Alerts.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "audio/Alarms.h"
#include "core/ConfigManager.h"
#include "core/Rules.h"
#include "core/Strings.h"
#include "ecs/BuildingGeometry.h"
#include "ecs/Events.h"
#include "ecs/Flares.h"
#include "ecs/Players.h"
#include "ecs/Victory.h"
#include "ecs/VillagerJobs.h"
#include "ecs/components/Building.h"
#include "ecs/components/Identity.h"
#include "ecs/components/Owner.h"
#include "ecs/components/ProductionQueue.h"
#include "hal/GraphicCache.h"
#include "rendering/EntityRenderer.h"
#include "rendering/Messages.h"
#include "rendering/UIRenderer.h"

namespace Rendering::Alerts {

namespace {

constexpr int kSlots = 5;

struct Notice {
    bool active = false;
    entt::entity subject = entt::null;
    ECS::Components::Vector2D at{};
    uint32_t raised = 0;   // SDL ticks
};

struct State {
    std::array<Notice, kSlots> notices{};
    int next = 0;          // the slot the next new notice takes
    int last = -1;         // the newest
    int cursor = 0;        // where the last-notification key is
    bool wrapped = false;  // every slot has been used
    uint64_t heard = 0;    // the last tick of the event log taken
    uint64_t flares = 0;   // the last flare taken, by its tick
    // What the player's buildings were researching when last looked: the
    // building a finished technology came from.
    struct Research {
        entt::entity building = entt::null;
        int tech = -1;
    };
    std::vector<Research> researching;
};
State g;

// The square's half-widths and palette colour against its age.
struct Stage {
    uint32_t until_ms;
    int outer;
    int colour;
};
constexpr Stage kStages[] = {
    {700, 16, 0x47}, {1000, 11, 0x46}, {1300, 8, 0x45},
    {1600, 6, 0x44}, {1900, 4, 0x43},  {2400, 2, 0x42},
};

// The classes whose finished buildings raise no notice: walls (27) and 50.
constexpr int kWallClass = 27;
constexpr int kQuietClass = 50;

// LANGUAGE.DLL's line for each notification, written with it.
constexpr int kResearchComplete = 37157;  // "--%s Research Complete--"
constexpr int kBuilt = 37158;             // "--%s Built--"
constexpr int kCreated = 37159;           // "--%s Created--"
constexpr int kAttackedBy = 37160;        // "--Warning: You are being attacked by %s!!!--"
constexpr int kSheepFound = 37161;
constexpr int kFarmExhausted = 37162;
constexpr int kFishTrapExpired = 37163;
constexpr int kRelicPickedUp = 37164;
constexpr int kWildAnimals = 37165;

// A notification's line, with the name its words take, if any.
void Caption(int string_id, const std::string& name = std::string()) {
    char text[256];
    std::snprintf(text, sizeof(text), Core::Strings::Get(string_id).c_str(), name.c_str());
    Messages::Post(text);
}

// What a unit or building of `player`'s is called: the unit its slot has
// become, as the object panel names it.
const std::string& NameOf(const entt::registry& registry, Core::PlayerId player, int unit_id) {
    return Core::ConfigManager::Get().GetUnitName(ECS::Players::ArtOf(registry, player, unit_id));
}

// Where a thing is: a building's middle, anything else's position.
ECS::Components::Vector2D WhereIs(const entt::registry& registry, entt::entity entity,
                                  const ECS::Components::Vector2D& fallback) {
    if (!registry.valid(entity)) return fallback;
    const auto* at = registry.try_get<ECS::Components::Transform>(entity);
    if (at == nullptr) return fallback;
    const auto* id = registry.try_get<ECS::Components::Identity>(entity);
    if (id != nullptr && registry.all_of<ECS::Components::Building>(entity)) {
        const auto bounds = ECS::Buildings::FootprintBounds(at->position, id->unit_id);
        return {(bounds.min_x + bounds.max_x) / 2, (bounds.min_y + bounds.max_y) / 2};
    }
    return at->position;
}

// The building of the player's that researched `tech`: the one that had it
// at the front of its queue when last looked.
entt::entity ResearcherOf(const entt::registry& registry, int tech) {
    for (const State::Research& r : g.researching) {
        if (r.tech == tech && registry.valid(r.building)) return r.building;
    }
    return entt::null;
}

// The building of the player's a unit came out of: the producing building
// nearest the tile it stepped out onto, which is beside its own.
entt::entity TrainerOf(const entt::registry& registry, Core::PlayerId viewer,
                       const ECS::Components::Vector2D& door) {
    constexpr float kReach2 = 8.0f * 8.0f;
    entt::entity best = entt::null;
    float best_d2 = kReach2;
    auto queues = registry.view<ECS::Components::ProductionQueue, ECS::Components::Owner>();
    for (auto building : queues) {
        if (queues.get<ECS::Components::Owner>(building).player_id != viewer) continue;
        const auto at = WhereIs(registry, building, door);
        const float dx = (at.x - door.x).ToFloat();
        const float dy = (at.y - door.y).ToFloat();
        const float d2 = dx * dx + dy * dy;
        if (d2 < best_d2) {
            best = building;
            best_d2 = d2;
        }
    }
    return best;
}

// One event of the log. `out`: the player is defeated, and is told nothing.
void Take(const entt::registry& registry, const ECS::Events::Event& event, Core::PlayerId viewer,
          bool out) {
    switch (event.kind) {
    case ECS::Events::Kind::Born: {
        if (event.player != viewer || out) return;
        Caption(kCreated, NameOf(registry, viewer, event.id));
        // At the building that trained it; the unit's own place were none
        // found.
        const entt::entity maker = TrainerOf(registry, viewer, event.at);
        if (maker != entt::null) {
            Raise(maker, WhereIs(registry, maker, event.at));
        } else {
            Raise(event.subject, event.at);
        }
        return;
    }
    case ECS::Events::Kind::Built: {
        if (event.player != viewer || out) return;
        const auto* ui = Core::ConfigManager::Get().GetUnitInterface(event.id);
        if (ui != nullptr && (ui->unit_class == kWallClass || ui->unit_class == kQuietClass)) return;
        Caption(kBuilt, NameOf(registry, viewer, event.id));
        Raise(event.subject, WhereIs(registry, event.subject, event.at));
        return;
    }
    case ECS::Events::Kind::Researched: {
        // What a building of the player's researched; what comes of itself,
        // a building's own technology, says nothing.
        if (event.player != viewer || out) return;
        const entt::entity maker = ResearcherOf(registry, event.id);
        if (maker == entt::null) return;
        Caption(kResearchComplete, Core::Rules::TechLabel(event.id));
        Raise(maker, WhereIs(registry, maker, event.at));
        return;
    }
    case ECS::Events::Kind::Claimed:
        if (event.player != viewer || out) return;
        Caption(kSheepFound);
        Raise(event.subject, event.at);
        return;
    case ECS::Events::Kind::Exhausted: {
        // A farm is the villager's to build; the fishing ship's is a fish
        // trap.
        if (event.player != viewer || out) return;
        const auto* rules = Core::Rules::Unit(event.id);
        const bool farm = rules != nullptr &&
            rules->train_loc == ECS::Villagers::UnitId(ECS::Components::VillagerJob::Build, false);
        Caption(farm ? kFarmExhausted : kFishTrapExpired);
        Raise(entt::null, event.at);
        return;
    }
    case ECS::Events::Kind::PickedUp:
        if (event.player != viewer || out) return;
        Caption(kRelicPickedUp);
        Raise(event.subject, event.at);
        return;
    case ECS::Events::Kind::Converted:
        if (event.player == viewer) {
            EntityRenderer::Flash(registry, event.subject, false);
        } else if (event.id == viewer && !out) {
            Raise(event.subject, WhereIs(registry, event.subject, event.at));
        }
        return;
    default:
        return;
    }
}

// What the player's buildings are researching now.
void LookAtQueues(const entt::registry& registry, Core::PlayerId viewer) {
    g.researching.clear();
    auto queues = registry.view<ECS::Components::ProductionQueue, ECS::Components::Owner>();
    for (auto building : queues) {
        if (queues.get<ECS::Components::Owner>(building).player_id != viewer) continue;
        const auto& queue = queues.get<ECS::Components::ProductionQueue>(building);
        if (queue.count <= 0 || !queue.items[0].tech) continue;
        g.researching.push_back({building, queue.items[0].id});
    }
}

} // namespace

void Raise(entt::entity subject, const ECS::Components::Vector2D& at) {
    const uint32_t now = SDL_GetTicks();
    // The thing the newest was about again: that one starts over, where the
    // thing is now.
    if (g.last >= 0 && subject != entt::null) {
        Notice& newest = g.notices[static_cast<size_t>(g.last)];
        if (newest.active && newest.subject == subject) {
            newest.raised = now;
            newest.at = at;
            g.cursor = g.last;
            return;
        }
    }
    g.notices[static_cast<size_t>(g.next)] = {true, subject, at, now};
    g.cursor = g.next;
    g.last = g.next;
    if (++g.next >= kSlots) {
        g.next = 0;
        g.wrapped = true;
    }
}

void Update(const entt::registry& registry, Core::PlayerId viewer) {
    const auto* outcome = ECS::Victory::Read(registry);
    const bool out = outcome != nullptr && outcome->Defeated(viewer);

    // The attack alarms that sounded since the last frame, and the line
    // naming who attacks; a monk at work has none.
    Audio::Alarms::Raised alarm;
    while (Audio::Alarms::TakeRaised(alarm)) {
        if (!alarm.conversion) {
            if (alarm.by == Core::PLAYER_GAIA) Caption(kWildAnimals);
            else Caption(kAttackedBy, UIRenderer::PlayerName(registry, alarm.by));
        }
        Raise(alarm.subject, alarm.at);
    }

    if (const ECS::Events::Log* log = ECS::Events::Read(registry)) {
        // A new world starts its clock again.
        if (log->tick < g.heard) {
            g.heard = 0;
            g.flares = 0;
        }
        for (const ECS::Events::Event& event : log->events) {
            if (event.tick <= g.heard) continue;
            Take(registry, event, viewer, out);
        }
        g.heard = log->tick;
    }
    if (const ECS::Flares::Log* flares = ECS::Flares::Read(registry)) {
        uint64_t newest = g.flares;
        for (const ECS::Flares::Flare& flare : flares->flares) {
            if (flare.tick > g.flares && flare.Seen(viewer)) Raise(entt::null, flare.at);
            newest = std::max(newest, flare.tick);
        }
        g.flares = newest;
    }
    LookAtQueues(registry, viewer);
}

void Draw(SDL_Renderer* sdl_r, const SDL_FRect& well, int map_w, int map_h) {
    const uint32_t now = SDL_GetTicks();
    auto& graphics = HAL::GraphicCache::Get();
    SDL_Rect clip_was{};
    const bool clipped = SDL_RenderIsClipEnabled(sdl_r) == SDL_TRUE;
    if (clipped) SDL_RenderGetClipRect(sdl_r, &clip_was);
    const SDL_Rect clip = {static_cast<int>(well.x), static_cast<int>(well.y),
                           static_cast<int>(well.w), static_cast<int>(well.h)};
    SDL_RenderSetClipRect(sdl_r, &clip);
    for (const Notice& notice : g.notices) {
        if (!notice.active) continue;
        const uint32_t age = now - notice.raised;
        const Stage* stage = nullptr;
        for (const Stage& s : kStages) {
            if (age < s.until_ms) {
                stage = &s;
                break;
            }
        }
        if (stage == nullptr) continue;
        const SDL_FPoint p = UIRenderer::MinimapProject(well, notice.at.x.ToFloat(),
                                                        notice.at.y.ToFloat(), map_w, map_h);
        const int x = static_cast<int>(std::floor(p.x));
        const int y = static_cast<int>(std::floor(p.y));
        const HAL::PaletteColor c = graphics.GetPaletteColor(static_cast<size_t>(stage->colour));
        SDL_SetRenderDrawColor(sdl_r, c.r, c.g, c.b, 255);
        // Two outlines, the second a pixel inside the first, each drawn
        // corner to corner inclusive.
        for (int h : {stage->outer, stage->outer - 1}) {
            const SDL_Rect r = {x - h, y - h, 2 * h + 1, 2 * h + 1};
            SDL_RenderDrawRect(sdl_r, &r);
        }
    }
    SDL_RenderSetClipRect(sdl_r, clipped ? &clip_was : nullptr);
}

bool Next(ECS::Components::Vector2D& at) {
    int cur = g.cursor;
    if (!g.notices[static_cast<size_t>(cur)].active) {
        if (g.last < 0) return false;
        cur = g.last;
    }
    at = g.notices[static_cast<size_t>(cur)].at;
    // Back one, round to the newest -- or, once all five have been used, to
    // the last slot.
    g.cursor = cur - 1;
    if (g.cursor < 0) g.cursor = g.wrapped ? kSlots - 1 : g.last;
    return true;
}

void Reset() {
    g = State{};
}

} // namespace Rendering::Alerts
