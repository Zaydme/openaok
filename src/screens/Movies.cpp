#include "screens/Movies.h"

#include <SDL2/SDL.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <thread>
#include <vector>

#include "hal/Audio.h"
#include "hal/Avi.h"
#include "hal/Indeo5.h"
#include "hal/Window.h"

namespace Screens::Movies {

namespace {

// The start-up sequence, in order.
constexpr const char* kStartup[] = {"mslogo", "eslogo", "age2"};

// The picture is shown at twice its size.
constexpr int kZoom = 2;

// Full screen, the display is switched to 640x480 for the movies.
constexpr int kScreenWidth = 640;
constexpr int kScreenHeight = 480;

// How much soundtrack is kept queued ahead of the playing.
constexpr double kAudioAhead = 1.0;

// How a movie ended.
enum class End { Next, All, Quit };

struct Player {
    HAL::Avi::File file;
    HAL::Indeo5::Decoder decoder;
    HAL::Indeo5::Picture picture;
    std::vector<uint8_t> chunk;
    size_t next_frame = 0;    // the next picture to decode
    size_t next_audio = 0;    // the next block of soundtrack to queue
    bool audio = false;       // the soundtrack is on the movie channel
    bool failed = false;
    bool showing = false;     // a picture of this movie is in the texture
    SDL_Texture* texture = nullptr;
    int texture_w = 0;
    int texture_h = 0;
    // The clock: the soundtrack's while it plays, the wall's otherwise, both
    // stopped while the focus is away.
    Uint64 started = 0;
    Uint64 paused_at = 0;
    Uint64 paused_total = 0;
    double audio_mark = 0.0;  // the soundtrack's clock when last seen running
    double wall_mark = 0.0;   // the wall clock at that moment
};

// Where a movie is looked for: the installed game's avi folder, then the
// CD's GAME/AVI. Either spelling, for case-sensitive disks.
std::string Find(const std::string& name) {
    std::string upper = name;
    std::transform(upper.begin(), upper.end(), upper.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    const std::string candidates[] = {
        "../assets/original/avi/" + name + ".avi",
        "../assets/original/AVI/" + upper + ".AVI",
        "../original_game_1999/GAME/AVI/" + upper + ".AVI",
        "../original_game_1999/GAME/avi/" + name + ".avi",
    };
    for (const std::string& path : candidates) {
        std::error_code ignored;
        if (std::filesystem::exists(path, ignored)) return path;
    }
    return {};
}

bool IsIndeo5(uint32_t fourcc) {
    return fourcc == HAL::Avi::FourCC("IV50") || fourcc == HAL::Avi::FourCC("iv50");
}

double Seconds(Uint64 ticks) {
    return static_cast<double>(ticks) / static_cast<double>(SDL_GetPerformanceFrequency());
}

double Wall(const Player& p) {
    const Uint64 now = p.paused_at != 0 ? p.paused_at : SDL_GetPerformanceCounter();
    return Seconds(now - p.started - p.paused_total);
}

// Where the movie is, in seconds. The soundtrack keeps the time while it
// plays, so the pictures stay with the voices even if the loop stalls; past
// its end the wall clock carries on from where it stopped.
double Now(Player& p) {
    const double wall = Wall(p);
    if (!p.audio) return wall;
    const bool running = p.next_audio < p.file.audio.size() || HAL::Audio::MovieAudioQueued() > 0.0;
    if (running) {
        p.audio_mark = HAL::Audio::MovieAudioPlayed();
        p.wall_mark = wall;
        return p.audio_mark;
    }
    return p.audio_mark + (wall - p.wall_mark);
}

double Duration(const Player& p) {
    return static_cast<double>(p.file.frames.size()) * p.file.scale / p.file.rate;
}

// Keeps a second of soundtrack queued ahead of the playing.
void Feed(Player& p) {
    while (p.audio && p.next_audio < p.file.audio.size() &&
           HAL::Audio::MovieAudioQueued() < kAudioAhead) {
        if (!HAL::Avi::Read(p.file, p.file.audio[p.next_audio], p.chunk)) {
            p.next_audio = p.file.audio.size();
            break;
        }
        HAL::Audio::QueueMovieAudio(p.chunk.data(), p.chunk.size());
        ++p.next_audio;
    }
}

bool Open(Player& p, const std::string& path, SDL_Renderer* renderer, bool sound) {
    std::string error;
    if (!HAL::Avi::Open(path, p.file, error)) {
        std::cerr << "[Movies] " << error << std::endl;
        return false;
    }
    if (!IsIndeo5(p.file.fourcc)) {
        std::cerr << "[Movies] " << path << " is not Indeo 5" << std::endl;
        return false;
    }
    HAL::Indeo5::Init(p.decoder);
    p.chunk.reserve(p.file.largest_chunk);
    p.next_frame = 0;
    p.next_audio = 0;
    p.failed = false;
    p.showing = false;
    if (p.texture == nullptr || p.texture_w != p.file.width || p.texture_h != p.file.height) {
        if (p.texture != nullptr) SDL_DestroyTexture(p.texture);
        p.texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
                                      SDL_TEXTUREACCESS_STREAMING, p.file.width, p.file.height);
        if (p.texture == nullptr) {
            std::cerr << "[Movies] " << SDL_GetError() << std::endl;
            return false;
        }
        // Each pixel doubled.
        SDL_SetTextureScaleMode(p.texture, SDL_ScaleModeNearest);
        p.texture_w = p.file.width;
        p.texture_h = p.file.height;
    }
    p.audio = sound && !p.file.audio.empty() &&
              HAL::Audio::OpenMovieAudio(p.file.sample_rate, p.file.channels, p.file.bits);
    Feed(p);
    p.started = SDL_GetPerformanceCounter();
    p.paused_at = 0;
    p.paused_total = 0;
    p.audio_mark = 0.0;
    p.wall_mark = 0.0;
    return true;
}

void Close(Player& p) {
    if (p.audio) HAL::Audio::CloseMovieAudio();
    p.audio = false;
    p.file.stream.close();
}

void Release(Player& p) {
    Close(p);
    if (p.texture != nullptr) SDL_DestroyTexture(p.texture);
    p.texture = nullptr;
}

// Decodes every picture whose time has come; true if there is a new one to
// show. Every frame is decoded, shown or not, since a predicted frame needs
// the one before it.
bool Advance(Player& p, size_t due) {
    due = std::min(due, p.file.frames.size());
    bool fresh = false;
    while (p.next_frame < due) {
        if (!HAL::Avi::Read(p.file, p.file.frames[p.next_frame], p.chunk)) {
            std::cerr << "[Movies] cannot read frame " << p.next_frame << std::endl;
            p.failed = true;
            break;
        }
        const HAL::Indeo5::Frame result =
            HAL::Indeo5::Decode(p.decoder, p.chunk.data(), p.chunk.size(), p.picture);
        if (result == HAL::Indeo5::Frame::Failed) {
            std::cerr << "[Movies] frame " << p.next_frame << ": " << p.decoder.error << std::endl;
            p.failed = true;
            break;
        }
        fresh |= result == HAL::Indeo5::Frame::Picture;
        ++p.next_frame;
    }
    return fresh;
}

// The newest picture into the streaming texture, converted in place.
void Upload(Player& p) {
    void* pixels = nullptr;
    int pitch = 0;
    if (p.texture == nullptr || p.picture.width != p.texture_w || p.picture.height != p.texture_h ||
        SDL_LockTexture(p.texture, nullptr, &pixels, &pitch) != 0) {
        return;
    }
    HAL::Indeo5::ToArgb(p.picture, static_cast<uint8_t*>(pixels), pitch);
    SDL_UnlockTexture(p.texture);
    p.showing = true;
}

// Black, and the picture at twice its size in the middle.
void Draw(HAL::Window& window, const Player& p) {
    window.BeginFrame();
    window.Clear(0, 0, 0);
    if (p.showing && p.texture != nullptr) {
        const int w = p.texture_w * kZoom;
        const int h = p.texture_h * kZoom;
        const SDL_Rect dest = {(window.GetWidth() - w) / 2, (window.GetHeight() - h) / 2, w, h};
        SDL_RenderCopy(window.GetSDLRenderer(), p.texture, nullptr, &dest);
    }
}

// The screen the movies are shown on: full screen, 640x480; in a window,
// the window as it is.
void UseScreen(HAL::Window& window) {
    if (window.Fullscreen()) window.SetGameSize(kScreenWidth, kScreenHeight);
}

void Pause(Player& p, bool pause) {
    if (pause == (p.paused_at != 0)) return;
    if (pause) {
        p.paused_at = SDL_GetPerformanceCounter();
    } else {
        p.paused_total += SDL_GetPerformanceCounter() - p.paused_at;
        p.paused_at = 0;
    }
    if (p.audio) HAL::Audio::PauseMovieAudio(pause);
}

End PlayOne(HAL::Window& window, Player& p, const std::string& name) {
    const std::string path = Find(name);
    if (path.empty()) {
        // Passed over as if it had played.
        std::cerr << "[Movies] " << name << ".avi not found" << std::endl;
        return End::Next;
    }
    if (!Open(p, path, window.GetSDLRenderer(), true)) return End::Next;
    const double fps = static_cast<double>(p.file.rate) / p.file.scale;
    // Noticed at the poll after the last picture: the next whole second.
    const double over = std::ceil(Duration(p));
    End end = End::Next;
    bool playing = true;
    while (playing) {
        const auto started = std::chrono::steady_clock::now();
        SDL_Event event;
        while (playing && SDL_PollEvent(&event)) {
            // A tap on a touch screen becomes a click, and Android's back is
            // Escape, there as everywhere else.
            window.TranslateEvent(event);
            switch (event.type) {
            case SDL_QUIT:
                end = End::Quit;
                playing = false;
                break;
            case SDL_MOUSEBUTTONDOWN:
                if (event.button.button == SDL_BUTTON_LEFT || event.button.button == SDL_BUTTON_RIGHT) {
                    playing = false;
                }
                break;
            case SDL_KEYDOWN:
                // Enter (either) and Space, this one; Escape, all of them.
                switch (event.key.keysym.sym) {
                case SDLK_RETURN:
                case SDLK_KP_ENTER:
                case SDLK_SPACE: playing = false; break;
                case SDLK_ESCAPE:
                    end = End::All;
                    playing = false;
                    break;
                default: break;
                }
                break;
            case SDL_WINDOWEVENT:
                if (event.window.event == SDL_WINDOWEVENT_FOCUS_LOST) Pause(p, true);
                if (event.window.event == SDL_WINDOWEVENT_FOCUS_GAINED) Pause(p, false);
                break;
            default: break;
            }
        }
        if (!playing) break;
        if (p.paused_at == 0) {
            Feed(p);
            const double now = Now(p);
            if (Advance(p, static_cast<size_t>(now * fps) + 1)) Upload(p);
            if (p.failed || (p.next_frame >= p.file.frames.size() && now >= over)) break;
        }
        Draw(window, p);
        window.Present();
        // A sixtieth of a second a loop: the pictures change fifteen times
        // a second, and the loop need not spin between them.
        const auto spent = std::chrono::steady_clock::now() - started;
        const auto tick = std::chrono::microseconds(1000000 / 60);
        if (spent < tick) std::this_thread::sleep_for(tick - spent);
    }
    std::cout << "[Movies] " << name << ": " << p.next_frame << " of " << p.file.frames.size()
              << " pictures in " << Wall(p) << " s"
              << (end == End::Quit ? ", window closed" : end == End::All ? ", skipped all"
                  : p.next_frame < p.file.frames.size() ? ", skipped" : "")
              << (p.audio ? "" : ", silent") << std::endl;
    Close(p);
    return end;
}

} // namespace

Outcome PlayStartup(HAL::Window& window, const std::string& only) {
    UseScreen(window);
    Player player;
    // A blank screen before the first.
    Draw(window, player);
    window.Present();
    Outcome outcome = Outcome::Finished;
    const std::vector<std::string> names =
        only.empty() ? std::vector<std::string>(std::begin(kStartup), std::end(kStartup))
                     : std::vector<std::string>{only};
    for (const std::string& name : names) {
        const End end = PlayOne(window, player, name);
        if (end == End::Quit) outcome = Outcome::Quit;
        if (end != End::Next) break;
    }
    Release(player);
    return outcome;
}

bool DrawStill(HAL::Window& window, const std::string& name, int frame) {
    UseScreen(window);
    Player player;
    const std::string path = Find(name);
    bool ok = !path.empty() && Open(player, path, window.GetSDLRenderer(), false) &&
              frame >= 0 && static_cast<size_t>(frame) < player.file.frames.size();
    if (ok) {
        Advance(player, static_cast<size_t>(frame) + 1);
        ok = !player.failed && player.picture.width > 0;
        if (ok) Upload(player);
    }
    if (ok) Draw(window, player);
    Release(player);
    return ok;
}

} // namespace Screens::Movies
