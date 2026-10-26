#include "core/Media.h"

#include <fstream>
#include <iostream>

#include <nlohmann/json.hpp>

namespace Core::Media {

namespace {

std::vector<std::vector<Delta>> g_deltas;   // by graphic id
std::vector<int> g_graphic_sound;           // by graphic id
std::vector<std::vector<AngleSounds>> g_frame_sounds;  // by graphic id
const std::vector<AngleSounds> kNoFrameSounds;
std::vector<Sound> g_sounds;                // by sound id
const std::vector<Delta> kNoDeltas;

template <typename T>
void Grow(std::vector<T>& table, size_t id, const T& fill) {
    if (table.size() <= id) table.resize(id + 1, fill);
}

} // namespace

bool Load(const std::string& path) {
    std::ifstream in(path);
    if (!in.is_open()) {
        std::cerr << "[Media] Cannot open " << path << std::endl;
        return false;
    }
    nlohmann::json data;
    in >> data;

    for (auto it = data["graphics"].begin(); it != data["graphics"].end(); ++it) {
        const size_t id = static_cast<size_t>(std::stoi(it.key()));
        Grow(g_deltas, id, std::vector<Delta>{});
        Grow(g_graphic_sound, id, -1);
        g_graphic_sound[id] = it.value().value("sound", -1);
        Grow(g_frame_sounds, id, std::vector<AngleSounds>{});
        if (it.value().contains("angle_sounds")) {
            for (const auto& angle : it.value()["angle_sounds"]) {
                AngleSounds entry;
                for (int k = 0; k < kFrameSoundsPerAngle && k < static_cast<int>(angle.size()); ++k) {
                    entry.sounds[k] = FrameSound{angle[k][0].get<int>(), angle[k][1].get<int>()};
                }
                g_frame_sounds[id].push_back(entry);
            }
        }
        for (const auto& d : it.value()["deltas"]) {
            g_deltas[id].push_back(Delta{d[0].get<int>(), d[1].get<int>(),
                                         d[2].get<int>(), d[3].get<int>()});
        }
    }
    for (auto it = data["sounds"].begin(); it != data["sounds"].end(); ++it) {
        const size_t id = static_cast<size_t>(std::stoi(it.key()));
        Grow(g_sounds, id, Sound{});
        Sound& sound = g_sounds[id];
        sound.valid = true;
        sound.delay = it.value().value("delay", 0);
        sound.total = it.value().value("total", 100);
        for (const auto& item : it.value()["items"]) {
            sound.items.push_back(SoundItem{item.value("resource", -1),
                                            item.value("probability", 100),
                                            item.value("civ", -1)});
        }
    }
    std::cout << "[Media] Loaded deltas and sounds for " << g_deltas.size()
              << " graphic slots, " << g_sounds.size() << " sound slots" << std::endl;
    return true;
}

const std::vector<Delta>& Deltas(int graphic_id) {
    if (graphic_id < 0 || graphic_id >= static_cast<int>(g_deltas.size())) return kNoDeltas;
    return g_deltas[graphic_id];
}

int GraphicSound(int graphic_id) {
    if (graphic_id < 0 || graphic_id >= static_cast<int>(g_graphic_sound.size())) return -1;
    return g_graphic_sound[graphic_id];
}

const std::vector<AngleSounds>& FrameSounds(int graphic_id) {
    if (graphic_id < 0 || graphic_id >= static_cast<int>(g_frame_sounds.size())) {
        return kNoFrameSounds;
    }
    return g_frame_sounds[graphic_id];
}

const Sound* SoundById(int sound_id) {
    if (sound_id < 0 || sound_id >= static_cast<int>(g_sounds.size())) return nullptr;
    return g_sounds[sound_id].valid ? &g_sounds[sound_id] : nullptr;
}

} // namespace Core::Media
