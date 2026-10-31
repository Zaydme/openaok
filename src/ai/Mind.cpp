#include "ai/Mind.h"

#include <algorithm>

namespace AI {

namespace {

using SN = Lang::SN;

// What the difficulty does to the scaled numbers, in percent of the value
// set: attack group sizes, numbers of attack groups, the enemy-sighted
// response and the percent of soldiers sent to attack; and the seconds added
// to the initial attack delay.
struct Scale {
    int sizes = 100, groups = 100, response = 100, attack = 100;
    int delay = 0;
};

Scale ScaleFor(const Core::GameSetup& setup) {
    const bool scenario = !setup.scenario.empty();
    switch (setup.difficulty) {
    case 1:  return scenario ? Scale{80, 80, 80, 100, 0} : Scale{};
    case 2:  return {75, 60, 60, 75, scenario ? 600 : 240};
    case 3:  return {50, 40, 40, 50, scenario ? 1200 : 900};
    case 4:  return {25, 20, 20, 25, scenario ? 1800 : 1260};
    default: return {};
    }
}

// The scaled value: a share, whole, at least 1.
int Scaled(int value, int percent) { return std::max(1, value * percent / 100); }

} // namespace

void SetStrategic(Mind& mind, SN sn, int value, const Core::GameSetup* setup) {
    int& v = mind.sn[static_cast<size_t>(sn)];
    v = value;
    if (v > 0 && setup != nullptr && mind.sn[static_cast<size_t>(SN::DoNotScaleForDifficultyLevel)] != 1) {
        const Scale scale = ScaleFor(*setup);
        switch (sn) {
        case SN::MinimumAttackGroupSize:
        case SN::MaximumAttackGroupSize:
        case SN::MinimumBoatAttackGroupSize:
        case SN::MaximumBoatAttackGroupSize:
            v = Scaled(v, scale.sizes);
            break;
        case SN::NumberAttackGroups:
        case SN::NumberBoatAttackGroups:
            v = Scaled(v, scale.groups);
            break;
        case SN::PercentEnemySightedResponse:
        case SN::EnemySightedResponseDistance:
            v = Scaled(v, scale.response);
            break;
        case SN::PercentAttackSoldiers:
        case SN::PercentAttackBoats:
            v = Scaled(v, scale.attack);
            break;
        case SN::InitialAttackDelay:
            v += scale.delay;
            break;
        default:
            break;
        }
    }
    switch (sn) {
    case SN::FoodGathererPercentage:
    case SN::WoodGathererPercentage:
    case SN::GoldGathererPercentage:
    case SN::StoneGathererPercentage:
        v = std::clamp(v, 0, 100);
        break;
    case SN::EnemySightedResponseDistance: v = std::min(v, 50); break;
    case SN::GroupFormDistance:           v = std::min(v, 30); break;
    case SN::MinimumBoarHuntGroupSize:    v = std::min(v, 8); break;
    case SN::MinimumExploreGroupSize:
    case SN::MaximumExploreGroupSize:
        if (v > 0) v = 1;
        break;
    default:
        break;
    }
}

void Reset(Mind& mind, size_t rules, const Core::GameSetup* setup) {
    mind = Mind{};
    mind.enabled.assign(rules, 1);
    for (int s = 0; s < Lang::kStrategicNumbers; ++s) {
        const auto sn = static_cast<SN>(s);
        SetStrategic(mind, sn, Lang::DefaultOf(sn), setup);
    }
}

} // namespace AI
