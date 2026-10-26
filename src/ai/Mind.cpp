#include "ai/Mind.h"

namespace AI {

void Reset(Mind& mind, size_t rules) {
    mind = Mind{};
    mind.enabled.assign(rules, 1);
    for (int s = 0; s < Lang::kStrategicNumbers; ++s) {
        mind.sn[static_cast<size_t>(s)] = Lang::DefaultOf(static_cast<Lang::SN>(s));
    }
}

} // namespace AI
