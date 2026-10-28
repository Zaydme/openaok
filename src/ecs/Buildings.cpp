#include "ecs/Buildings.h"

#include "ecs/Events.h"
#include "ecs/Garrisons.h"
#include "ecs/Relics.h"
#include "ecs/Walls.h"

#include <algorithm>
#include <vector>

#include "core/ConfigManager.h"
#include "core/FixedMath.h"
#include "core/Media.h"
#include "core/Rules.h"
#include "ecs/BuildingGeometry.h"
#include "ecs/Diplomacy.h"
#include "ecs/Fog.h"
#include "ecs/Navigation.h"
#include "ecs/Players.h"
#include "ecs/Score.h"
#include "ecs/Production.h"
#include "ecs/Techs.h"
#include "ecs/Vision.h"
#include "ecs/components/Animation.h"
#include "ecs/components/Building.h"
#include "ecs/components/Combat.h"
#include "ecs/components/Health.h"
#include "ecs/components/Identity.h"
#include "ecs/components/Mortal.h"
#include "ecs/components/Owner.h"
#include "ecs/components/Path.h"
#include "ecs/components/PartOf.h"
#include "ecs/components/Resource.h"
#include "ecs/components/Selectable.h"
#include "ecs/components/Sight.h"
#include "ecs/components/Sprite.h"
#include "ecs/components/Transform.h"
#include "ecs/components/Worker.h"
#include "pathfinding/ApproachPlanner.h"

namespace ECS::Buildings {

namespace {

namespace R = Core::Rules;

// Half a unit's width, and the gap a new unit keeps from the wall it appears
// against.
const Core::Fixed32 kDoorStandoff = Core::Fixed32(45) / Core::Fixed32(100);

// How far apart units appearing at the same building are placed, so a queue
// of trained units lines up beside the wall.
const Core::Fixed32 kDoorClearance = Core::Fixed32(55) / Core::Fixed32(100);

// How far from a building other units still count as standing at its door.
const Core::Fixed32 kDoorwayReach = Core::Fixed32(3);

// The dat's class for a farm: its food is the owner's "Farm Food Amount"
// resource, which techs raise.
constexpr int kFarmClass = 49;

Core::PlayerId OwnerOf(const entt::registry& registry, entt::entity entity) {
    const auto* owner = registry.try_get<Components::Owner>(entity);
    return owner != nullptr ? owner->player_id : Core::PLAYER_GAIA;
}

int CivOf(const entt::registry& registry, Core::PlayerId player) {
    const Players::Player* p = Players::Find(registry, player);
    return p != nullptr ? p->civ : 1;
}

// Building pieces never resolve a companion shadow on their own: a building's
// shadow spans the whole footprint, so it is one of the composed pieces.
Components::Sprite PieceSprite(int slp_id, int bias, bool flip, bool shadow,
                               const Core::ConfigManager::Annex* src = nullptr) {
    Components::Sprite sprite{};
    sprite.slp_id = slp_id;
    sprite.slp_frame = 0;
    sprite.width = 0;
    sprite.height = 0;
    sprite.sort_bias = bias;
    sprite.flip_h = flip;
    sprite.is_shadow = shadow;
    sprite.shadow_resolved = true;
    if (src && src->src_w > 0) {
        sprite.sub_x = src->src_x;
        sprite.sub_y = src->src_y;
        sprite.sub_w = src->src_w;
        sprite.sub_h = src->src_h;
    }
    return sprite;
}

void DestroyPieces(entt::registry& registry, entt::entity building) {
    std::vector<entt::entity> pieces;
    auto parts = registry.view<Components::PartOf>();
    for (auto part : parts) {
        if (parts.get<Components::PartOf>(part).building == building) {
            pieces.push_back(part);
        }
    }
    for (auto piece : pieces) registry.destroy(piece);
}

// Places a finished building's art. Large buildings are composed of a main
// unit plus annex units, each with its own graphic at an offset from the
// origin -- a Town Center is four such pieces. Each piece becomes its own
// entity with a transform and a sprite; `sort_bias` orders the pieces of one
// building, so units can draw between its back and front.
//
// Which art: the owner's slot for this building, which an age advance
// upgrades -- a Feudal Age Town Center wears unit 71's composition -- and
// within that, the owner's age and architecture.
void SpawnArt(entt::registry& registry, entt::entity building) {
    // Walls and gates are composed by their own module: a wall's piece is
    // its neighbours' choice, and a gate is an arch between two posts.
    if (Walls::SpawnArt(registry, building)) return;
    auto& cfg = Core::ConfigManager::Get();
    const int unit_id = registry.get<Components::Identity>(building).unit_id;
    const auto& origin = registry.get<Components::Transform>(building).position;
    const int tile_x = Physics::TileOf(origin.x);
    const int tile_y = Physics::TileOf(origin.y);
    const Core::PlayerId owner = OwnerOf(registry, building);
    // Its art is the table it plays by -- a converted building its maker's
    // age and architecture -- and its colour its owner's.
    const Core::PlayerId maker = Players::RulesPlayer(registry, building);
    const int art_unit = Players::ArtOf(registry, maker, unit_id);

    const std::vector<Core::ConfigManager::Annex>* pieces = cfg.GetAnnexes(art_unit);
    if (pieces == nullptr) pieces = cfg.GetAnnexes(unit_id);

    // Without a composition the unit is a single graphic on its own tile,
    // resolved by name: the dat's standing graphic for the Mill is a fragment,
    // and for the two camps art that is not on the disc.
    if (pieces == nullptr) {
        const int civ = CivOf(registry, maker);
        const int age = Techs::Age(registry, maker) + 1;
        auto art = cfg.GetBuildingArt(art_unit, age, civ);
        if (!art.Valid()) art = cfg.GetBuildingArt(unit_id, age, civ);
        // A building standing before its own age -- a Regicide game's Castle
        // in the Dark Age -- wears its own first age's art.
        for (int later = age + 1; later <= 4 && !art.Valid(); ++later) {
            art = cfg.GetBuildingArt(art_unit, later, civ);
        }
        const auto body = cfg.GetGraphic(art.body);
        if (!body.Valid()) return;
        registry.emplace_or_replace<Components::Sprite>(building,
            PieceSprite(body.slp_id, 0, false, false));
        const auto shadow = cfg.GetGraphic(art.shadow);
        if (shadow.Valid()) {
            auto shade = registry.create();
            registry.emplace<Components::Transform>(shade, origin, origin);
            registry.emplace<Components::Sprite>(shade,
                PieceSprite(shadow.slp_id, -1, false, true));
            registry.emplace<Components::PartOf>(shade, building);
        }
        return;
    }

    // With one, the composition is authoritative and lists every drawable,
    // including the unit's own graphic -- which does not necessarily sit on the
    // origin tile. The building entity takes the first piece that does, so it
    // still has a sprite of its own; the rest become their own entities.
    //
    // The footprint's shadow follows the pieces: it is whichever of them has a
    // companion shadow graphic, as resolved for this owner and age.
    int shadow_slp = -1;
    for (const auto& piece : *pieces) {
        if (piece.shadow) continue;
        const int resolved = PieceGraphic(registry, maker, piece);
        if (resolved >= 0 && cfg.GetShadowSlp(resolved) >= 0) {
            shadow_slp = cfg.GetShadowSlp(resolved);
            break;
        }
    }

    bool owner_has_sprite = false;
    int bias = 0;
    for (const auto& piece : *pieces) {
        const int slp_id = piece.shadow && shadow_slp >= 0
                         ? shadow_slp : PieceGraphic(registry, maker, piece);
        if (slp_id < 0) continue;

        // Fixed-point offsets, converted at load, so placement inside a tick
        // is deterministic.
        const Core::Fixed32 px = Core::Fixed32(tile_x) + piece.fx;
        const Core::Fixed32 py = Core::Fixed32(tile_y) + piece.fy;
        auto sprite = PieceSprite(slp_id, bias++, piece.flip, piece.shadow, &piece);

        if (!owner_has_sprite && !piece.shadow &&
            piece.fx.value == 0 && piece.fy.value == 0) {
            owner_has_sprite = true;
            registry.emplace_or_replace<Components::Sprite>(building, std::move(sprite));
            continue;
        }

        auto part = registry.create();
        registry.emplace<Components::Transform>(part,
            Components::Vector2D{px, py}, Components::Vector2D{px, py});
        registry.emplace<Components::Sprite>(part, std::move(sprite));
        // Every piece is drawn in the owner's colour, and the renderer reads
        // the player from the entity it is drawing.
        registry.emplace<Components::Owner>(part, owner);
        registry.emplace<Components::PartOf>(part, building);
    }
}

// The House: its site stays at its graphic's first frame, its one stage, for
// the whole build, where other sites step through their graphic's angles.
constexpr int kHouse = 70;

// The construction site's art: the dat's construction graphic, whose frames
// the animation system steps with the site's progress. A building that goes
// up as another unit -- a gate as its own site unit, the dat's head unit, a
// gate-shaped scaffold -- wears that unit's. The graphic's deltas, a stone
// wall's or a gate's shadow, become pieces drawn at the site's frame.
void SpawnSiteArt(entt::registry& registry, entt::entity building,
                  const R::UnitRules& rules) {
    auto& cfg = Core::ConfigManager::Get();
    int graphic = rules.construction_graphic;
    if (const R::UnitRules* head = rules.head_unit >= 0 ? R::Unit(rules.head_unit) : nullptr;
        head != nullptr && cfg.GetGraphic(head->construction_graphic).Valid()) {
        graphic = head->construction_graphic;
    }
    const auto construction = cfg.GetGraphic(graphic);
    Components::Sprite site{};
    site.width = 0;
    site.height = 0;
    if (construction.Valid()) {
        site.slp_id = construction.slp_id;
        site.construction_frames =
            registry.get<Components::Identity>(building).unit_id == kHouse
                ? std::max(1, construction.frame_count)
                : std::max(1, construction.frame_count * std::max(1, construction.angle_count));
        site.shadow_resolved = true;   // its shadow is a delta, if anything
        const auto& at = registry.get<Components::Transform>(building).position;
        for (const auto& delta : Core::Media::Deltas(graphic)) {
            const auto layer = cfg.GetGraphic(delta.graphic);
            if (!layer.Valid()) continue;
            auto piece = registry.create();
            registry.emplace<Components::Transform>(piece, at, at);
            auto sprite = PieceSprite(layer.slp_id, -1, false, true);
            sprite.site_layer = true;
            registry.emplace<Components::Sprite>(piece, std::move(sprite));
            registry.emplace<Components::Owner>(piece, OwnerOf(registry, building));
            registry.emplace<Components::PartOf>(piece, building);
        }
    } else {
        // No construction art on the disc (the Farm's field, SLP 419): the
        // site draws nothing, and a Farm going up shows only its farmland
        // stages.
        site.color = 0;
    }
    registry.emplace_or_replace<Components::Sprite>(building, std::move(site));
    Walls::SiteArt(registry, building);
}

// Lays `terrain` on every tile of a building's footprint; who does not see a
// tile keeps it as it was (ecs/Fog).
void LayGround(entt::registry& registry, Physics::Grid& grid, int unit_id, int tile_x, int tile_y,
               Core::TerrainType terrain) {
    ForEachFootprintTile(unit_id, tile_x, tile_y, [&](int x, int y) {
        if (!grid.InBounds(x, y)) return;
        const Core::TerrainType was = grid.GetTile(x, y).terrain;
        if (was == terrain) return;
        Fog::GroundChanging(registry, x, y, was);
        grid.SetTerrain(x, y, terrain);
    });
}

// The ground a farm site shows for how far along it is: three construction
// stages of farmland before the crop comes up.
Core::TerrainType FarmSiteGround(Core::Fixed32 progress) {
    if (progress < Core::Fixed32(1) / Core::Fixed32(3)) return Core::TerrainType::FarmBuild1;
    if (progress < Core::Fixed32(2) / Core::Fixed32(3)) return Core::TerrainType::FarmBuild2;
    return Core::TerrainType::FarmBuild3;
}

// The dat's foundation terrain for a building -- dirt under a Town Center,
// planted farmland under a farm -- as the engine's terrain, or false.
bool FoundationGround(const R::UnitRules& rules, Core::TerrainType& out) {
    return rules.foundation_terrain >= 0 &&
           Core::ConfigManager::Get().TerrainForRmsId(rules.foundation_terrain, out);
}

// A farm that lays a crop: the dat gives it a foundation terrain, planted
// farmland, and its ground goes through the construction stages to it and
// lies harvested out after. The fish trap is a farm too (the farm class) and
// has none: it stands in the water as it is.
bool LaysCrop(const R::UnitRules& rules) {
    Core::TerrainType ground;
    return rules.cls == kFarmClass && FoundationGround(rules, ground);
}

// A building that holds a gatherable resource is worked from on top rather
// than walked around: a farm. Returns the stock it starts with, or false.
//
// A farm's food is the civ's own Farm Food Amount, and a fish trap's -- the
// farm that lays no crop -- its Fish Trap Food Amount (resources 36 and 88),
// not the amount either unit's storage states.
bool Farmland(const entt::registry& registry, Core::PlayerId owner,
              const R::UnitRules& rules, Components::Resource& out) {
    for (int i = 0; i < rules.storage_count; ++i) {
        const R::Storage& store = rules.storage[i];
        if (store.mode != R::StorageMode::Gatherable) continue;
        const Core::ResourceType kind = R::StockpileOf(store.resource);
        if (kind == Core::ResourceType::COUNT) continue;
        int amount = store.amount.ToInt();
        if (rules.cls == kFarmClass) {
            if (const Players::Player* p = Players::Find(registry, owner)) {
                amount = Players::Resource(*p, LaysCrop(rules) ? R::Res::FarmFoodAmount
                                                               : R::Res::FishTrapFoodAmount)
                             .ToInt();
            }
        }
        if (amount <= 0) return false;
        out = Components::Resource{kind, amount};
        return true;
    }
    return false;
}

// A finished building whose rules give it a missile shoots: a tower, a
// castle. One that only shows an attack in its panel does not.
void Arm(entt::registry& registry, entt::entity building) {
    const R::UnitRules* rules = Players::UnitOf(
        registry, OwnerOf(registry, building),
        registry.get<Components::Identity>(building).unit_id);
    // Armed with a missile of its own, or one its garrison looses: a Town
    // Center shoots only with units inside.
    if (rules != nullptr && rules->attack_count > 0 &&
        (rules->projectile >= 0 || rules->secondary_projectile >= 0)) {
        registry.emplace_or_replace<Components::Combat>(building);
    }
}

} // namespace

// Units standing where a building has just been placed step off its ground,
// to the nearest spot beside it, so none is walled inside.
void StepAside(entt::registry& registry, const Physics::Grid& grid, const Bounds& ground) {
    std::vector<entt::entity> inside;
    std::vector<Components::Vector2D> standing;
    auto units = registry.view<Components::Transform, Components::Animation>();
    for (auto unit : units) {
        if (registry.all_of<Components::Building>(unit)) continue;
        const auto& at = units.get<Components::Transform>(unit).position;
        if (ground.DistanceFrom(at).value == 0) {
            inside.push_back(unit);
        } else if (ground.DistanceFrom(at) <= kDoorwayReach) {
            standing.push_back(at);
        }
    }
    for (auto unit : inside) {
        auto& transform = registry.get<Components::Transform>(unit);
        Pathfinding::ApproachRequest ask;
        ask.target = ground;
        ask.from = transform.position;
        ask.standoff = kDoorStandoff;
        ask.clearance = kDoorClearance;
        ask.taken = standing.empty() ? nullptr : standing.data();
        ask.taken_count = standing.size();
        ask.layer = Navigation::LayerOf(registry, grid, unit);
        const auto spot = Pathfinding::FindStandingSpot(grid, ask);
        if (!spot.found) continue;
        transform.position = spot.spot;
        standing.push_back(spot.spot);
        // Whatever route it had started from inside the walls.
        registry.remove<Components::Path>(unit);
    }
}

int PieceGraphic(const entt::registry& registry, Core::PlayerId owner,
                 const Core::ConfigManager::Annex& piece) {
    auto& cfg = Core::ConfigManager::Get();
    if (piece.counterpart_of >= 0) {
        const auto other = cfg.GetCounterpartGraphic(
            Players::ArtOf(registry, owner, piece.counterpart_of));
        if (other.Valid()) return other.slp_id;
        return piece.slp_id;
    }
    if (piece.unit_id >= 0) {
        const auto gfx = cfg.GetUnitGraphicById(
            Players::ArtOf(registry, owner, piece.unit_id), "idle_graphic0");
        if (gfx.Valid()) return gfx.slp_id;
    }
    return piece.slp_id;
}

int PlacedUnit(int unit_id) {
    const R::UnitRules* rules = R::Unit(unit_id);
    if (rules != nullptr && rules->stack_unit >= 0 && R::Unit(rules->stack_unit)) {
        return rules->stack_unit;
    }
    return unit_id;
}

namespace {

bool Overlap(const Bounds& a, const Bounds& b) {
    return a.min_x < b.max_x && b.min_x < a.max_x && a.min_y < b.max_y && b.min_y < a.max_y;
}

bool SameSide(const entt::registry& registry, Core::PlayerId a, Core::PlayerId b) {
    return a == b || Diplomacy::Allied(registry, a, b);
}

// A building takes its ground: its footprint blocked -- a farm's stays open --
// and whoever stands on it stepping off.
void Hold(entt::registry& registry, Physics::Grid& grid, entt::entity building) {
    auto& state = registry.get<Components::Building>(building);
    const int unit_id = registry.get<Components::Identity>(building).unit_id;
    const auto origin = registry.get<Components::Transform>(building).position;
    if (!registry.all_of<Components::Resource>(building)) {
        ForEachFootprintTile(unit_id, Physics::TileOf(origin.x), Physics::TileOf(origin.y), [&](int x, int y) {
            grid.SetWalkable(x, y, false);
        });
        StepAside(registry, grid, FootprintBounds(origin, unit_id));
    }
    state.begun = true;
}

// Whether a unit stands on `ground`, and whose: the first of a player not on
// `player`'s side, or else whether any stands there at all.
bool UnitOn(const entt::registry& registry, const Bounds& ground, Core::PlayerId player, bool& foe) {
    foe = false;
    bool any = false;
    auto units = registry.view<const Components::Transform, const Components::Animation,
                               const Components::Owner>();
    for (auto unit : units) {
        if (registry.any_of<Components::Building, Components::Dying, Components::Corpse>(unit)) continue;
        if (ground.DistanceFrom(units.get<const Components::Transform>(unit).position).value != 0) continue;
        const Core::PlayerId owner = units.get<const Components::Owner>(unit).player_id;
        if (owner != Core::PLAYER_GAIA && !SameSide(registry, player, owner)) {
            foe = true;
            return true;
        }
        any = true;
    }
    return any;
}

} // namespace

Site Check(const entt::registry& registry, const Physics::Grid& grid, Core::PlayerId player,
           int unit_id, int tile_x, int tile_y, Ask ask, entt::entity self) {
    const int placed = PlacedUnit(unit_id);
    if (ask == Ask::Placing) {
        if (!CanPlace(grid, unit_id, tile_x, tile_y)) return Site::Ground;
    } else {
        bool open = true;
        ForEachFootprintTile(placed, tile_x, tile_y, [&](int x, int y) {
            open = open && grid.InBounds(x, y) && !grid.IsBlocked(x, y);
        });
        if (!open) return Site::Ground;
    }
    if (ask == Ask::Placing) {
        bool seen = false;
        ForEachFootprintTile(placed, tile_x, tile_y, [&](int x, int y) {
            seen = seen || Vision::Explored(registry, player, x, y);
        });
        if (!seen) return Site::Unexplored;
    }
    const Bounds ground = FootprintBounds({Core::Fixed32(tile_x), Core::Fixed32(tile_y)}, placed);
    auto sites = registry.view<const Components::Building, const Components::Identity,
                               const Components::Transform, const Components::Owner>();
    for (auto site : sites) {
        if (site == self || sites.get<const Components::Building>(site).begun) continue;
        if (!SameSide(registry, player, sites.get<const Components::Owner>(site).player_id)) continue;
        const Bounds theirs = FootprintBounds(sites.get<const Components::Transform>(site).position,
                                              sites.get<const Components::Identity>(site).unit_id);
        if (Overlap(ground, theirs)) return Site::Foundation;
    }
    // A farm is laid under whoever walks there.
    const R::UnitRules* rules = R::Unit(placed);
    if (rules != nullptr && LaysCrop(*rules)) return Site::Clear;
    bool foe = false;
    UnitOn(registry, ground, player, foe);
    return foe ? Site::Unit : Site::Clear;
}

bool Begun(const entt::registry& registry, entt::entity building) {
    const auto* state = registry.try_get<Components::Building>(building);
    return state == nullptr || state->begun;
}

Start Begin(entt::registry& registry, Physics::Grid& grid, entt::entity site) {
    if (Begun(registry, site)) return Start::Begun;
    const int unit_id = registry.get<Components::Identity>(site).unit_id;
    const auto origin = registry.get<Components::Transform>(site).position;
    const Core::PlayerId owner = OwnerOf(registry, site);
    if (Check(registry, grid, owner, unit_id, Physics::TileOf(origin.x), Physics::TileOf(origin.y),
              Ask::Beginning, site) != Site::Clear) {
        return Start::Taken;
    }
    const Bounds ground = FootprintBounds(origin, unit_id);
    if (!registry.all_of<Components::Resource>(site)) {
        bool foe = false;
        StepAside(registry, grid, ground);
        if (UnitOn(registry, ground, owner, foe)) return Start::Crowded;
    }
    // Whatever else was laid out here and not yet begun goes, refunded.
    std::vector<entt::entity> overlapping;
    auto sites = registry.view<const Components::Building, const Components::Identity,
                               const Components::Transform>();
    for (auto other : sites) {
        if (other == site || sites.get<const Components::Building>(other).begun) continue;
        if (Overlap(ground, FootprintBounds(sites.get<const Components::Transform>(other).position,
                                            sites.get<const Components::Identity>(other).unit_id))) {
            overlapping.push_back(other);
        }
    }
    for (entt::entity other : overlapping) {
        Refund(registry, other);
        Destroy(registry, grid, other, false);
    }
    Hold(registry, grid, site);
    return Start::Begun;
}

void Refund(entt::registry& registry, entt::entity building) {
    const auto* state = registry.try_get<Components::Building>(building);
    const auto* identity = registry.try_get<Components::Identity>(building);
    const auto* health = registry.try_get<Components::Health>(building);
    if (state == nullptr || identity == nullptr) return;
    Core::Fixed32 share{};
    if (state->state != Components::BuildingState::Complete) {
        share = !state->begun ? Core::Fixed32(1)
                              : Core::FixedMath::Min(Core::Fixed32(1), Core::Fixed32(1) - state->progress);
    }
    if (health != nullptr && health->current_hp < 1) share = Core::Fixed32(0);
    if (share.value <= 0) return;
    const Core::PlayerId owner = OwnerOf(registry, building);
    const R::UnitRules* rules = Players::UnitOf(registry, owner, identity->unit_id);
    Players::Player* player = Players::Find(registry, owner);
    if (rules == nullptr || player == nullptr) return;
    for (int i = 0; i < rules->cost_count; ++i) {
        const R::Cost& cost = rules->costs[i];
        if (cost.resource < 0 || !cost.paid || cost.amount <= 0) continue;
        Players::AddResource(*player, cost.resource, Core::Fixed32(cost.amount) * share);
    }
}

// Whether a tile is flat: none of the eight around it -- a tile off the map
// counting as level -- one level higher.
bool Flat(const Physics::Grid& grid, int x, int y) {
    const int level = grid.GetTile(x, y).elevation;
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            if ((dx != 0 || dy != 0) && grid.InBounds(x + dx, y + dy) &&
                grid.GetTile(x + dx, y + dy).elevation == level + 1) {
                return false;
            }
        }
    }
    return true;
}

bool CanPlace(const Physics::Grid& grid, int unit_id, int tile_x, int tile_y) {
    const int placed = PlacedUnit(unit_id);
    bool clear = true;
    // Every tile free of anything standing there, and of a ground the
    // building's own terrain restriction lets it stand on: land for most,
    // water and shore for a dock.
    const R::UnitRules* own = R::Unit(placed);
    const int restriction = own != nullptr ? own->terrain_restriction : -1;
    auto& cfg = Core::ConfigManager::Get();
    ForEachFootprintTile(placed, tile_x, tile_y, [&](int x, int y) {
        if (!grid.InBounds(x, y) || grid.IsBlocked(x, y) ||
            !R::Buildable(restriction, cfg.GetTerrainArt(grid.GetTile(x, y).terrain).dat_id)) {
            clear = false;
            return;
        }
    });
    if (!clear) return false;
    // The dat's placement terrains, past the restriction. With placement
    // terrains -- a dock's water and shallows -- its middle tile must be one
    // of them, and its side terrain, the beach, must lie straight beside that
    // tile: the dock stands half on the shore. With a side terrain alone --
    // the shore fish's beach -- it must lie straight beside the footprint.
    // Corners do not count.
    if (own != nullptr) {
        auto is_one_of = [&](const int* terrains, int x, int y) {
            if (!grid.InBounds(x, y)) return false;
            const int dat = cfg.GetTerrainArt(grid.GetTile(x, y).terrain).dat_id;
            return dat >= 0 && (dat == terrains[0] || dat == terrains[1]);
        };
        const bool under_rule = own->placement_terrain[0] >= 0 || own->placement_terrain[1] >= 0;
        const bool side_rule =
            own->placement_side_terrain[0] >= 0 || own->placement_side_terrain[1] >= 0;
        if (under_rule && !is_one_of(own->placement_terrain, tile_x, tile_y)) return false;
        if (side_rule) {
            const Footprint f = FootprintOf(placed);
            const int x0 = under_rule ? tile_x : tile_x + f.x.low;
            const int x1 = under_rule ? tile_x : tile_x + f.x.high;
            const int y0 = under_rule ? tile_y : tile_y + f.y.low;
            const int y1 = under_rule ? tile_y : tile_y + f.y.high;
            bool beside = false;
            for (int x = x0; x <= x1 && !beside; ++x) {
                beside = is_one_of(own->placement_side_terrain, x, y0 - 1) ||
                         is_one_of(own->placement_side_terrain, x, y1 + 1);
            }
            for (int y = y0; y <= y1 && !beside; ++y) {
                beside = is_one_of(own->placement_side_terrain, x0 - 1, y) ||
                         is_one_of(own->placement_side_terrain, x1 + 1, y);
            }
            if (!beside) return false;
        }
    }
    // The dat's hill rule. A Town Center (2) wants every tile of its ground
    // flat. Most buildings (3) want only the two edges at the front -- the
    // corner nearest the bottom of the screen and the rows running from it --
    // within a level of that corner; the rest of the ground may lie as it
    // will. Anything else (0) stands wherever.
    const R::UnitRules* rules = R::Unit(placed);
    const int hill_mode = rules != nullptr ? rules->hill_mode : 0;
    constexpr int kFlatOnly = 2;
    constexpr int kFrontWithinALevel = 3;
    const Footprint f = FootprintOf(placed);
    const int front_x = tile_x + f.x.high;
    const int front_y = tile_y + f.y.high;
    bool fits = true;
    if (hill_mode == kFlatOnly) {
        ForEachFootprintTile(placed, tile_x, tile_y, [&](int x, int y) {
            fits = fits && Flat(grid, x, y);
        });
    } else if (hill_mode == kFrontWithinALevel) {
        const int corner = grid.GetTile(front_x, front_y).elevation;
        ForEachFootprintTile(placed, tile_x, tile_y, [&](int x, int y) {
            if (x != front_x && y != front_y) return;
            fits = fits && std::abs(grid.GetTile(x, y).elevation - corner) <= 1;
        });
    }
    return fits;
}

entt::entity Place(entt::registry& registry, Physics::Grid& grid, int unit_id,
                   Core::PlayerId player, int tile_x, int tile_y, bool complete) {
    const int placed = PlacedUnit(unit_id);
    const R::UnitRules* rules = Players::UnitOf(registry, player, placed);
    if (rules == nullptr) return entt::null;

    const entt::entity building = registry.create();
    const Components::Vector2D origin{Core::Fixed32(tile_x), Core::Fixed32(tile_y)};
    registry.emplace<Components::Transform>(building, origin, origin);
    registry.emplace<Components::Building>(building, Components::Building{
        complete ? Components::BuildingState::Complete
                 : Components::BuildingState::UnderConstruction,
        complete ? Core::Fixed32(1) : Core::Fixed32(0), false});
    registry.emplace<Components::Identity>(building, placed);
    registry.emplace<Components::Owner>(building, player);
    const int hp = std::max(1, rules->hp);
    registry.emplace<Components::Health>(building, complete ? hp : 1, hp);
    // Buildings are selectable, which is what lets one be picked and deleted.
    registry.emplace<Components::Selectable>(building, false, 2);
    // Its line of sight once it stands; until then only its own ground.
    registry.emplace<Components::Sight>(building, Components::Sight{std::max(1, rules->los.ToInt()), !complete});

    // A farm carries its food. A foundation takes its ground and its sight
    // only when first worked on (Begin); a finished building has both now.
    Components::Resource stock;
    if (Farmland(registry, player, *rules, stock)) {
        registry.emplace<Components::Resource>(building, stock);
        // A Farm over a dead one takes its place: the remains go.
        if (rules->dead_unit >= 0) {
            const auto ground = FootprintBounds(origin, placed);
            std::vector<entt::entity> dead;
            auto remains = registry.view<const Components::Identity, const Components::Transform>();
            for (auto e : remains) {
                if (e == building || remains.get<const Components::Identity>(e).unit_id != rules->dead_unit) continue;
                if (ground.DistanceFrom(remains.get<const Components::Transform>(e).position).value == 0) {
                    dead.push_back(e);
                }
            }
            for (entt::entity e : dead) registry.destroy(e);
        }
    }
    if (complete) Hold(registry, grid, building);

    // The ground it stands on: a farm site's first stage from the start; the
    // dat's foundation terrain only once the building stands (Complete).
    Core::TerrainType ground;
    if (LaysCrop(*rules)) {
        LayGround(registry, grid, placed, tile_x, tile_y,
                  complete && FoundationGround(*rules, ground) ? ground
                                                               : FarmSiteGround(Core::Fixed32(0)));
    } else if (complete && FoundationGround(*rules, ground)) {
        LayGround(registry, grid, placed, tile_x, tile_y, ground);
    }

    Players::Contribute(registry, player, placed,
                        R::StorageMode::Permanent | R::StorageMode::WhileAlive, +1);
    if (complete) {
        if (Walls::IsGate(placed)) Walls::GateFinished(registry, grid, building);
        SpawnArt(registry, building);
        Arm(registry, building);
        Players::Contribute(registry, player, placed, R::StorageMode::WhileStanding, +1);
        // Standing finished counts as being finished (ecs/Score).
        Score::BuildingFinished(registry, player, placed);
        Techs::BuildingFinished(registry, player, placed);
    } else {
        SpawnSiteArt(registry, building, *rules);
    }
    // A wall beside it may now be a run where it was an end.
    Walls::RefreshAround(registry, building);
    return building;
}

void Complete(entt::registry& registry, Physics::Grid& grid, entt::entity building) {
    if (!registry.valid(building) ||
        !registry.all_of<Components::Building, Components::Identity>(building)) {
        return;
    }
    auto& state = registry.get<Components::Building>(building);
    if (state.state == Components::BuildingState::Complete) return;
    if (!state.begun) Hold(registry, grid, building);
    if (auto* sight = registry.try_get<Components::Sight>(building)) sight->footprint = false;
    state.state = Components::BuildingState::Complete;
    state.progress = Core::Fixed32(1);

    const int unit_id = registry.get<Components::Identity>(building).unit_id;
    const Core::PlayerId owner = OwnerOf(registry, building);
    // A gate opens to its own side from the moment it stands.
    if (Walls::IsGate(unit_id)) Walls::GateFinished(registry, grid, building);
    DestroyPieces(registry, building);
    registry.remove<Components::Sprite>(building);
    SpawnArt(registry, building);
    Arm(registry, building);
    // Its ground: a farm's crop comes up, anything else lays the dat's
    // foundation terrain.
    const R::UnitRules* rules = Players::UnitOf(registry, owner, unit_id);
    Core::TerrainType ground;
    if (rules != nullptr && FoundationGround(*rules, ground)) {
        const auto& at = registry.get<Components::Transform>(building).position;
        LayGround(registry, grid, unit_id, Physics::TileOf(at.x), Physics::TileOf(at.y), ground);
    }
    Players::Contribute(registry, owner, unit_id, R::StorageMode::WhileStanding, +1);
    Score::BuildingFinished(registry, owner, unit_id);
    Events::Emit(registry, {.kind = Events::Kind::Built, .subject = building, .player = owner,
                            .id = unit_id,
                            .at = registry.get<Components::Transform>(building).position});
    Techs::BuildingFinished(registry, owner, unit_id);
    Walls::RefreshAround(registry, building);
}

void Update(entt::registry& registry, Physics::Grid& grid) {
    // Collected first: finishing spawns art, which creates entities while the
    // view would be live.
    std::vector<entt::entity> finished;
    auto sites = registry.view<Components::Building, Components::Identity,
                               Components::Transform>();
    for (auto site : sites) {
        const auto& building = sites.get<Components::Building>(site);
        if (building.state != Components::BuildingState::UnderConstruction) continue;
        if (building.progress >= Core::Fixed32(1)) {
            finished.push_back(site);
            continue;
        }
        // A farm site's ground follows the work done on it.
        const int unit_id = sites.get<Components::Identity>(site).unit_id;
        const auto* rules = Players::UnitOf(registry, OwnerOf(registry, site), unit_id);
        if (rules != nullptr && LaysCrop(*rules)) {
            const auto& at = sites.get<Components::Transform>(site).position;
            LayGround(registry, grid, unit_id, Physics::TileOf(at.x), Physics::TileOf(at.y),
                      FarmSiteGround(building.progress));
        }
    }
    for (auto site : finished) Complete(registry, grid, site);
}

void Destroy(entt::registry& registry, Physics::Grid& grid, entt::entity building,
             bool rubble) {
    if (!registry.valid(building)) return;
    // Its worth leaves the economy score, while it is still what it was
    // (ecs/Score).
    Score::Removed(registry, building);

    // Whatever it was making is refunded, and research it held is available
    // to queue elsewhere.
    Production::Abandon(registry, building);
    // Whoever sheltered in it is turned out.
    Garrisons::Eject(registry, grid, building);
    // And the relics it held are left on the ground.
    Relics::Release(registry, building);

    // Anyone building it has nothing left to build.
    auto workers = registry.view<Components::Worker>();
    for (auto worker : workers) {
        auto& state = workers.get<Components::Worker>(worker);
        if (state.target_building != building) continue;
        state.target_building = entt::null;
        state.state = Core::UnitState::Idle;
    }

    if (registry.all_of<Components::Identity, Components::Transform>(building)) {
        auto& cfg = Core::ConfigManager::Get();
        const int unit_id = registry.get<Components::Identity>(building).unit_id;
        const auto at = registry.get<Components::Transform>(building).position;
        const Core::PlayerId owner = OwnerOf(registry, building);

        if (Walls::IsGate(unit_id)) Walls::GateGone(registry, grid, building);
        if (!registry.all_of<Components::Resource>(building) && Begun(registry, building)) {
            ForEachFootprintTile(unit_id, Physics::TileOf(at.x), Physics::TileOf(at.y), [&](int x, int y) {
                grid.SetWalkable(x, y, true);
            });
        }
        // A farm leaves its field harvested out.
        const R::UnitRules* farm = Players::UnitOf(registry, owner, unit_id);
        if (farm != nullptr && LaysCrop(*farm)) {
            LayGround(registry, grid, unit_id, Physics::TileOf(at.x), Physics::TileOf(at.y), Core::TerrainType::FarmDead);
        }

        const bool standing =
            registry.all_of<Components::Building>(building) &&
            registry.get<Components::Building>(building).state ==
                Components::BuildingState::Complete;
        Players::Contribute(registry, owner, unit_id,
                            standing ? (R::StorageMode::WhileAlive |
                                        R::StorageMode::WhileStanding)
                                     : R::StorageMode::WhileAlive,
                            -1);

        // What it leaves behind: the dat's rubble unit.
        const R::UnitRules* rules = Players::UnitOf(registry, owner, unit_id);
        const auto rubble_art = rules != nullptr ? cfg.GetObjectArt(rules->dead_unit)
                                                 : Core::ConfigManager::ObjectArt{};
        entt::entity remains = entt::null;
        if (rubble && rubble_art.Valid()) {
            remains = registry.create();
            registry.emplace<Components::Transform>(remains, at, at);
            Components::Sprite wreck{};
            wreck.slp_id = rubble_art.slp_id;
            wreck.slp_frame = 0;
            wreck.width = 0;
            wreck.height = 0;
            wreck.shadow_resolved = true;   // rubble casts none
            registry.emplace<Components::Sprite>(remains, std::move(wreck));
        }
        // Its fall: the dat's dying graphic -- a collapse in dust, with its
        // sound -- played once over the rubble.
        if (rubble && rules != nullptr && rules->dying_graphic >= 0) {
            const auto fall = cfg.GetGraphic(rules->dying_graphic);
            if (fall.Valid() && fall.frame_count > 0) {
                const entt::entity dust = registry.create();
                registry.emplace<Components::Transform>(dust, at, at);
                Components::Animation anim{};
                const int idle = static_cast<int>(Components::AnimState::Idle);
                anim.graphic[idle] = rules->dying_graphic;
                anim.slp[idle] = fall.slp_id;
                anim.frames[idle] = fall.frame_count;
                anim.frame_ms[idle] = cfg.GetFrameMs(rules->dying_graphic, 100);
                anim.needs_apply = true;
                const int32_t lasts = anim.frames[idle] * anim.frame_ms[idle];
                registry.emplace<Components::Animation>(dust, anim);
                Components::Sprite cloud{};
                cloud.width = 0;
                cloud.height = 0;
                registry.emplace<Components::Sprite>(dust, std::move(cloud));
                registry.emplace<Components::Dying>(dust, lasts);
            }
        }
        // A farm run out leaves its dead farm, the owner's to rebuild: a
        // villager right-clicked on it puts up a new Farm there ("Right-click
        // to rebuild this Farm.", string 3930).
        if (rubble && farm != nullptr && LaysCrop(*farm) && farm->dead_unit >= 0) {
            if (remains == entt::null) {
                remains = registry.create();
                registry.emplace<Components::Transform>(remains, at, at);
            }
            registry.emplace<Components::Identity>(remains, farm->dead_unit);
            registry.emplace<Components::Owner>(remains, owner);
        }
    }

    // Walls beside it lose a neighbour: an end becomes a post.
    const bool joined = registry.all_of<Components::Identity, Components::Transform>(building) &&
                        Walls::Joins(registry.get<Components::Identity>(building).unit_id);
    Components::Vector2D gone_at{};
    int gone_unit = -1;
    const Core::PlayerId gone_owner = OwnerOf(registry, building);
    if (joined) {
        gone_at = registry.get<Components::Transform>(building).position;
        gone_unit = registry.get<Components::Identity>(building).unit_id;
    }
    DestroyPieces(registry, building);
    registry.destroy(building);
    if (joined) {
        const auto ground = FootprintBounds(gone_at, gone_unit);
        Walls::Refresh(registry, gone_owner, Physics::TileOf(ground.min_x) - 1,
                       Physics::TileOf(ground.min_y) - 1, Physics::TileOf(ground.max_x) + 1,
                       Physics::TileOf(ground.max_y) + 1);
    }
}

// The spot comes from the approach planner. "In front" is toward the bottom
// of the screen: increasing x and y in world terms.
bool Doorstep(const entt::registry& registry, const Physics::Grid& grid,
              entt::entity building, Components::Vector2D& out, Physics::Layer layer) {
    if (!registry.valid(building) ||
        !registry.all_of<Components::Identity, Components::Transform>(building)) {
        return false;
    }
    const auto ground = FootprintBounds(
        registry.get<Components::Transform>(building).position,
        registry.get<Components::Identity>(building).unit_id);
    const Components::Vector2D middle{
        (ground.min_x + ground.max_x) / Core::Fixed32(2),
        (ground.min_y + ground.max_y) / Core::Fixed32(2)};
    const Core::Fixed32 ahead(8);

    std::vector<Components::Vector2D> standing;
    auto units = registry.view<const Components::Transform,
                               const Components::Selectable>();
    for (auto unit : units) {
        if (units.get<const Components::Selectable>(unit).priority <= 0) continue;
        const auto& at = units.get<const Components::Transform>(unit).position;
        if (ground.DistanceFrom(at) <= kDoorwayReach) standing.push_back(at);
    }

    Pathfinding::ApproachRequest ask;
    ask.target = ground;
    ask.from = {middle.x + ahead, middle.y + ahead};
    ask.standoff = kDoorStandoff;
    ask.clearance = kDoorClearance;
    ask.taken = standing.empty() ? nullptr : standing.data();
    ask.taken_count = standing.size();
    ask.layer = layer;

    const Pathfinding::ApproachResult spot = Pathfinding::FindStandingSpot(grid, ask);
    if (!spot.found) return false;
    out = spot.spot;
    return true;
}

void RefreshArt(entt::registry& registry, entt::entity building) {
    if (!registry.valid(building) ||
        !registry.all_of<Components::Building, Components::Identity,
                         Components::Transform>(building)) {
        return;
    }
    // A site keeps its construction art until it is finished; it is composed
    // from the owner's rules at that moment anyway.
    if (registry.get<Components::Building>(building).state !=
        Components::BuildingState::Complete) {
        return;
    }
    DestroyPieces(registry, building);
    registry.remove<Components::Sprite>(building);
    SpawnArt(registry, building);
}

} // namespace ECS::Buildings
