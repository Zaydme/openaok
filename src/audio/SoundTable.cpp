#include "audio/SoundTable.h"

#include <random>

#include "core/Media.h"

namespace Audio::SoundTable {

namespace {

std::minstd_rand g_rng{0x5EED};

} // namespace

int Pick(int sound_id, int civ) {
    const Core::Media::Sound* sound = Core::Media::SoundById(sound_id);
    if (sound == nullptr) return -1;

    bool own = false;
    for (const auto& item : sound->items) {
        if (item.civ == civ) { own = true; break; }
    }
    const int wanted = own ? civ : -1;
    int total = 0;
    for (const auto& item : sound->items) {
        if (item.civ == wanted) total += item.probability;
    }
    if (total <= 0) return -1;

    int roll = static_cast<int>(g_rng() % static_cast<unsigned>(total));
    for (const auto& item : sound->items) {
        if (item.civ != wanted) continue;
        if (roll < item.probability) return item.resource;
        roll -= item.probability;
    }
    return -1;
}

} // namespace Audio::SoundTable
