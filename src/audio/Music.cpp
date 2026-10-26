#include "audio/Music.h"

#include <filesystem>
#include <string>

#include "hal/Audio.h"

namespace Audio::Music {

namespace {

const char* kStream = "../original_game_1999/GAME/SOUND/STREAM/";

// The civilisations' themes by dat civ id.
const char* kCivThemes[] = {
    "RANDOM.MP3",                                   // 0: random
    "BRITISH.MP3", "FRENCH.MP3", "GOTH.MP3", "TEUTON.MP3", "JAPANESE.MP3",
    "CHINESE.MP3", "BYZANTIN.MP3", "PERSIAN.MP3", "SARACEN.MP3", "TURK.MP3",
    "VIKING.MP3", "MONGOL.MP3", "CELT.MP3",
};

enum class Mode { None, Menus, Game, Once };

struct State {
    Mode mode = Mode::None;
    int next_track = 1;   // the playlist's next stream\music%d.mp3
};

State& S() {
    static State state;
    return state;
}

bool Play(const std::string& name, bool loop) {
    return HAL::Audio::PlayMusic(std::string(kStream) + name, loop);
}

// The playlist's next track that exists, round from the first again.
bool NextInPlaylist() {
    State& s = S();
    for (int tries = 0; tries < 2; ++tries) {
        const std::string name = "MUSIC" + std::to_string(s.next_track) + ".MP3";
        if (std::filesystem::exists(std::string(kStream) + name) && Play(name, false)) {
            ++s.next_track;
            return true;
        }
        if (s.next_track == 1) return false;   // none at all
        s.next_track = 1;
    }
    return false;
}

} // namespace

void Menus() {
    State& s = S();
    if (s.mode == Mode::Menus && HAL::Audio::MusicPlaying()) return;
    s.mode = Mode::Menus;
    Play("OPEN.MP3", true);
}

void Credits() {
    S().mode = Mode::Once;
    Play("CREDITS.MP3", false);
}

void Game() {
    State& s = S();
    s.mode = Mode::Game;
    HAL::Audio::StopMusic();
    s.next_track = 1;
    NextInPlaylist();
}

void Ended(bool won) {
    S().mode = Mode::Once;
    Play(won ? "WON1.MP3" : "LOST.MP3", false);
}

void Civ(int civ) {
    if (civ < 0 || civ >= static_cast<int>(std::size(kCivThemes))) return;
    S().mode = Mode::Once;
    Play(kCivThemes[civ], false);
}

void SetVolume(int volume) {
    HAL::Audio::SetMusicVolume(static_cast<float>(volume) / 10.0f);
}

void Update() {
    HAL::Audio::PumpMusic();
    State& s = S();
    if (HAL::Audio::MusicPlaying()) return;
    // A track has ended: the menus go back to their own, a game to the next
    // of its playlist.
    if (s.mode == Mode::Game) NextInPlaylist();
}

} // namespace Audio::Music
