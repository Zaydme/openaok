#include "ecs/Search.h"

#include <initializer_list>

#include "core/GameSetup.h"
#include "ecs/Players.h"
#include "ecs/components/Identity.h"
#include "ecs/components/Owner.h"

namespace ECS::Search {

namespace {

using Mask = uint64_t;

constexpr Mask Of(std::initializer_list<int> classes) {
    Mask mask = 0;
    for (int c : classes) mask |= Mask{1} << c;
    return mask;
}

// The dat classes the kinds tell apart.
constexpr int kArtifact = 1;
constexpr int kTradeBoat = 2;
constexpr int kBuilding = 3;
constexpr int kCivilian = 4;
constexpr int kPrey = 9;
constexpr int kPredator = 10;
constexpr int kMonk = 18;
constexpr int kTradeCart = 19;
constexpr int kTransport = 20;
constexpr int kFishingBoat = 21;
constexpr int kWall = 27;
constexpr int kGate = 39;
constexpr int kMonkWithRelic = 43;
constexpr int kFarm = 49;
constexpr int kPackedUnit = 51;
constexpr int kTower = 52;
constexpr int kBoardingBoat = 53;
constexpr int kUnpackedSiege = 54;
constexpr int kKing = 59;
// The highest class a mask holds.
constexpr int kHighestClass = 63;

// The wild boar, which looks for nothing: it only answers its hunter.
constexpr int kBoar = 48;

// Siege weapons by their unit, whatever their class: the rams, mangonels,
// scorpions and the Bombard Cannon.
constexpr int kSiegeUnits[] = {35, 36, 279, 280, 422, 542, 548, 550, 588};

// What each kind looks for.
// Soldiers, ships of war, siege weapons and buildings: people, soldiers,
// ships, buildings, walls, farms, relics.
constexpr Mask kSoldierSought = Of({0, 2, 3, 4, 6, 12, 13, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26,
                                    27, 28, 35, 36, 39, 42, 43, 44, 45, 46, 47, 49, 50, 51, 52, 53,
                                    54, 55, 56, 57, 59});
// Wolves, looked for as well by a building that shoots and by a soldier the
// computer plays; never by a siege weapon.
constexpr Mask kWolves = Mask{1} << kPredator;
// Villagers: their work, what threatens them and where they go for cover.
constexpr Mask kVillagerSought = Of({0, 3, 4, 6, 7, 8, 9, 10, 12, 15, 17, 18, 19, 22, 23, 24, 25,
                                     26, 27, 28, 32, 33, 35, 36, 39, 44, 45, 46, 47, 49, 50, 52,
                                     55, 56, 57, 58});
// Predators: villagers, soldiers on foot and on horseback, and prey.
constexpr Mask kPredatorSought = Of({0, 4, 6, 9, 12, 23, 24, 25, 26, 28, 35, 36, 44, 45, 46, 50,
                                     56, 57});
// Prey and kings: whatever could hurt them.
constexpr Mask kPreySought = Of({0, 4, 6, 10, 12, 13, 17, 18, 19, 23, 24, 25, 26, 28, 35, 36, 43,
                                 44, 45, 46, 47, 50, 51, 54, 55, 56, 57, 59});
// The rest: Monks (and boarding boats), relic carts, trade, transport and
// fishing ships.
constexpr Mask kMonkSought = Of({0, 3, 4, 6, 10, 12, 13, 17, 18, 19, 23, 24, 25, 26, 28, 35, 36, 42,
                                 43, 44, 45, 46, 47, 50, 51, 52, 54, 55, 56, 57});
constexpr Mask kArtifactSought = Of({0, 2, 4, 6, 12, 13, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 28,
                                     35, 36, 43, 44, 45, 46, 47, 50, 51, 53, 54, 55, 56, 57, 59});
constexpr Mask kTradeBoatSought = Of({2, 3, 20, 21, 22, 52, 53});
constexpr Mask kTradeCartSought = Of({3, 17, 18, 19, 52});
constexpr Mask kTransportSought = Of({2, 3, 20, 21, 22, 27, 39, 49, 52, 53});
constexpr Mask kFishingSought = Of({2, 3, 5, 20, 21, 22, 27, 31, 33, 39, 49, 52, 53});

bool Siege(int unit_id) {
    for (int id : kSiegeUnits) {
        if (id == unit_id) return true;
    }
    return false;
}

bool PlayedByComputer(const entt::registry& registry, entt::entity self) {
    const auto* owner = registry.try_get<Components::Owner>(self);
    const auto* setup = registry.ctx().find<Core::GameSetup>();
    if (owner == nullptr || setup == nullptr) return false;
    const int p = owner->player_id;
    return p >= 0 && p < Core::kMaxPlayers && setup->players[p].computer;
}

} // namespace

uint64_t Sought(const entt::registry& registry, entt::entity self) {
    const auto* identity = registry.try_get<Components::Identity>(self);
    if (identity == nullptr) return 0;
    const Core::Rules::UnitRules* rules = Players::RulesFor(registry, self, identity->unit_id);
    if (rules == nullptr) return 0;
    const int cls = rules->cls;
    if (cls == kPackedUnit || cls == kUnpackedSiege || Siege(identity->unit_id)) return kSoldierSought;
    switch (cls) {
    case kArtifact:
        return kArtifactSought;
    case kTradeBoat:
        return kTradeBoatSought;
    case kTradeCart:
        return kTradeCartSought;
    case kBuilding:
    case kWall:
    case kGate:
    case kFarm:
    case kTower:
        return cls == kTower || rules->projectile >= 0 ? kSoldierSought | kWolves : kSoldierSought;
    case kCivilian:
        return kVillagerSought;
    case kPrey:
    case kKing:
        return kPreySought;
    case kPredator:
        return identity->unit_id == kBoar ? 0 : kPredatorSought;
    case kMonk:
    case kMonkWithRelic:
    case kBoardingBoat:
        return kMonkSought;
    case kTransport:
        return kTransportSought;
    case kFishingBoat:
        return kFishingSought;
    // Fish, plants, mines, scenery, livestock: nothing.
    case 5: case 7: case 8: case 11: case 14: case 15: case 16: case 29: case 30: case 31:
    case 32: case 33: case 34: case 58:
        return 0;
    default:
        return PlayedByComputer(registry, self) ? kSoldierSought | kWolves : kSoldierSought;
    }
}

bool Hunts(const entt::registry& registry, entt::entity self) {
    const auto* identity = registry.try_get<Components::Identity>(self);
    if (identity == nullptr || identity->unit_id == kBoar) return false;
    const Core::Rules::UnitRules* rules = Players::RulesFor(registry, self, identity->unit_id);
    return rules != nullptr && rules->cls == kPredator;
}

bool Holds(uint64_t classes, int cls) {
    return cls >= 0 && cls <= kHighestClass && ((classes >> cls) & 1) != 0;
}

} // namespace ECS::Search
