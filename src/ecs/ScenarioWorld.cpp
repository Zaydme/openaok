#include "ecs/ScenarioWorld.h"

#include <algorithm>
#include <iostream>
#include <string>
#include <vector>

#include "core/Cliffs.h"
#include "core/ConfigManager.h"
#include "core/FixedMath.h"
#include "core/MapGenerator.h"
#include "core/Random.h"
#include "core/Rules.h"
#include "ecs/Buildings.h"
#include "ecs/Diplomacy.h"
#include "ecs/Garrisons.h"
#include "ecs/Players.h"
#include "ecs/Simulation.h"
#include "ecs/Techs.h"
#include "ecs/Triggers.h"
#include "ecs/Units.h"
#include "ecs/Vision.h"
#include "ecs/components/Building.h"
#include "ecs/components/Garrison.h"
#include "ecs/components/Health.h"
#include "ecs/components/Identity.h"
#include "ecs/components/Mortal.h"
#include "ecs/components/Owner.h"
#include "ecs/components/Resource.h"
#include "ecs/components/Sprite.h"
#include "ecs/components/Transform.h"
#include "scenario/Scenario.h"

namespace ECS::ScenarioWorld {

namespace {

namespace C = Components;
namespace R = Core::Rules;

// The dat's classes this module tells apart.
constexpr int kTreeClass = 15;
constexpr int kCliffClass = 34;
// Gaia's living things that are the map's rather than anybody's units: prey,
// predators, the hawks (miscellaneous), domestic animals, birds, relics and
// livestock. Placed as a random map places them; Gaia's soldiers and
// villagers are whole units.
constexpr int kWildClasses[] = {9, 10, 11, 29, 38, 42, 58};

// The engine has no TerrainType of these; each is laid as the ground the dat
// draws it as (its TerrainToDraw), except the Water Bridge -- see the header.
struct DrawsAs {
    int dat;
    int as;
};
constexpr DrawsAs kDrawsAs[] = {
    {14, 13},  // Desert -> Palm Desert (the engine's Sand)
    {15, 1},   // Old Water -> Water
    {16, 0},   // Old Grass -> Grass
    {17, 0},   // Unused 5 -> Grass
    {18, 10},  // Bamboo -> Forest
    {19, 10},  // Pine Forest -> Forest
    {20, 10},  // Oak Forest -> Forest
    {21, 10},  // Unused 7 -> Forest
    {28, 4},   // Water Bridge -> Shallows: drawn as water, crossed as land
};

bool Wild(int cls) {
    for (int c : kWildClasses) {
        if (c == cls) return true;
    }
    return false;
}

Core::Random& RandomOf(entt::registry& registry) {
    if (auto* random = registry.ctx().find<Core::Random>()) return *random;
    return registry.ctx().emplace<Core::Random>(1u);
}

// What a map object holds for the taking: the random maps' own table first
// (it knows a fish's "fish" is food), then the dat's gatherable storage.
bool Worth(int unit_id, Core::ResourceType& type, int& amount) {
    if (const auto* gaia = Core::ConfigManager::Get().GetGaiaObject(unit_id)) {
        if (gaia->resource >= 0) {
            type = static_cast<Core::ResourceType>(gaia->resource);
            amount = gaia->amount;
            return true;
        }
    }
    const R::UnitRules* rules = R::Unit(unit_id);
    if (rules == nullptr) return false;
    for (int i = 0; i < rules->storage_count; ++i) {
        const R::Storage& store = rules->storage[i];
        if (store.mode != R::StorageMode::Gatherable) continue;
        const Core::ResourceType kind = R::StockpileOf(store.resource);
        if (kind == Core::ResourceType::COUNT || store.amount.value <= 0) continue;
        type = kind;
        amount = store.amount.ToInt();
        return true;
    }
    return false;
}

// The facing nearest the file's rotation: radians from the file's x axis
// toward its y, an eighth of a turn at a time, seen as a unit walking that way
// would be seen -- the way turned onto the grid as the file is (a step along
// the file's x is one along the grid's -y, along its y one along the grid's x).
Core::Direction FacingOf(Core::Fixed32 rotation) {
    const Core::Fixed32 eighth = Core::Fixed32(785398) / Core::Fixed32(1000000);
    int k = ((rotation + eighth / Core::Fixed32(2)) / eighth).ToInt();
    k = ((k % 8) + 8) % 8;
    static const int kX[8] = {1, 1, 0, -1, -1, -1, 0, 1};
    static const int kY[8] = {0, 1, 1, 1, 0, -1, -1, -1};
    return Core::FixedMath::WorldVectorToFacing(Core::Fixed32(kY[k]), Core::Fixed32(-kX[k]));
}

// A cliff piece as the editor left it: the cliff unit drawing piece `frame`,
// blocking the tiles under it.
entt::entity PlaceCliff(entt::registry& registry, Physics::Grid& grid, int unit_id,
                        C::Vector2D at, int frame) {
    auto& cfg = Core::ConfigManager::Get();
    const auto art = cfg.GetUnitGraphicById(unit_id, "idle_graphic0");
    if (!art.Valid()) return entt::null;
    Core::Cliffs::Block(grid, unit_id, at.x, at.y);
    const entt::entity cliff = registry.create();
    registry.emplace<C::Transform>(cliff, C::Transform{at, at});
    registry.emplace<C::Identity>(cliff, unit_id);
    registry.emplace<C::Owner>(cliff, Core::PLAYER_GAIA);
    C::Sprite sprite{};
    sprite.slp_id = art.slp_id;
    sprite.slp_frame = frame;
    sprite.is_file_sprite = false;
    sprite.shadow_resolved = true;
    sprite.width = 0;
    sprite.height = 0;
    registry.emplace<C::Sprite>(cliff, sprite);
    return cliff;
}

// A map object: a tree, a mine, a rock, a flag, an animal, a bridge.
entt::entity PlaceObject(entt::registry& registry, Physics::Grid& grid, int unit_id,
                         Core::PlayerId owner, C::Vector2D at, Core::Fixed32 rotation, int frame) {
    auto& cfg = Core::ConfigManager::Get();
    Core::ConfigManager::ObjectArt art = cfg.GetObjectArt(unit_id);
    if (!art.Valid()) {
        // Not one the random maps place: its own standing graphic.
        const auto graphic = cfg.GetUnitGraphicById(unit_id, "idle_graphic0");
        if (graphic.Valid()) {
            art.unit_id = unit_id;
            art.slp_id = graphic.slp_id;
            art.frame_count = std::max(1, graphic.frame_count);
            art.angle_count = std::max(1, graphic.angle_count);
            art.frame_ms = cfg.GetFrameMs(graphic.graphic_id, 0);
        }
    }
    const entt::entity entity = registry.create();
    registry.emplace<C::Transform>(entity, C::Transform{at, at});
    if (art.Valid()) {
        art.unit_id = unit_id;
        Core::MapGenerator::PlaceMapObject(registry, entity, art, RandomOf(registry));
        // The editor chose the shape a still object stands as, and the way an
        // animal faces.
        if (auto* sprite = registry.try_get<C::Sprite>(entity)) {
            if (!art.Animated()) {
                sprite->slp_frame = frame % std::max(1, art.angle_count);
            } else {
                sprite->facing = FacingOf(rotation);
            }
        }
    } else {
        registry.emplace<C::Identity>(entity, unit_id);
    }
    if (owner != Core::PLAYER_GAIA) registry.emplace_or_replace<C::Owner>(entity, owner);

    Core::ResourceType type = Core::ResourceType::Food;
    int amount = 0;
    if (Worth(unit_id, type, amount)) {
        registry.emplace_or_replace<C::Resource>(entity, C::Resource{.type = type, .current_amount = amount});
    }
    if (!registry.all_of<C::Health>(entity) && Blocks(unit_id)) {
        const int x = Physics::TileOf(at.x);
        const int y = Physics::TileOf(at.y);
        grid.SetWalkable(x, y, false);
        const R::UnitRules* rules = R::Unit(unit_id);
        if (rules != nullptr && rules->cls == kTreeClass) grid.GetTileMut(x, y).has_tree = true;
    }
    return entity;
}

// Everything researched that the player could research: the editor's
// Post-Imperial Age, played as the Imperial Age with every technology its
// buildings offer already done.
void ResearchEverything(entt::registry& registry, Core::PlayerId player) {
    const auto& techs = R::Get().techs;
    bool progressed = true;
    while (progressed) {
        progressed = false;
        for (int id = 0; id < static_cast<int>(techs.size()); ++id) {
            if (!techs[static_cast<size_t>(id)].valid || techs[static_cast<size_t>(id)].location < 0) continue;
            if (!Techs::Available(registry, player, id)) continue;
            Techs::Complete(registry, player, id);
            progressed = true;
        }
    }
}

Diplomacy::Stance StanceOf(uint8_t value) {
    if (value == 0) return Diplomacy::Stance::Ally;
    if (value == 1) return Diplomacy::Stance::Neutral;
    return Diplomacy::Stance::Enemy;
}

} // namespace

Core::TerrainType TerrainOf(int dat_terrain) {
    auto& cfg = Core::ConfigManager::Get();
    Core::TerrainType out = Core::TerrainType::Grass1;
    if (cfg.TerrainForRmsId(dat_terrain, out)) return out;
    for (const DrawsAs& d : kDrawsAs) {
        if (d.dat == dat_terrain && cfg.TerrainForRmsId(d.as, out)) return out;
    }
    return Core::TerrainType::Grass1;
}

bool Blocks(int unit_id) {
    Core::ResourceType type = Core::ResourceType::Food;
    int amount = 0;
    if (Worth(unit_id, type, amount)) return true;
    const auto* ui = Core::ConfigManager::Get().GetUnitInterface(unit_id);
    return ui != nullptr && ui->obstruction == 2;
}

entt::entity Place(entt::registry& registry, Physics::Grid& grid, int unit_id,
                   Core::PlayerId owner, Components::Vector2D at, Core::Fixed32 rotation,
                   int frame) {
    const R::UnitRules* rules = R::Unit(unit_id);
    if (rules == nullptr || !rules->valid) return entt::null;
    const int tile_x = Physics::TileOf(at.x);
    const int tile_y = Physics::TileOf(at.y);
    if (!grid.InBounds(tile_x, tile_y)) return entt::null;

    if (rules->cls == kCliffClass) return PlaceCliff(registry, grid, unit_id, at, frame);
    if (rules->type == R::UnitType::Building) {
        // A building with ground of its own stands finished on the tile its
        // centre is in. A bridge or a ruin has none, and is a map object.
        const auto* ui = Core::ConfigManager::Get().GetUnitInterface(unit_id);
        if (ui != nullptr && ui->footprint > 0) {
            return Buildings::Place(registry, grid, unit_id, owner, tile_x, tile_y, true);
        }
        return PlaceObject(registry, grid, unit_id, owner, at, rotation, frame);
    }
    if (rules->type == R::UnitType::Creatable && !(owner == Core::PLAYER_GAIA && Wild(rules->cls))) {
        const entt::entity unit = Units::Spawn(registry, unit_id, owner, at);
        if (unit != entt::null) {
            if (auto* sprite = registry.try_get<C::Sprite>(unit)) sprite->facing = FacingOf(rotation);
        }
        return unit;
    }
    return PlaceObject(registry, grid, unit_id, owner, at, rotation, frame);
}

void Remove(entt::registry& registry, Physics::Grid& grid, entt::entity entity) {
    if (!registry.valid(entity)) return;
    if (registry.all_of<C::Building>(entity)) {
        Buildings::Destroy(registry, grid, entity);
        return;
    }
    // Whoever it carries goes with it.
    if (const auto* aboard = registry.try_get<C::Garrison>(entity)) {
        const std::vector<entt::entity> inside = aboard->inside;
        for (entt::entity unit : inside) Remove(registry, grid, unit);
    }
    // And it leaves whatever holds it.
    if (const auto* in = registry.try_get<C::Garrisoned>(entity)) {
        if (registry.valid(in->building)) {
            if (auto* holder = registry.try_get<C::Garrison>(in->building)) {
                auto& list = holder->inside;
                list.erase(std::remove(list.begin(), list.end(), entity), list.end());
            }
        }
    }
    const auto* identity = registry.try_get<C::Identity>(entity);
    const auto* owner = registry.try_get<C::Owner>(entity);
    if (identity != nullptr && !registry.all_of<C::Dying>(entity)) {
        Players::Contribute(registry, owner != nullptr ? owner->player_id : Core::PLAYER_GAIA,
                            identity->unit_id, R::StorageMode::WhileAlive, -1);
    }
    // A map object frees the tile it blocked.
    if (identity != nullptr && !registry.all_of<C::Health>(entity) && Blocks(identity->unit_id)) {
        if (const auto* at = registry.try_get<C::Transform>(entity)) {
            const int x = Physics::TileOf(at->position.x);
            const int y = Physics::TileOf(at->position.y);
            if (grid.InBounds(x, y)) {
                grid.GetTileMut(x, y).has_tree = false;
                grid.SetWalkable(x, y, true);
            }
        }
    }
    registry.destroy(entity);
}

Components::Vector2D FromFile(Core::Fixed32 x, Core::Fixed32 y, int file_width) {
    const Core::Fixed32 half = Core::Fixed32(1) / Core::Fixed32(2);
    return {y - half, Core::Fixed32(file_width) - x - half};
}

std::pair<int, int> TileFromFile(int x, int y, int file_width) {
    return {y, file_width - 1 - x};
}

Scenario::File Turned(const Scenario::File& file) {
    Scenario::File out = file;
    const int w = file.width;
    const int h = file.height;
    out.width = h;
    out.height = w;
    const size_t tiles = static_cast<size_t>(w) * static_cast<size_t>(h);
    if (file.terrain.size() == tiles && file.elevation.size() == tiles) {
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                const auto [gx, gy] = TileFromFile(x, y, w);
                const size_t from = static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x);
                const size_t to = static_cast<size_t>(gy) * static_cast<size_t>(h) + static_cast<size_t>(gx);
                out.terrain[to] = file.terrain[from];
                out.elevation[to] = file.elevation[from];
            }
        }
    }
    // A place in the span of its tile, as the file keeps it.
    auto span = [&](Core::Fixed32& x, Core::Fixed32& y) {
        const Core::Fixed32 fx = x;
        x = y;
        y = Core::Fixed32(w) - fx;
    };
    auto tile = [&](int32_t& x, int32_t& y) {
        if (x < 0 || y < 0) return;
        const auto [gx, gy] = TileFromFile(x, y, w);
        x = gx;
        y = gy;
    };
    for (Scenario::Player& player : out.players) {
        for (Scenario::Unit& unit : player.units) span(unit.x, unit.y);
        span(player.camera_x, player.camera_y);
    }
    tile(out.camera_x, out.camera_y);
    auto area = [&](Scenario::Area& a) {
        if (!a.Set()) return;
        tile(a.x0, a.y0);
        tile(a.x1, a.y1);
    };
    for (Scenario::Trigger& trigger : out.triggers) {
        for (Scenario::Condition& c : trigger.conditions) area(c.area);
        for (Scenario::Effect& e : trigger.effects) {
            tile(e.location.x, e.location.y);
            area(e.area);
        }
    }
    return out;
}

} // namespace ECS::ScenarioWorld

namespace ECS {

void Simulation::InitFromFile(const Core::GameSetup& game, const Scenario::File& file) {
    Core::GameSetup with = game;
    if (with.scenario.empty()) with.scenario = "<editor>";
    from_file = &file;
    Init(with);
    from_file = nullptr;
}

void Simulation::InitScenario() {
    namespace R = Core::Rules;
    Scenario::File file;
    std::string error;
    if (from_file != nullptr) {
        file = *from_file;
    } else if (!Scenario::Load(setup.scenario, setup.scenario_entry, file, &error)) {
        // A world anyway -- the lobby's size, bare -- so a save of a scenario
        // whose file has gone still fails cleanly in the save module.
        std::cerr << "[Simulation] Scenario \"" << setup.scenario << "\" unreadable: " << error
                  << std::endl;
        file = Scenario::File{};
        file.width = std::max(1, setup.map_size);
        file.height = file.width;
        file.terrain.assign(static_cast<size_t>(file.width) * static_cast<size_t>(file.height), 0);
        file.elevation.assign(file.terrain.size(), 0);
    }
    // Everything below, the triggers and a save of this game see the file
    // turned onto the grid's frame.
    file = ScenarioWorld::Turned(file);

    // What the players start with: the file's stockpiles, the editor's
    // Post-Imperial Age, the technologies it forbids. Their ages were reached
    // with the rest of the players' set-up, from the seats.
    for (int p = 1; p <= Scenario::kPlayers; ++p) {
        if (!setup.players[p].active) continue;
        const auto seat = static_cast<Core::PlayerId>(p);
        const Scenario::Player& from = file.players[p];
        if (Players::Player* player = Players::Find(registry, seat)) {
            Players::SetResource(*player, R::Res::Food, Core::Fixed32(from.food));
            Players::SetResource(*player, R::Res::Wood, Core::Fixed32(from.wood));
            Players::SetResource(*player, R::Res::Gold, Core::Fixed32(from.gold));
            Players::SetResource(*player, R::Res::Stone, Core::Fixed32(from.stone));
        }
        if (from.starting_age >= 4) ScenarioWorld::ResearchEverything(registry, seat);
        if (Players::Player* player = Players::Find(registry, seat)) {
            for (int32_t tech : from.disabled_techs) {
                if (tech < 0 || tech >= static_cast<int32_t>(player->techs.size())) continue;
                auto& status = player->techs[static_cast<size_t>(tech)];
                if (status.state != Players::TechState::Done) status.state = Players::TechState::Disabled;
            }
        }
    }

    // How each regards each other, and who wins with allies: the file's, set
    // straight into the table -- these are where the game starts, not
    // changes anyone made, so there is no news of them.
    auto& table = registry.ctx().get<Diplomacy::Table>();
    for (int p = 1; p <= Scenario::kPlayers; ++p) {
        if (!setup.players[p].active) continue;
        for (int q = 1; q <= Scenario::kPlayers; ++q) {
            if (q == p || !setup.players[q].active) continue;
            table.stance[p][q] = ScenarioWorld::StanceOf(file.players[p].stance[q]);
        }
        table.allied_victory[p] = file.players[p].allied_victory;
    }

    // The ground.
    grid.Resize(file.width, file.height);
    Core::MapGenerator::SetUpLayers(grid);
    for (int y = 0; y < file.height; ++y) {
        for (int x = 0; x < file.width; ++x) {
            const size_t i = static_cast<size_t>(y) * static_cast<size_t>(file.width) + static_cast<size_t>(x);
            Core::MapGenerator::Paint(grid, x, y, ScenarioWorld::TerrainOf(file.terrain[i]));
            grid.GetTileMut(x, y).elevation = file.elevation[i];
        }
    }

    // Everything on it, Gaia's first, then each player's, in the file's
    // order. A player the scenario leaves out has none.
    const Core::Fixed32 half = Core::Fixed32(1) / Core::Fixed32(2);
    std::vector<Triggers::Placed> placed;
    for (int p = 0; p < Core::kMaxPlayers; ++p) {
        if (p != Core::PLAYER_GAIA && !setup.players[p].active) continue;
        for (const Scenario::Unit& unit : file.players[p].units) {
            const entt::entity e = ScenarioWorld::Place(registry, grid, unit.unit_id,
                                                        static_cast<Core::PlayerId>(p),
                                                        {unit.x - half, unit.y - half},
                                                        unit.rotation, unit.frame);
            if (e != entt::null) placed.push_back({unit.id, e});
        }
    }
    std::stable_sort(placed.begin(), placed.end(),
                     [](const Triggers::Placed& a, const Triggers::Placed& b) { return a.id < b.id; });
    auto lookup = [&](int32_t id) {
        auto it = std::lower_bound(placed.begin(), placed.end(), id,
                                   [](const Triggers::Placed& a, int32_t v) { return a.id < v; });
        return it != placed.end() && it->id == id ? it->entity : entt::null;
    };
    // Those the editor put inside something.
    for (int p = 0; p < Core::kMaxPlayers; ++p) {
        if (p != Core::PLAYER_GAIA && !setup.players[p].active) continue;
        for (const Scenario::Unit& unit : file.players[p].units) {
            if (unit.garrisoned_in < 0) continue;
            const entt::entity holder = lookup(unit.garrisoned_in);
            const entt::entity inside = lookup(unit.id);
            if (holder != entt::null && inside != entt::null) Garrisons::PutInside(registry, inside, holder);
        }
    }
    grid.WalkabilityChanged();

    Vision::Setup(registry, grid.GetWidth(), grid.GetHeight(), setup.reveal);
    // Where each player's view opens, in seat order as a random map's starts:
    // player 1's is the view the editor saved, a tile; the others' cameras are
    // the Players screen's, a tile's middle.
    player_starts.clear();
    for (int p = 1; p <= Scenario::kPlayers; ++p) {
        if (!setup.players[p].active) continue;
        int x = Physics::TileOf(file.players[p].camera_x - half);
        int y = Physics::TileOf(file.players[p].camera_y - half);
        if (p == 1 && file.camera_x >= 0 && file.camera_y >= 0) {
            x = file.camera_x;
            y = file.camera_y;
        }
        player_starts.emplace_back(std::clamp(x, 0, grid.GetWidth() - 1), std::clamp(y, 0, grid.GetHeight() - 1));
    }

    Triggers::Setup(registry, file, std::move(placed));
    // The file itself, for the interface -- its texts, its objectives -- and
    // for the triggers, whose definitions it holds.
    registry.ctx().insert_or_assign(std::move(file));
    CalculateVision();
    std::cout << "[Simulation] Scenario world initialized." << std::endl;
}

} // namespace ECS
