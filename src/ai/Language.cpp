#include "ai/Language.h"

#include <array>

namespace AI::Lang {

namespace {

using P = Param;

// The facts, in the order of the Fact enumeration.
const Signature kFacts[] = {
    {"true"}, {"false"},
    {"attack-soldier-count", {P::RelOp, P::Value}, 2},
    {"attack-warboat-count", {P::RelOp, P::Value}, 2},
    {"building-available", {P::Building}, 1},
    {"building-count", {P::RelOp, P::Value}, 2},
    {"building-count-total", {P::RelOp, P::Value}, 2},
    {"building-type-count", {P::Building, P::RelOp, P::Value}, 3},
    {"building-type-count-total", {P::Building, P::RelOp, P::Value}, 3},
    {"can-afford-building", {P::Building}, 1},
    {"can-afford-complete-wall", {P::Value, P::WallType}, 2},
    {"can-afford-research", {P::Research}, 1},
    {"can-afford-unit", {P::Unit}, 1},
    {"can-build", {P::Building}, 1},
    {"can-build-gate", {P::Value}, 1},
    {"can-build-gate-with-escrow", {P::Value}, 1},
    {"can-build-wall", {P::Value, P::WallType}, 2},
    {"can-build-wall-with-escrow", {P::Value, P::WallType}, 2},
    {"can-build-with-escrow", {P::Building}, 1},
    {"can-buy-commodity", {P::Commodity}, 1},
    {"can-research", {P::Research}, 1},
    {"can-research-with-escrow", {P::Research}, 1},
    {"can-sell-commodity", {P::Commodity}, 1},
    {"can-spy"}, {"can-spy-with-escrow"},
    {"can-train", {P::Unit}, 1},
    {"can-train-with-escrow", {P::Unit}, 1},
    {"cc-players-building-count", {P::Player, P::RelOp, P::Value}, 3},
    {"cc-players-building-type-count", {P::Player, P::Building, P::RelOp, P::Value}, 4},
    {"cc-players-unit-count", {P::Player, P::RelOp, P::Value}, 3},
    {"cc-players-unit-type-count", {P::Player, P::Unit, P::RelOp, P::Value}, 4},
    {"cheats-enabled"},
    {"civ-selected", {P::Civ}, 1},
    {"civilian-population", {P::RelOp, P::Value}, 2},
    {"commodity-buying-price", {P::Commodity, P::RelOp, P::Value}, 3},
    {"commodity-selling-price", {P::Commodity, P::RelOp, P::Value}, 3},
    {"current-age", {P::RelOp, P::Age}, 2},
    {"current-age-time", {P::RelOp, P::Value}, 2},
    {"current-score", {P::RelOp, P::Value}, 2},
    {"death-match-game"},
    {"defend-soldier-count", {P::RelOp, P::Value}, 2},
    {"defend-warboat-count", {P::RelOp, P::Value}, 2},
    {"difficulty", {P::RelOp, P::Difficulty}, 2},
    {"doctrine", {P::Value}, 1},
    {"dropsite-min-distance", {P::Resource, P::RelOp, P::Value}, 3},
    {"enemy-buildings-in-town"},
    {"enemy-captured-relics"},
    {"escrow-amount", {P::Resource, P::RelOp, P::Value}, 3},
    {"event-detected", {P::EventType, P::Value}, 2},
    {"food-amount", {P::RelOp, P::Value}, 2},
    {"game-time", {P::RelOp, P::Value}, 2},
    {"goal", {P::Value, P::Value}, 2},
    {"gold-amount", {P::RelOp, P::Value}, 2},
    {"housing-headroom", {P::RelOp, P::Value}, 2},
    {"idle-farm-count", {P::RelOp, P::Value}, 2},
    {"map-size", {P::MapSize}, 1},
    {"map-type", {P::MapType}, 1},
    {"military-population", {P::RelOp, P::Value}, 2},
    {"player-computer", {P::Player}, 1},
    {"player-human", {P::Player}, 1},
    {"player-in-game", {P::Player}, 1},
    {"player-number", {P::Player}, 1},
    {"player-resigned", {P::Player}, 1},
    {"player-valid", {P::Player}, 1},
    {"players-building-count", {P::Player, P::RelOp, P::Value}, 3},
    {"players-building-type-count", {P::Player, P::Building, P::RelOp, P::Value}, 4},
    {"players-civ", {P::Player, P::Civ}, 2},
    {"players-civilian-population", {P::Player, P::RelOp, P::Value}, 3},
    {"players-current-age", {P::Player, P::RelOp, P::Age}, 3},
    {"players-current-age-time", {P::Player, P::RelOp, P::Value}, 3},
    {"players-military-population", {P::Player, P::RelOp, P::Value}, 3},
    {"players-population", {P::Player, P::RelOp, P::Value}, 3},
    {"players-score", {P::Player, P::RelOp, P::Value}, 3},
    {"players-stance", {P::Player, P::Stance}, 2},
    {"players-tribute", {P::Player, P::Resource, P::RelOp, P::Value}, 4},
    {"players-tribute-memory", {P::Player, P::Resource, P::RelOp, P::Value}, 4},
    {"players-unit-count", {P::Player, P::RelOp, P::Value}, 3},
    {"players-unit-type-count", {P::Player, P::Unit, P::RelOp, P::Value}, 4},
    {"population", {P::RelOp, P::Value}, 2},
    {"population-cap", {P::RelOp, P::Value}, 2},
    {"population-headroom", {P::RelOp, P::Value}, 2},
    {"random-number", {P::RelOp, P::Value}, 2},
    {"regicide-game"},
    {"research-available", {P::Research}, 1},
    {"research-completed", {P::Research}, 1},
    {"resource-found", {P::Resource}, 1},
    {"shared-goal", {P::Value, P::Value}, 2},
    {"sheep-and-forage-too-far"},
    {"soldier-count", {P::RelOp, P::Value}, 2},
    {"stance-toward", {P::Player, P::Stance}, 2},
    {"starting-age", {P::RelOp, P::Age}, 2},
    {"starting-resources", {P::RelOp, P::StartingResources}, 2},
    {"stone-amount", {P::RelOp, P::Value}, 2},
    {"strategic-number", {P::StrategicNumber, P::RelOp, P::Value}, 3},
    {"taunt-detected", {P::Player, P::Value}, 2},
    {"timer-triggered", {P::Value}, 1},
    {"town-under-attack"},
    {"unit-available", {P::Unit}, 1},
    {"unit-count", {P::RelOp, P::Value}, 2},
    {"unit-count-total", {P::RelOp, P::Value}, 2},
    {"unit-type-count", {P::Unit, P::RelOp, P::Value}, 3},
    {"unit-type-count-total", {P::Unit, P::RelOp, P::Value}, 3},
    {"victory-condition", {P::VictoryCondition}, 1},
    {"wall-completed-percentage", {P::Value, P::RelOp, P::Value}, 3},
    {"wall-invisible-percentage", {P::Value, P::RelOp, P::Value}, 3},
    {"warboat-count", {P::RelOp, P::Value}, 2},
    {"wood-amount", {P::RelOp, P::Value}, 2},
};
static_assert(sizeof(kFacts) / sizeof(kFacts[0]) == static_cast<size_t>(Fact::COUNT));

// The actions, in the order of the Action enumeration.
const Signature kActions[] = {
    {"do-nothing"},
    {"acknowledge-event", {P::EventType, P::Value}, 2},
    {"acknowledge-taunt", {P::Player, P::Value}, 2},
    {"attack-now"},
    {"build", {P::Building}, 1},
    {"build-forward", {P::Building}, 1},
    {"build-gate", {P::Value}, 1},
    {"build-wall", {P::Value, P::WallType}, 2},
    {"buy-commodity", {P::Commodity}, 1},
    {"cc-add-resource", {P::Resource, P::Value}, 2},
    {"chat-local", {P::String}, 1},
    {"chat-local-using-id", {P::Value}, 1},
    {"chat-local-using-range", {P::Value, P::Value}, 2},
    {"chat-local-to-self", {P::String}, 1},
    {"chat-to-all", {P::String}, 1},
    {"chat-to-all-using-id", {P::Value}, 1},
    {"chat-to-all-using-range", {P::Value, P::Value}, 2},
    {"chat-to-allies", {P::String}, 1},
    {"chat-to-allies-using-id", {P::Value}, 1},
    {"chat-to-allies-using-range", {P::Value, P::Value}, 2},
    {"chat-to-enemies", {P::String}, 1},
    {"chat-to-enemies-using-id", {P::Value}, 1},
    {"chat-to-enemies-using-range", {P::Value, P::Value}, 2},
    {"chat-to-player", {P::Player, P::String}, 2},
    {"chat-to-player-using-id", {P::Player, P::Value}, 2},
    {"chat-to-player-using-range", {P::Player, P::Value, P::Value}, 3},
    {"chat-trace", {P::Value}, 1},
    {"clear-tribute-memory", {P::Player, P::Resource}, 2},
    {"delete-building", {P::Building}, 1},
    {"delete-unit", {P::Unit}, 1},
    {"disable-self"},
    {"disable-timer", {P::Value}, 1},
    {"enable-timer", {P::Value, P::Value}, 2},
    {"enable-wall-placement", {P::Value}, 1},
    {"generate-random-number", {P::Value}, 1},
    {"log", {P::String}, 1},
    {"log-trace", {P::Value}, 1},
    {"release-escrow", {P::Resource}, 1},
    {"research", {P::Research}, 1},
    {"resign"},
    {"sell-commodity", {P::Commodity}, 1},
    {"set-difficulty-parameter", {P::DifficultyParameter, P::Value}, 2},
    {"set-doctrine", {P::Value}, 1},
    {"set-escrow-percentage", {P::Resource, P::Value}, 2},
    {"set-goal", {P::Value, P::Value}, 2},
    {"set-shared-goal", {P::Value, P::Value}, 2},
    {"set-signal", {P::Value}, 1},
    {"set-stance", {P::Player, P::Stance}, 2},
    {"set-strategic-number", {P::StrategicNumber, P::Value}, 2},
    {"spy"},
    {"taunt", {P::Value}, 1},
    {"taunt-using-range", {P::Value, P::Value}, 2},
    {"train", {P::Unit}, 1},
    {"tribute-to-player", {P::Player, P::Resource, P::Value}, 3},
};
static_assert(sizeof(kActions) / sizeof(kActions[0]) == static_cast<size_t>(Action::COUNT));

struct Word {
    std::string_view name;
    int value;
};

template <size_t N>
bool Lookup(const Word (&table)[N], std::string_view name, int& out) {
    for (const Word& w : table) {
        if (w.name == name) {
            out = w.value;
            return true;
        }
    }
    return false;
}

const Word kLogic[] = {{"and", 0}, {"or", 1}, {"not", 2}, {"nand", 3}, {"nor", 4}, {"xor", 5}};

const Word kRelOps[] = {
    {"<", 0}, {"<=", 1}, {">", 2}, {">=", 3}, {"==", 4}, {"!=", 5},
    {"less-than", 0}, {"less-or-equal", 1}, {"greater-than", 2}, {"greater-or-equal", 3},
    {"equal", 4}, {"not-equal", 5},
};

const Word kWildcards[] = {
    {"any-ally", 0}, {"any-computer", 1}, {"any-computer-ally", 2}, {"any-computer-enemy", 3},
    {"any-computer-neutral", 4}, {"any-enemy", 5}, {"any-human", 6}, {"any-human-ally", 7},
    {"any-human-enemy", 8}, {"any-human-neutral", 9}, {"any-neutral", 10},
    {"every-ally", 11}, {"every-computer", 12}, {"every-enemy", 13}, {"every-human", 14},
    {"every-neutral", 15},
};
const Word kVariables[] = {
    {"this-any-ally", 0}, {"this-any-computer", 1}, {"this-any-computer-ally", 2},
    {"this-any-computer-enemy", 3}, {"this-any-computer-neutral", 4}, {"this-any-enemy", 5},
    {"this-any-human", 6}, {"this-any-human-ally", 7}, {"this-any-human-enemy", 8},
    {"this-any-human-neutral", 9}, {"this-any-neutral", 10},
};

const Word kAges[] = {{"dark-age", 0}, {"feudal-age", 1}, {"castle-age", 2}, {"imperial-age", 3},
                      {"post-imperial-age", 4}};

// The <civ> words, and the dat's own civ names lower-cased -- "british",
// "french", "german" -- which the standard scripts also use.
const Word kCivs[] = {
    {"briton", 1},   {"british", 1}, {"frankish", 2}, {"french", 2},   {"gothic", 3},
    {"teutonic", 4}, {"german", 4},  {"japanese", 5}, {"chinese", 6},  {"byzantine", 7},
    {"persian", 8},  {"saracen", 9}, {"turkish", 10}, {"viking", 11},  {"mongol", 12},
    {"celtic", 13},
};

// Hardest 0 .. easiest 4: the lobby's order, hardest first (LANGUAGE.DLL
// 11216-11220).
const Word kDifficulties[] = {{"hardest", 0}, {"hard", 1}, {"moderate", 2}, {"easy", 3},
                              {"easiest", 4}};

const std::string_view kMapTypeNames[kMapTypes] = {
    "arabia", "archipelago", "baltic", "black-forest", "coastal", "continental", "crater-lake",
    "fortress", "gold-rush", "highland", "islands", "mediterranean", "migration", "rivers",
    "team-islands", "scenario-map",
};

const Word kMapSizes[] = {{"tiny", 0}, {"small", 1}, {"medium", 2}, {"normal", 3}, {"large", 4},
                          {"giant", 5}};

// Core::ResourceType: food 0, wood 1, gold 2, stone 3.
const Word kResources[] = {{"food", 0}, {"wood", 1}, {"gold", 2}, {"stone", 3}};
const Word kStances[] = {{"ally", 0}, {"neutral", 1}, {"enemy", 2}};
const Word kStartingResources[] = {{"low-resources", 0}, {"medium-resources", 1},
                                   {"high-resources", 2}};
const Word kDifficultyParameters[] = {{"ability-to-dodge-missiles", 0},
                                      {"ability-to-maintain-distance", 1}};
const Word kVictoryConditions[] = {{"standard", 0}, {"conquest", 1}, {"time-limit", 2},
                                   {"score", 3}, {"custom", 4}};
const Word kEventTypes[] = {{"trigger", 0}};

struct SNEntry {
    std::string_view name;
    int value;   // the starting value
};

// The <strategic-number> words, in the SN enumeration's order, with their
// starting values. The maximum drop distances start at -1;
// sn-percent-attack-soldiers starts at 100, so a bare attack-now sends every
// available military unit.
const SNEntry kStrategic[] = {
    {"sn-percent-civilian-explorers", 34}, {"sn-percent-civilian-builders", 0},
    {"sn-percent-civilian-gatherers", 66}, {"sn-cap-civilian-explorers", 2},
    {"sn-cap-civilian-builders", 2}, {"sn-cap-civilian-gatherers", -1},
    {"sn-minimum-attack-group-size", 4}, {"sn-total-number-explorers", 4},
    {"sn-percent-enemy-sighted-response", 50}, {"sn-enemy-sighted-response-distance", 50},
    {"sn-sentry-distance", 12}, {"sn-relic-return-distance", 10},
    {"sn-minimum-defend-group-size", 1}, {"sn-maximum-attack-group-size", 10},
    {"sn-maximum-defend-group-size", 4}, {"sn-minimum-peace-like-level", 85},
    {"sn-percent-exploration-required", 100}, {"sn-zero-priority-distance", 50},
    {"sn-minimum-civilian-explorers", 0}, {"sn-number-attack-groups", 3},
    {"sn-number-defend-groups", 0}, {"sn-attack-group-gather-spacing", 4},
    {"sn-number-explore-groups", 0}, {"sn-minimum-explore-group-size", 1},
    {"sn-maximum-explore-group-size", 3}, {"sn-gold-defend-priority", 0},
    {"sn-stone-defend-priority", 0}, {"sn-forage-defend-priority", 0},
    {"sn-relic-defend-priority", 0}, {"sn-town-defend-priority", 7},
    {"sn-defense-distance", 3}, {"sn-number-boat-attack-groups", 2},
    {"sn-minimum-boat-attack-group-size", 1}, {"sn-maximum-boat-attack-group-size", 5},
    {"sn-number-boat-explore-groups", 1}, {"sn-minimum-boat-explore-group-size", 1},
    {"sn-maximum-boat-explore-group-size", 2}, {"sn-number-boat-defend-groups", 0},
    {"sn-minimum-boat-defend-group-size", 0}, {"sn-maximum-boat-defend-group-size", 0},
    {"sn-dock-defend-priority", 0}, {"sn-sentry-distance-variation", 2},
    {"sn-minimum-town-size", 12}, {"sn-maximum-town-size", 20},
    {"sn-group-commander-selection-method", 3}, {"sn-consecutive-idle-unit-limit", 15},
    {"sn-target-evaluation-distance", 50}, {"sn-target-evaluation-hitpoints", 0},
    {"sn-target-evaluation-damage-capability", 0}, {"sn-target-evaluation-kills", 0},
    {"sn-target-evaluation-ally-proximity", 0}, {"sn-target-evaluation-rof", 0},
    {"sn-target-evaluation-randomness", 0}, {"sn-camp-max-distance", 25},
    {"sn-mill-max-distance", 100}, {"sn-target-evaluation-attack-attempts", -25},
    {"sn-target-evaluation-range", 0}, {"sn-defend-overlap-distance", 5},
    {"sn-scale-minimum-attack-group-size", 1}, {"sn-scale-maximum-attack-group-size", 0},
    {"sn-attack-group-size-randomness", 1}, {"sn-scaling-frequency", 10},
    {"sn-maximum-gaia-attack-response", 3}, {"sn-build-frequency", 1},
    {"sn-attack-separation-time-randomness", 15}, {"sn-attack-intelligence", 0},
    {"sn-initial-attack-delay", 0}, {"sn-save-scenario-information", 0},
    {"sn-special-attack-type1", -1}, {"sn-special-attack-influence1", 0},
    {"sn-minimum-water-body-size-for-dock", 300}, {"sn-number-build-attempts-before-skip", 25},
    {"sn-max-skips-per-attempt", 10}, {"sn-food-gatherer-percentage", 0},
    {"sn-gold-gatherer-percentage", 0}, {"sn-stone-gatherer-percentage", 0},
    {"sn-wood-gatherer-percentage", 0}, {"sn-target-evaluation-continent", 100},
    {"sn-target-evaluation-siege-weapon", 0}, {"sn-group-leader-defense-distance", 3},
    {"sn-initial-attack-delay-type", 0}, {"sn-blot-exploration-map", 1}, {"sn-blot-size", 15},
    {"sn-intelligent-gathering", 0}, {"sn-task-ungrouped-soldiers", 1},
    {"sn-target-evaluation-boat", 0}, {"sn-number-enemy-objects-required", 10},
    {"sn-number-max-skip-cycles", 50}, {"sn-retask-gather-amount", 20},
    {"sn-max-retask-gather-amount", 40}, {"sn-max-build-plan-gatherer-percentage", 50},
    {"sn-food-dropsite-distance", 7}, {"sn-wood-dropsite-distance", 10},
    {"sn-stone-dropsite-distance", 25}, {"sn-gold-dropsite-distance", 7},
    {"sn-initial-exploration-required", 2}, {"sn-random-placement-factor", 50},
    {"sn-required-forest-tiles", 10}, {"sn-attack-diplomacy-impact", 10},
    {"sn-percent-half-exploration", 30}, {"sn-target-evaluation-time-kill-ratio", 20},
    {"sn-target-evaluation-in-progress", 50}, {"sn-attack-winning-player", 1},
    {"sn-coop-share-information", 1}, {"sn-attack-winning-player-factor", 25},
    {"sn-coop-share-attacking", 1}, {"sn-coop-share-attacking-interval", 120},
    {"sn-percentage-explore-exterminators", 50}, {"sn-track-player-history", 0},
    {"sn-minimum-dropsite-buffer", 25}, {"sn-use-by-type-max-gathering", 1},
    {"sn-minimum-boar-hunt-group-size", 5}, {"sn-minimum-amount-for-trading", 50},
    {"sn-easiest-reaction-percentage", 100}, {"sn-easier-reaction-percentage", 100},
    {"sn-hits-before-alliance-change", 3}, {"sn-allow-civilian-defense", 1},
    {"sn-number-forward-builders", 0}, {"sn-percent-attack-soldiers", 100},
    {"sn-percent-attack-boats", 100}, {"sn-do-not-scale-for-difficulty-level", 0},
    {"sn-group-form-distance", 0}, {"sn-ignore-attack-group-under-attack", 0},
    {"sn-gather-defense-units", 0}, {"sn-maximum-wood-drop-distance", -1},
    {"sn-maximum-food-drop-distance", -1}, {"sn-maximum-hunt-drop-distance", -1},
    {"sn-maximum-fish-boat-drop-distance", -1}, {"sn-maximum-gold-drop-distance", -1},
    {"sn-maximum-stone-drop-distance", -1},
};
static_assert(sizeof(kStrategic) / sizeof(kStrategic[0]) == static_cast<size_t>(SN::COUNT));

// The <unit>, <building> and <research-item> words and the dat entry each
// binds to, checked against the dat's own names (`Check`, run by
// `--verify`): `label` is the name LANGUAGE.DLL gives the unit, or the
// technology's name in the dat. A unit line is named after its first unit;
// its members are in the order the dat's upgrade effects carry the line's
// first unit through them.
struct ThingRow {
    Thing thing;
    std::string_view label;
};

constexpr Thing U(std::string_view name, int id) {
    Thing t{};
    t.name = name;
    t.kind = ThingKind::Unit;
    t.ids[0] = id;
    t.count = 1;
    return t;
}
constexpr Thing B(std::string_view name, int id) {
    Thing t = U(name, id);
    t.kind = ThingKind::Building;
    return t;
}
// A member of a line: `ids` is the whole line, `member` this unit's place.
constexpr Thing M(std::string_view name, ThingKind kind, std::array<int, kMaxLine> line, int member) {
    Thing t{};
    t.name = name;
    t.kind = kind;
    t.count = 0;
    for (int i = 0; i < kMaxLine; ++i) {
        t.ids[i] = line[static_cast<size_t>(i)];
        if (line[static_cast<size_t>(i)] >= 0) ++t.count;
    }
    t.member = member;
    return t;
}
constexpr Thing L(std::string_view name, ThingKind kind, std::array<int, kMaxLine> line) {
    Thing t = M(name, kind, line, 0);
    t.line = true;
    return t;
}
constexpr Thing R(std::string_view name, int tech) {
    Thing t{};
    t.name = name;
    t.kind = ThingKind::Research;
    t.tech = tech;
    return t;
}
constexpr Thing A(std::string_view name, int age) {
    Thing t{};
    t.name = name;
    t.kind = ThingKind::Age;
    t.tech = age;
    return t;
}
constexpr Thing S(std::string_view name, ThingKind kind) {
    Thing t{};
    t.name = name;
    t.kind = kind;
    return t;
}

constexpr ThingKind Un = ThingKind::Unit;
constexpr ThingKind Bu = ThingKind::Building;

constexpr std::array<int, kMaxLine> kArcher = {4, 24, 492, -1, -1};
constexpr std::array<int, kMaxLine> kCavArcher = {39, 474, -1, -1, -1};
constexpr std::array<int, kMaxLine> kSkirmisher = {7, 6, -1, -1, -1};
constexpr std::array<int, kMaxLine> kMilitia = {74, 75, 77, 473, 567};
constexpr std::array<int, kMaxLine> kSpearman = {93, 358, -1, -1, -1};
constexpr std::array<int, kMaxLine> kKnight = {38, 283, 569, -1, -1};
constexpr std::array<int, kMaxLine> kScout = {448, 546, -1, -1, -1};
constexpr std::array<int, kMaxLine> kCamel = {329, 330, -1, -1, -1};
constexpr std::array<int, kMaxLine> kRam = {35, 422, 548, -1, -1};
constexpr std::array<int, kMaxLine> kMangonel = {280, 550, 588, -1, -1};
constexpr std::array<int, kMaxLine> kScorpion = {279, 542, -1, -1, -1};
constexpr std::array<int, kMaxLine> kGalley = {539, 21, 442, -1, -1};
constexpr std::array<int, kMaxLine> kFireShip = {529, 532, -1, -1, -1};
constexpr std::array<int, kMaxLine> kDemoShip = {527, 528, -1, -1, -1};
constexpr std::array<int, kMaxLine> kCannonGalleon = {420, 691, -1, -1, -1};
constexpr std::array<int, kMaxLine> kLongboat = {250, 533, -1, -1, -1};
constexpr std::array<int, kMaxLine> kBerserk = {692, 694, -1, -1, -1};
constexpr std::array<int, kMaxLine> kCataphract = {40, 553, -1, -1, -1};
constexpr std::array<int, kMaxLine> kChuKoNu = {73, 559, -1, -1, -1};
constexpr std::array<int, kMaxLine> kHuskarl = {41, 555, -1, -1, -1};
constexpr std::array<int, kMaxLine> kJanissary = {46, 557, -1, -1, -1};
constexpr std::array<int, kMaxLine> kLongbowman = {8, 530, -1, -1, -1};
constexpr std::array<int, kMaxLine> kMameluke = {282, 556, -1, -1, -1};
constexpr std::array<int, kMaxLine> kMangudai = {11, 561, -1, -1, -1};
constexpr std::array<int, kMaxLine> kSamurai = {291, 560, -1, -1, -1};
constexpr std::array<int, kMaxLine> kTeutonicKnight = {25, 554, -1, -1, -1};
constexpr std::array<int, kMaxLine> kThrowingAxeman = {281, 531, -1, -1, -1};
constexpr std::array<int, kMaxLine> kWarElephant = {239, 558, -1, -1, -1};
constexpr std::array<int, kMaxLine> kWoadRaider = {232, 534, -1, -1, -1};
constexpr std::array<int, kMaxLine> kWatchTower = {79, 234, 235, -1, -1};
constexpr std::array<int, kMaxLine> kStoneWall = {117, 155, -1, -1, -1};

const ThingRow kThings[] = {
    // Buildings.
    {B("archery-range", 87), "Archery Range"},     {B("barracks", 12), "Barracks"},
    {B("blacksmith", 103), "Blacksmith"},          {B("bombard-tower", 236), "Bombard Tower"},
    {B("castle", 82), "Castle"},                   {B("dock", 45), "Dock"},
    {B("farm", 50), "Farm"},                       {B("fish-trap", 199), "Fish Trap"},
    {M("watch-tower", Bu, kWatchTower, 0), "Watch Tower"},
    {M("guard-tower", Bu, kWatchTower, 1), "Guard Tower"},
    {M("keep", Bu, kWatchTower, 2), "Keep"},
    {L("watch-tower-line", Bu, kWatchTower), "Watch Tower"},
    {B("house", 70), "House"},                     {B("lumber-camp", 562), "Lumber Camp"},
    {B("market", 84), "Market"},                   {B("mill", 68), "Mill"},
    {B("mining-camp", 584), "Mining Camp"},        {B("monastery", 104), "Monastery"},
    {B("outpost", 598), "Outpost"},                {B("siege-workshop", 49), "Siege Workshop"},
    {B("stable", 101), "Stable"},                  {B("town-center", 109), "Town Center"},
    {B("university", 209), "University"},          {B("wonder", 276), "Wonder"},
    {B("gate", 487), "Gate"},                      {B("palisade-wall", 72), "Palisade Wall"},
    {M("stone-wall", Bu, kStoneWall, 0), "Stone Wall"},
    {M("fortified-wall", Bu, kStoneWall, 1), "Fortified Wall"},
    {L("stone-wall-line", Bu, kStoneWall), "Stone Wall"},
    // Archery range.
    {M("archer", Un, kArcher, 0), "Archer"},       {M("crossbowman", Un, kArcher, 1), "Crossbowman"},
    {M("arbalest", Un, kArcher, 2), "Arbalest"},   {L("archer-line", Un, kArcher), "Archer"},
    {M("cavalry-archer", Un, kCavArcher, 0), "Cavalry Archer"},
    {M("heavy-cavalry-archer", Un, kCavArcher, 1), "Heavy Cavalry Archer"},
    {L("cavalry-archer-line", Un, kCavArcher), "Cavalry Archer"},
    {M("skirmisher", Un, kSkirmisher, 0), "Skirmisher"},
    {M("elite-skirmisher", Un, kSkirmisher, 1), "Elite Skirmisher"},
    {L("skirmisher-line", Un, kSkirmisher), "Skirmisher"},
    {U("hand-cannoneer", 5), "Hand Cannoneer"},
    // Barracks.
    {M("militiaman", Un, kMilitia, 0), "Militia"},
    {M("man-at-arms", Un, kMilitia, 1), "Man-at-Arms"},
    {M("long-swordsman", Un, kMilitia, 2), "Long Swordsman"},
    {M("two-handed-swordsman", Un, kMilitia, 3), "Two-Handed Swordsman"},
    {M("champion", Un, kMilitia, 4), "Champion"},
    {L("militiaman-line", Un, kMilitia), "Militia"},
    {M("spearman", Un, kSpearman, 0), "Spearman"}, {M("pikeman", Un, kSpearman, 1), "Pikeman"},
    {L("spearman-line", Un, kSpearman), "Spearman"},
    // Castle.
    {M("berserk", Un, kBerserk, 0), "Berserk"},    {M("elite-berserk", Un, kBerserk, 1), "Elite Berserk"},
    {L("berserk-line", Un, kBerserk), "Berserk"},
    {M("cataphract", Un, kCataphract, 0), "Cataphract"},
    {M("elite-cataphract", Un, kCataphract, 1), "Elite Cataphract"},
    {L("cataphract-line", Un, kCataphract), "Cataphract"},
    {M("chu-ko-nu", Un, kChuKoNu, 0), "Chu Ko Nu"},
    {M("elite-chu-ko-nu", Un, kChuKoNu, 1), "Elite Chu Ko Nu"},
    {L("chu-ko-nu-line", Un, kChuKoNu), "Chu Ko Nu"},
    {M("huskarl", Un, kHuskarl, 0), "Huskarl"},    {M("elite-huskarl", Un, kHuskarl, 1), "Elite Huskarl"},
    {L("huskarl-line", Un, kHuskarl), "Huskarl"},
    {M("janissary", Un, kJanissary, 0), "Janissary"},
    {M("elite-janissary", Un, kJanissary, 1), "Elite Janissary"},
    {L("janissary-line", Un, kJanissary), "Janissary"},
    {M("longbowman", Un, kLongbowman, 0), "Longbowman"},
    {M("elite-longbowman", Un, kLongbowman, 1), "Elite Longbowman"},
    {L("longbowman-line", Un, kLongbowman), "Longbowman"},
    {M("mameluke", Un, kMameluke, 0), "Mameluke"},
    {M("elite-mameluke", Un, kMameluke, 1), "Elite Mameluke"},
    {L("mameluke-line", Un, kMameluke), "Mameluke"},
    {M("mangudai", Un, kMangudai, 0), "Mangudai"},
    {M("elite-mangudai", Un, kMangudai, 1), "Elite Mangudai"},
    {L("mangudai-line", Un, kMangudai), "Mangudai"},
    {M("samurai", Un, kSamurai, 0), "Samurai"},    {M("elite-samurai", Un, kSamurai, 1), "Elite Samurai"},
    {L("samurai-line", Un, kSamurai), "Samurai"},
    {M("teutonic-knight", Un, kTeutonicKnight, 0), "Teutonic Knight"},
    {M("elite-teutonic-knight", Un, kTeutonicKnight, 1), "Elite Teutonic Knight"},
    {L("teutonic-knight-line", Un, kTeutonicKnight), "Teutonic Knight"},
    {M("throwing-axeman", Un, kThrowingAxeman, 0), "Throwing Axeman"},
    {M("elite-throwing-axeman", Un, kThrowingAxeman, 1), "Elite Throwing Axeman"},
    {L("throwing-axeman-line", Un, kThrowingAxeman), "Throwing Axeman"},
    {M("war-elephant", Un, kWarElephant, 0), "War Elephant"},
    {M("elite-war-elephant", Un, kWarElephant, 1), "Elite War Elephant"},
    {L("war-elephant-line", Un, kWarElephant), "War Elephant"},
    {M("woad-raider", Un, kWoadRaider, 0), "Woad Raider"},
    {M("elite-woad-raider", Un, kWoadRaider, 1), "Elite Woad Raider"},
    {L("woad-raider-line", Un, kWoadRaider), "Woad Raider"},
    {U("trebuchet", 42), "Trebuchet"},
    // Dock.
    {M("galley", Un, kGalley, 0), "Galley"},       {M("war-galley", Un, kGalley, 1), "War Galley"},
    {M("galleon", Un, kGalley, 2), "Galleon"},     {L("galley-line", Un, kGalley), "Galley"},
    {M("fire-ship", Un, kFireShip, 0), "Fire Ship"},
    {M("fast-fire-ship", Un, kFireShip, 1), "Fast Fire Ship"},
    {L("fire-ship-line", Un, kFireShip), "Fire Ship"},
    {M("demolition-ship", Un, kDemoShip, 0), "Demolition Ship"},
    {M("heavy-demolition-ship", Un, kDemoShip, 1), "Heavy Demolition Ship"},
    {L("demolition-ship-line", Un, kDemoShip), "Demolition Ship"},
    {M("cannon-galleon", Un, kCannonGalleon, 0), "Cannon Galleon"},
    {M("elite-cannon-galleon", Un, kCannonGalleon, 1), "Elite Cannon Galleon"},
    {L("cannon-galleon-line", Un, kCannonGalleon), "Cannon Galleon"},
    {M("longboat", Un, kLongboat, 0), "Longboat"}, {M("elite-longboat", Un, kLongboat, 1), "Elite Longboat"},
    {L("longboat-line", Un, kLongboat), "Longboat"},
    {U("fishing-ship", 13), "Fishing Ship"},       {U("trade-cog", 17), "Trade Cog"},
    {U("transport-ship", 545), "Transport Ship"},
    // Market, monastery, town center.
    {U("trade-cart", 128), "Trade Cart"},          {U("monk", 125), "Monk"},
    {U("villager", 83), "Villager"},
    // Siege workshop.
    {M("battering-ram", Un, kRam, 0), "Battering Ram"}, {M("capped-ram", Un, kRam, 1), "Capped Ram"},
    {M("siege-ram", Un, kRam, 2), "Siege Ram"},    {L("battering-ram-line", Un, kRam), "Battering Ram"},
    {M("mangonel", Un, kMangonel, 0), "Mangonel"}, {M("onager", Un, kMangonel, 1), "Onager"},
    {M("siege-onager", Un, kMangonel, 2), "Siege Onager"},
    {L("mangonel-line", Un, kMangonel), "Mangonel"},
    {M("scorpion", Un, kScorpion, 0), "Scorpion"}, {M("heavy-scorpion", Un, kScorpion, 1), "Heavy Scorpion"},
    {L("scorpion-line", Un, kScorpion), "Scorpion"},
    {U("bombard-cannon", 36), "Bombard Cannon"},
    // Stable.
    {M("knight", Un, kKnight, 0), "Knight"},       {M("cavalier", Un, kKnight, 1), "Cavalier"},
    {M("paladin", Un, kKnight, 2), "Paladin"},     {L("knight-line", Un, kKnight), "Knight"},
    {M("scout-cavalry", Un, kScout, 0), "Scout Cavalry"},
    {M("light-cavalry", Un, kScout, 1), "Light Cavalry"},
    {L("scout-cavalry-line", Un, kScout), "Scout Cavalry"},
    {M("camel", Un, kCamel, 0), "Camel"},          {M("heavy-camel", Un, kCamel, 1), "Heavy Camel"},
    {L("camel-line", Un, kCamel), "Camel"},
    // The player's own unique unit, worked out per civilisation from the
    // castle's elite technology for it (ai/Facts).
    {S("my-unique-unit", ThingKind::UniqueUnit), ""},
    {S("my-elite-unique-unit", ThingKind::EliteUniqueUnit), ""},
    {S("my-unique-unit-line", ThingKind::UniqueUnitLine), ""},
    {S("my-unique-unit-upgrade", ThingKind::UniqueUnitUpgrade), ""},
    // Ages, as research items.
    {A("dark-age", 0), ""}, {A("feudal-age", 1), ""}, {A("castle-age", 2), ""},
    {A("imperial-age", 3), ""},
    // Research items, by building.
    {R("ri-arbalest", 237), "Arbalest"},           {R("ri-crossbow", 100), "Crossbow"},
    {R("ri-elite-skirmisher", 98), "Elite Skirmisher"}, {R("ri-hand-cannon", 85), "Hand Cannon"},
    {R("ri-heavy-cavalry-archer", 218), "Heavy Cavalry Archer"},
    {R("ri-champion", 264), "Champion"},           {R("ri-long-swordsman", 207), "Long Swordsman"},
    {R("ri-man-at-arms", 222), "Man-At-Arms"},     {R("ri-pikeman", 197), "Pikeman"},
    {R("ri-squires", 215), "Squires"},             {R("ri-tracking", 90), "Tracking"},
    {R("ri-two-handed-swordsman", 217), "Two-Handed Swordsman"},
    {R("ri-blast-furnace", 75), "Blast Furnace"},  {R("ri-bodkin-arrow", 200), "Bodkin Arrow"},
    {R("ri-bracer", 201), "Bracer"},               {R("ri-chain-barding", 82), "Chain Barding Armor"},
    {R("ri-chain-mail", 76), "Chain Mail Armor"},  {R("ri-fletching", 199), "Fletching"},
    {R("ri-forging", 67), "Forging"},              {R("ri-iron-casting", 68), "Iron casting"},
    {R("ri-leather-archer-armor", 212), "Leather Archer Armor"},
    {R("ri-padded-archer-armor", 211), "Padded Archer Armor"},
    {R("ri-plate-barding", 80), "Plate Barding Armor"}, {R("ri-plate-mail", 77), "Plate Mail Armor"},
    {R("ri-ring-archer-armor", 219), "Ring Archer Armor"},
    {R("ri-scale-barding", 81), "Scale Barding Armor"}, {R("ri-scale-mail", 74), "Scale Mail Armor"},
    {R("ri-conscription", 315), "Conscription"},   {R("ri-hoardings", 379), "Hoardings"},
    {R("ri-sappers", 321), "Sappers"},             {R("ri-elite-berserk", 398), "Elite Berserk"},
    {R("ri-elite-cataphract", 361), "Elite Cataphract"},
    {R("ri-elite-chu-ko-nu", 362), "Elite Chu Ko Nu"}, {R("ri-elite-huskarl", 365), "Elite Huskarl"},
    {R("ri-elite-janissary", 369), "Elite Jannisary"},
    {R("ri-elite-longbowman", 360), "Elite Longbow"},
    {R("ri-elite-mameluke", 368), "Elite Mameluke"}, {R("ri-elite-mangudai", 371), "Elite Mangudai"},
    {R("ri-elite-samurai", 366), "Elite Samurai"},
    {R("ri-elite-teutonic-knight", 364), "Elite Teutonic Knight"},
    {R("ri-elite-throwing-axeman", 363), "Elite Throwing Axemen"},
    {R("ri-elite-war-elephant", 367), "Elite War Elephant"},
    {R("ri-elite-woad-raider", 370), "Elite Woad Raider"},
    {R("ri-cannon-galleon", 37), "Cannon Galleon"}, {R("ri-careening", 374), "Careening"},
    // Deck Guns is a word of the language but not in the dat; the scripts
    // ask for it, and it is never there to research.
    {R("ri-deck-guns", -1), ""},
    {R("ri-dry-dock", 375), "Dry Dock"},           {R("ri-elite-longboat", 372), "Unique Heroic Viking LBT"},
    {R("ri-fast-fire-ship", 246), "Fast Fire Ship"}, {R("ri-galleon", 35), "Galleon"},
    {R("ri-heavy-demolition-ship", 244), "Heavy Demolition"}, {R("ri-shipwright", 373), "Shipwright"},
    {R("ri-war-galley", 34), "War Galley"},        {R("ri-elite-cannon-galleon", 376), "Elite Cannon Galley"},
    // The axe's word is split in two so the simulation's floating-point
    // check does not take it for a type.
    {R("ri-bow-saw", 203), "Bow Saw"},             {R("ri-" "dou" "ble-bit-axe", 202), "Double Bit Axe"},
    {R("ri-two-man-saw", 221), "Two Man Saw"},     {R("ri-banking", 17), "Banking"},
    {R("ri-cartography", 19), "Cartography"},      {R("ri-coinage", 23), "Coinage"},
    {R("ri-guilds", 15), "Guilds"},                {R("ri-crop-rotation", 12), "Crop rotation"},
    {R("ri-heavy-plow", 13), "Heavy plow"},        {R("ri-horse-collar", 14), "Horse collar"},
    {R("ri-gold-mining", 55), "Gold Mining"},      {R("ri-gold-shaft-mining", 182), "Gold Shaft Mining"},
    {R("ri-stone-mining", 278), "Stone Mining"},   {R("ri-stone-shaft-mining", 279), "Stone Shaft Mining"},
    {R("ri-atonement", 319), "Atonement"},         {R("ri-block-printing", 230), "Block Printing"},
    {R("ri-faith", 45), "Faith"},                  {R("ri-fervor", 252), "Fervor"},
    {R("ri-illumination", 233), "Illumination"},   {R("ri-redemption", 316), "Redemption"},
    {R("ri-sanctity", 231), "Sanctity"},           {R("ri-bombard-cannon", 188), "Bombard Cannon"},
    {R("ri-capped-ram", 96), "Capped Ram"},        {R("ri-heavy-scorpion", 239), "Heavy Scorpion"},
    {R("ri-onager", 257), "Onager"},               {R("ri-scorpion", 94), "Scorpion"},
    {R("ri-siege-onager", 320), "Siege Onager"},   {R("ri-siege-ram", 255), "Siege Ram"},
    {R("ri-cavalier", 209), "Cavalier"},           {R("ri-heavy-camel", 236), "Heavy Camel"},
    {R("ri-husbandry", 39), "Husbandry"},          {R("ri-light-cavalry", 254), "Light Cavalry"},
    {R("ri-paladin", 265), "Paladin"},             {R("ri-hand-cart", 249), "Hand Cart"},
    {R("ri-loom", 22), "Loom"},                    {R("ri-town-patrol", 280), "Town Patrol"},
    {R("ri-town-watch", 8), "Town Watch"},         {R("ri-wheel-barrow", 213), "Wheel Barrow"},
    {R("ri-architecture", 51), "Architecture"},    {R("ri-ballistics", 93), "Ballistics"},
    {R("ri-bombard-tower", 64), "Bombard Tower"},  {R("ri-chemistry", 47), "Chemistry"},
    {R("ri-fortified-wall", 194), "Fortified Wall"}, {R("ri-guard-tower", 140), "Guard Tower"},
    {R("ri-heated-shot", 380), "Heated Shot"},     {R("ri-keep", 63), "Keep"},
    {R("ri-masonry", 50), "Masonry"},              {R("ri-murder-holes", 322), "Murder Holes"},
    {R("ri-siege-engineers", 377), "Siege Engineers"},
    // The interface calls it Treadmill Crane; the dat's name and the
    // script word are Stonecutting.
    {R("ri-stonecutting", 54), "Stone cutting"},
};

constexpr int kThingCount = static_cast<int>(sizeof(kThings) / sizeof(kThings[0]));

struct ThingTable {
    Thing things[kThingCount];
    ThingTable() {
        for (int i = 0; i < kThingCount; ++i) things[i] = kThings[i].thing;
    }
};

const ThingTable& Table() {
    static const ThingTable table;
    return table;
}

const std::string_view kCivSymbols[] = {
    "GAIA",         "BRITON-CIV",  "FRANKISH-CIV", "GOTHIC-CIV",   "TEUTONIC-CIV",
    "JAPANESE-CIV", "CHINESE-CIV", "BYZANTINE-CIV", "PERSIAN-CIV", "SARACEN-CIV",
    "TURKISH-CIV",  "VIKING-CIV",  "MONGOL-CIV",   "CELTIC-CIV",
};

} // namespace

const Signature& Describe(Fact fact) { return kFacts[static_cast<size_t>(fact)]; }
const Signature& Describe(Action action) { return kActions[static_cast<size_t>(action)]; }

bool FindFact(std::string_view name, Fact& out) {
    for (size_t i = 0; i < static_cast<size_t>(Fact::COUNT); ++i) {
        if (kFacts[i].name == name) {
            out = static_cast<Fact>(i);
            return true;
        }
    }
    return false;
}

bool FindAction(std::string_view name, Action& out) {
    for (size_t i = 0; i < static_cast<size_t>(Action::COUNT); ++i) {
        if (kActions[i].name == name) {
            out = static_cast<Action>(i);
            return true;
        }
    }
    return false;
}

bool FindLogic(std::string_view name, Logic& out) {
    int v = 0;
    if (!Lookup(kLogic, name, v)) return false;
    out = static_cast<Logic>(v);
    return true;
}

bool FindRelOp(std::string_view name, RelOp& out) {
    int v = 0;
    if (!Lookup(kRelOps, name, v)) return false;
    out = static_cast<RelOp>(v);
    return true;
}

bool FindWildcard(std::string_view name, Who& out) {
    int v = 0;
    if (!Lookup(kWildcards, name, v)) return false;
    out = static_cast<Who>(v);
    return true;
}

bool FindVariable(std::string_view name, Who& out) {
    int v = 0;
    if (!Lookup(kVariables, name, v)) return false;
    out = static_cast<Who>(v);
    return true;
}

bool IsEvery(Who who) { return static_cast<int>(who) >= kAnyWildcards; }

Who CriteriaOf(Who who) {
    switch (who) {
    case Who::EveryAlly:     return Who::AnyAlly;
    case Who::EveryComputer: return Who::AnyComputer;
    case Who::EveryEnemy:    return Who::AnyEnemy;
    case Who::EveryHuman:    return Who::AnyHuman;
    case Who::EveryNeutral:  return Who::AnyNeutral;
    default:                 return who;
    }
}

Criteria CriteriaFor(Who who) {
    constexpr int8_t kAlly = static_cast<int8_t>(Stance::Ally);
    constexpr int8_t kNeutral = static_cast<int8_t>(Stance::Neutral);
    constexpr int8_t kEnemy = static_cast<int8_t>(Stance::Enemy);
    switch (CriteriaOf(who)) {
    case Who::AnyAlly:            return {-1, kAlly};
    case Who::AnyComputer:        return {1, -1};
    case Who::AnyComputerAlly:    return {1, kAlly};
    case Who::AnyComputerEnemy:   return {1, kEnemy};
    case Who::AnyComputerNeutral: return {1, kNeutral};
    case Who::AnyEnemy:           return {-1, kEnemy};
    case Who::AnyHuman:           return {0, -1};
    case Who::AnyHumanAlly:       return {0, kAlly};
    case Who::AnyHumanEnemy:      return {0, kEnemy};
    case Who::AnyHumanNeutral:    return {0, kNeutral};
    case Who::AnyNeutral:         return {-1, kNeutral};
    default:                      return {};
    }
}

bool FindStrategicNumber(std::string_view name, SN& out) {
    for (size_t i = 0; i < static_cast<size_t>(SN::COUNT); ++i) {
        if (kStrategic[i].name == name) {
            out = static_cast<SN>(i);
            return true;
        }
    }
    // Another spelling of the forest SN.
    if (name == "sn-minimum-forest-tiles") {
        out = SN::RequiredForestTiles;
        return true;
    }
    return false;
}

std::string_view NameOf(SN sn) { return kStrategic[static_cast<size_t>(sn)].name; }
int DefaultOf(SN sn) { return kStrategic[static_cast<size_t>(sn)].value; }

const Thing* Things() { return Table().things; }
int ThingCount() { return kThingCount; }

int FindThing(std::string_view name) {
    for (int i = 0; i < kThingCount; ++i) {
        if (kThings[i].thing.name == name) return i;
    }
    return -1;
}

std::string_view ExpectedLabel(int thing) {
    if (thing < 0 || thing >= kThingCount) return {};
    return kThings[thing].label;
}

bool FindAge(std::string_view name, int& out) { return Lookup(kAges, name, out); }
bool FindCiv(std::string_view name, int& out) { return Lookup(kCivs, name, out); }
bool FindDifficulty(std::string_view name, int& out) { return Lookup(kDifficulties, name, out); }
bool FindMapSize(std::string_view name, int& out) { return Lookup(kMapSizes, name, out); }
bool FindResource(std::string_view name, int& out) { return Lookup(kResources, name, out); }
bool FindStance(std::string_view name, int& out) { return Lookup(kStances, name, out); }
bool FindStartingResources(std::string_view name, int& out) {
    return Lookup(kStartingResources, name, out);
}
bool FindDifficultyParameter(std::string_view name, int& out) {
    return Lookup(kDifficultyParameters, name, out);
}
bool FindVictoryCondition(std::string_view name, int& out) {
    return Lookup(kVictoryConditions, name, out);
}
bool FindEventType(std::string_view name, int& out) { return Lookup(kEventTypes, name, out); }

bool FindMapType(std::string_view name, int& out) {
    for (int i = 0; i < kMapTypes; ++i) {
        if (kMapTypeNames[i] == name) {
            out = i;
            return true;
        }
    }
    return false;
}

bool FindWallType(std::string_view name, int& out) {
    if (name != "palisade-wall" && name != "stone-wall" && name != "fortified-wall" &&
        name != "stone-wall-line") {
        return false;
    }
    out = FindThing(name);
    return out >= 0;
}

std::string_view MapTypeName(int index) {
    return index >= 0 && index < kMapTypes ? kMapTypeNames[index] : std::string_view{};
}

std::string_view CivSymbol(int dat_civ) {
    constexpr int n = static_cast<int>(sizeof(kCivSymbols) / sizeof(kCivSymbols[0]));
    return dat_civ >= 0 && dat_civ < n ? kCivSymbols[dat_civ] : std::string_view{};
}

bool Compare(int lhs, RelOp op, int rhs) {
    switch (op) {
    case RelOp::Less:         return lhs < rhs;
    case RelOp::LessEqual:    return lhs <= rhs;
    case RelOp::Greater:      return lhs > rhs;
    case RelOp::GreaterEqual: return lhs >= rhs;
    case RelOp::Equal:        return lhs == rhs;
    case RelOp::NotEqual:     return lhs != rhs;
    }
    return false;
}

} // namespace AI::Lang
