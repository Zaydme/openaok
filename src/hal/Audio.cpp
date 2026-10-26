#include "hal/Audio.h"

#include <SDL2/SDL.h>

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#define MINIMP3_IMPLEMENTATION
#include <minimp3/minimp3.h>

namespace HAL::Audio {

namespace {

// Voices mixed at once.
constexpr int kVoices = 24;

// Volumes are fixed point out of this, so the mixer is integer throughout.
constexpr int kUnity = 256;

// A decoded file: interleaved stereo samples in the device's format.
struct Clip {
    std::vector<int16_t> samples;
};

struct Voice {
    const Clip* clip = nullptr;
    size_t position = 0;     // in samples, both channels counted
    int left = 0;            // 0..kUnity
    int right = 0;
    int file_id = -1;
};

// The music: one MP3 at a time, decoded a little ahead of where it plays.
// The main thread decodes into the stream (PumpMusic); the mixer takes from
// it.
struct MusicChannel {
    std::vector<uint8_t> file;
    size_t at = 0;
    mp3dec_t decoder{};
    SDL_AudioStream* stream = nullptr;
    int channels = 0;
    int rate = 0;
    bool loop = false;
    bool decoding = false;   // more of the file to decode
    bool active = false;     // the mixer is playing it
    int level = kUnity;
    std::vector<int16_t> scratch;
};

// A movie's soundtrack: PCM queued by the main thread a little ahead of
// where it plays, converted to the device's format on the way in. What the
// mixer has taken from it is the movie's clock.
struct MovieChannel {
    SDL_AudioStream* stream = nullptr;
    uint64_t played = 0;   // samples, both channels counted
    bool paused = false;
    std::vector<int16_t> scratch;
};

struct Device {
    SDL_AudioDeviceID id = 0;
    SDL_AudioSpec spec{};
    std::vector<std::unique_ptr<DrsArchive>> owned;
    std::vector<const DrsArchive*> archives;
    // Node-based, so a clip's address is stable while voices point at it.
    std::unordered_map<int, Clip> clips;
    // MP3s from disk, by path; their voices carry no archive file id.
    std::unordered_map<std::string, Clip> mp3s;
    Voice voices[kVoices];
    int master = kUnity * 3 / 4;
    std::vector<int32_t> mix;
    MusicChannel music;
    MusicChannel narration;   // played at the master volume, not the music's
    MovieChannel movie;
};

Device g_device;

void Mix(void* /*user*/, Uint8* stream, int bytes) {
    const size_t count = static_cast<size_t>(bytes) / sizeof(int16_t);
    auto& mix = g_device.mix;
    mix.assign(count, 0);
    for (Voice& voice : g_device.voices) {
        if (voice.clip == nullptr) continue;
        const auto& samples = voice.clip->samples;
        size_t i = 0;
        while (i + 1 < count && voice.position + 1 < samples.size()) {
            mix[i] += samples[voice.position] * voice.left;
            mix[i + 1] += samples[voice.position + 1] * voice.right;
            i += 2;
            voice.position += 2;
        }
        if (voice.position + 1 >= samples.size()) voice = Voice{};
    }
    // The music, under its own volume, not the master's.
    MusicChannel& music = g_device.music;
    size_t music_samples = 0;
    if (music.active && music.stream != nullptr) {
        music.scratch.resize(count);
        const int got = SDL_AudioStreamGet(music.stream, music.scratch.data(),
                                           static_cast<int>(count * sizeof(int16_t)));
        music_samples = got > 0 ? static_cast<size_t>(got) / sizeof(int16_t) : 0;
        if (music_samples == 0 && !music.decoding) music.active = false;
    }
    // A slide show's narration, under the sound volume.
    MusicChannel& narration = g_device.narration;
    size_t narration_samples = 0;
    if (narration.active && narration.stream != nullptr) {
        narration.scratch.resize(count);
        const int got = SDL_AudioStreamGet(narration.stream, narration.scratch.data(),
                                           static_cast<int>(count * sizeof(int16_t)));
        narration_samples = got > 0 ? static_cast<size_t>(got) / sizeof(int16_t) : 0;
        if (narration_samples == 0 && !narration.decoding) narration.active = false;
    }
    // A movie's soundtrack, at its own full level.
    MovieChannel& movie = g_device.movie;
    size_t movie_samples = 0;
    if (movie.stream != nullptr && !movie.paused) {
        movie.scratch.resize(count);
        const int got = SDL_AudioStreamGet(movie.stream, movie.scratch.data(),
                                           static_cast<int>(count * sizeof(int16_t)));
        movie_samples = got > 0 ? static_cast<size_t>(got) / sizeof(int16_t) : 0;
        movie.played += movie_samples;
    }
    auto* out = reinterpret_cast<int16_t*>(stream);
    for (size_t i = 0; i < count; ++i) {
        int32_t value = mix[i] / kUnity * g_device.master / kUnity;
        if (i < music_samples) value += music.scratch[i] * music.level / kUnity;
        if (i < narration_samples) value += narration.scratch[i] * g_device.master / kUnity;
        if (i < movie_samples) value += movie.scratch[i];
        out[i] = static_cast<int16_t>(std::clamp<int32_t>(value, INT16_MIN, INT16_MAX));
    }
}

// The file's bytes, from the first archive holding it.
bool Find(int file_id, const uint8_t*& data, uint32_t& size) {
    for (const DrsArchive* archive : g_device.archives) {
        auto it = archive->entries.find(static_cast<uint32_t>(file_id));
        if (it == archive->entries.end()) continue;
        data = archive->data.data() + it->second.offset;
        size = it->second.size;
        return true;
    }
    return false;
}

// Converts interleaved samples of one format to the device's rate and
// layout, into `clip`.
void Convert(Clip& clip, SDL_AudioFormat format, int channels, int rate, const void* data,
             int bytes) {
    SDL_AudioStream* stream =
        SDL_NewAudioStream(format, static_cast<Uint8>(channels), rate, g_device.spec.format,
                           g_device.spec.channels, g_device.spec.freq);
    if (stream == nullptr) return;
    SDL_AudioStreamPut(stream, data, bytes);
    SDL_AudioStreamFlush(stream);
    const int available = SDL_AudioStreamAvailable(stream);
    clip.samples.resize(static_cast<size_t>(available) / sizeof(int16_t));
    SDL_AudioStreamGet(stream, clip.samples.data(), available);
    SDL_FreeAudioStream(stream);
}

// Decodes an MP3 on disk, frame by frame, and converts it.
const Clip* LoadMp3(const std::string& path) {
    auto cached = g_device.mp3s.find(path);
    if (cached != g_device.mp3s.end()) {
        return cached->second.samples.empty() ? nullptr : &cached->second;
    }
    Clip& clip = g_device.mp3s[path];   // an empty entry marks a failure too
    std::ifstream file(path, std::ios::binary);
    if (!file) return nullptr;
    const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)),
                                     std::istreambuf_iterator<char>());
    mp3dec_t decoder;
    mp3dec_init(&decoder);
    std::vector<int16_t> pcm;
    int channels = 0;
    int rate = 0;
    size_t at = 0;
    mp3d_sample_t frame[MINIMP3_MAX_SAMPLES_PER_FRAME];
    while (at < bytes.size()) {
        mp3dec_frame_info_t info{};
        const int samples = mp3dec_decode_frame(&decoder, bytes.data() + at,
                                                static_cast<int>(bytes.size() - at), frame, &info);
        if (info.frame_bytes <= 0) break;   // nothing more it can read
        at += static_cast<size_t>(info.frame_bytes);
        if (samples <= 0) continue;         // a tag, or a frame skipped
        if (channels == 0) {
            channels = info.channels;
            rate = info.hz;
        }
        if (info.channels != channels || info.hz != rate) continue;
        pcm.insert(pcm.end(), frame, frame + samples * info.channels);
    }
    if (pcm.empty() || channels <= 0 || rate <= 0) return nullptr;
    Convert(clip, AUDIO_S16SYS, channels, rate, pcm.data(),
            static_cast<int>(pcm.size() * sizeof(int16_t)));
    return clip.samples.empty() ? nullptr : &clip;
}

// Decodes a file and converts it to the device's rate and layout.
const Clip* Load(int file_id) {
    auto cached = g_device.clips.find(file_id);
    if (cached != g_device.clips.end()) {
        return cached->second.samples.empty() ? nullptr : &cached->second;
    }
    Clip& clip = g_device.clips[file_id];   // an empty entry marks a failure too

    const uint8_t* data = nullptr;
    uint32_t size = 0;
    if (!Find(file_id, data, size)) return nullptr;
    SDL_AudioSpec spec{};
    Uint8* buffer = nullptr;
    Uint32 length = 0;
    SDL_RWops* rw = SDL_RWFromConstMem(data, static_cast<int>(size));
    if (SDL_LoadWAV_RW(rw, 1, &spec, &buffer, &length) == nullptr) return nullptr;

    Convert(clip, spec.format, spec.channels, spec.freq, buffer, static_cast<int>(length));
    SDL_FreeWAV(buffer);
    return clip.samples.empty() ? nullptr : &clip;
}

} // namespace

bool Open() {
    if (g_device.id != 0) return true;
    if (SDL_WasInit(SDL_INIT_AUDIO) == 0 && SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
        std::cerr << "[Audio] No audio: " << SDL_GetError() << std::endl;
        return false;
    }
    SDL_AudioSpec want{};
    want.freq = 44100;
    want.format = AUDIO_S16SYS;
    want.channels = 2;
    want.samples = 1024;
    want.callback = Mix;
    g_device.id = SDL_OpenAudioDevice(nullptr, 0, &want, &g_device.spec, 0);
    if (g_device.id == 0) {
        std::cerr << "[Audio] No output device: " << SDL_GetError() << std::endl;
        return false;
    }
    SDL_PauseAudioDevice(g_device.id, 0);
    return true;
}

void Close() {
    if (g_device.id == 0) return;
    StopMusic();
    StopNarration();
    CloseMovieAudio();
    SDL_CloseAudioDevice(g_device.id);
    g_device.id = 0;
    for (Voice& voice : g_device.voices) voice = Voice{};
    g_device.clips.clear();
    g_device.mp3s.clear();
}

bool OpenArchive(const std::string& path) {
    auto archive = std::make_unique<DrsArchive>();
    if (!DrsOpen(path, *archive)) return false;
    g_device.archives.push_back(archive.get());
    g_device.owned.push_back(std::move(archive));
    return true;
}

void UseArchive(const DrsArchive& archive) {
    g_device.archives.push_back(&archive);
}

namespace {

// Starts a decoded clip on a free voice, or the quietest if this is louder.
bool Start(const Clip* clip, int file_id, float volume, float pan) {
    // Equal-power-ish pan: the far side fades, the near side holds.
    const float level = std::clamp(volume, 0.0f, 1.0f);
    const float p = std::clamp(pan, -1.0f, 1.0f);
    const int left = static_cast<int>(level * std::min(1.0f, 1.0f - p) * kUnity);
    const int right = static_cast<int>(level * std::min(1.0f, 1.0f + p) * kUnity);

    SDL_LockAudioDevice(g_device.id);
    // A free voice, or else the quietest one if this is louder.
    int chosen = -1;
    int quietest = kUnity * 2;
    for (int v = 0; v < kVoices; ++v) {
        const Voice& voice = g_device.voices[v];
        if (voice.clip == nullptr) { chosen = v; break; }
        const int loudness = voice.left + voice.right;
        if (loudness < quietest) { quietest = loudness; chosen = v; }
    }
    const bool started = chosen >= 0 && (g_device.voices[chosen].clip == nullptr ||
                                         quietest < left + right);
    if (started) g_device.voices[chosen] = Voice{clip, 0, left, right, file_id};
    SDL_UnlockAudioDevice(g_device.id);
    return started;
}

} // namespace

bool Play(int file_id, float volume, float pan) {
    if (g_device.id == 0 || volume <= 0.0f) return false;
    const Clip* clip = Load(file_id);
    if (clip == nullptr) return false;
    return Start(clip, file_id, volume, pan);
}

bool PlayMp3(const std::string& path, float volume) {
    if (g_device.id == 0 || volume <= 0.0f) return false;
    const Clip* clip = LoadMp3(path);
    if (clip == nullptr) return false;
    return Start(clip, -1, volume, 0.0f);
}

bool Playing(int file_id) {
    if (g_device.id == 0) return false;
    SDL_LockAudioDevice(g_device.id);
    bool playing = false;
    for (const Voice& voice : g_device.voices) {
        if (voice.clip != nullptr && voice.file_id == file_id) { playing = true; break; }
    }
    SDL_UnlockAudioDevice(g_device.id);
    return playing;
}

void SetVolume(float volume) {
    const int master = static_cast<int>(std::clamp(volume, 0.0f, 1.0f) * kUnity);
    if (g_device.id != 0) SDL_LockAudioDevice(g_device.id);
    g_device.master = master;
    if (g_device.id != 0) SDL_UnlockAudioDevice(g_device.id);
}

namespace {

// Decodes frames of the music file into its stream until about `seconds` of
// it are waiting. Called with the device unlocked.
void DecodeAhead(MusicChannel& music, float seconds) {
    const int ahead = static_cast<int>(seconds * static_cast<float>(g_device.spec.freq)) *
                      g_device.spec.channels * static_cast<int>(sizeof(int16_t));
    mp3d_sample_t frame[MINIMP3_MAX_SAMPLES_PER_FRAME];
    int restarts = 0;
    while (music.decoding) {
        SDL_LockAudioDevice(g_device.id);
        const int waiting = music.stream != nullptr ? SDL_AudioStreamAvailable(music.stream) : 0;
        SDL_UnlockAudioDevice(g_device.id);
        if (waiting >= ahead) return;
        mp3dec_frame_info_t info{};
        const int samples = music.at < music.file.size()
            ? mp3dec_decode_frame(&music.decoder, music.file.data() + music.at,
                                  static_cast<int>(music.file.size() - music.at), frame, &info)
            : 0;
        if (info.frame_bytes <= 0) {
            // The end: round again for a looping track, else done.
            if (music.loop && restarts++ < 2) {
                music.at = 0;
                mp3dec_init(&music.decoder);
                continue;
            }
            music.decoding = false;
            return;
        }
        music.at += static_cast<size_t>(info.frame_bytes);
        if (samples <= 0) continue;   // a tag, or a frame skipped
        SDL_LockAudioDevice(g_device.id);
        if (music.stream == nullptr) {
            music.channels = info.channels;
            music.rate = info.hz;
            music.stream = SDL_NewAudioStream(AUDIO_S16SYS, static_cast<Uint8>(info.channels),
                                              info.hz, g_device.spec.format,
                                              g_device.spec.channels, g_device.spec.freq);
        }
        if (music.stream != nullptr && info.channels == music.channels && info.hz == music.rate) {
            SDL_AudioStreamPut(music.stream, frame,
                               samples * info.channels * static_cast<int>(sizeof(int16_t)));
        }
        SDL_UnlockAudioDevice(g_device.id);
    }
}

} // namespace

namespace {

void StopChannel(MusicChannel& channel) {
    if (g_device.id != 0) SDL_LockAudioDevice(g_device.id);
    channel.active = false;
    channel.decoding = false;
    if (channel.stream != nullptr) SDL_FreeAudioStream(channel.stream);
    channel.stream = nullptr;
    if (g_device.id != 0) SDL_UnlockAudioDevice(g_device.id);
    channel.file.clear();
    channel.file.shrink_to_fit();
}

bool StartChannel(MusicChannel& channel, const std::string& path, bool loop) {
    StopChannel(channel);
    std::ifstream file(path, std::ios::binary);
    if (!file) return false;
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)),
                               std::istreambuf_iterator<char>());
    channel.file = std::move(bytes);
    channel.at = 0;
    mp3dec_init(&channel.decoder);
    channel.loop = loop;
    channel.decoding = true;
    DecodeAhead(channel, 0.5f);
    SDL_LockAudioDevice(g_device.id);
    channel.active = channel.stream != nullptr;
    const bool started = channel.active;
    SDL_UnlockAudioDevice(g_device.id);
    return started;
}

bool ChannelPlaying(const MusicChannel& channel) {
    if (g_device.id == 0) return false;
    SDL_LockAudioDevice(g_device.id);
    const bool playing = channel.active;
    SDL_UnlockAudioDevice(g_device.id);
    return playing;
}

} // namespace

bool PlayMusic(const std::string& path, bool loop) {
    if (g_device.id == 0) return false;
    return StartChannel(g_device.music, path, loop);
}

void StopMusic() { StopChannel(g_device.music); }

bool MusicPlaying() { return ChannelPlaying(g_device.music); }

void PumpMusic() {
    if (g_device.id == 0) return;
    DecodeAhead(g_device.music, 1.0f);
}

bool PlayNarration(const std::string& path) {
    if (g_device.id == 0) return false;
    return StartChannel(g_device.narration, path, false);
}

void StopNarration() { StopChannel(g_device.narration); }

bool NarrationPlaying() { return ChannelPlaying(g_device.narration); }

void PumpNarration() {
    if (g_device.id == 0) return;
    DecodeAhead(g_device.narration, 1.0f);
}

void SetMusicVolume(float volume) {
    const int level = static_cast<int>(std::clamp(volume, 0.0f, 1.0f) * kUnity);
    if (g_device.id != 0) SDL_LockAudioDevice(g_device.id);
    g_device.music.level = level;
    if (g_device.id != 0) SDL_UnlockAudioDevice(g_device.id);
}

bool OpenMovieAudio(int rate, int channels, int bits) {
    if (g_device.id == 0) return false;
    CloseMovieAudio();
    if (rate <= 0 || channels <= 0 || (bits != 8 && bits != 16)) return false;
    SDL_AudioStream* stream = SDL_NewAudioStream(
        bits == 8 ? AUDIO_U8 : AUDIO_S16LSB, static_cast<Uint8>(channels), rate,
        g_device.spec.format, g_device.spec.channels, g_device.spec.freq);
    if (stream == nullptr) return false;
    SDL_LockAudioDevice(g_device.id);
    g_device.movie.stream = stream;
    g_device.movie.played = 0;
    g_device.movie.paused = false;
    SDL_UnlockAudioDevice(g_device.id);
    return true;
}

void PauseMovieAudio(bool paused) {
    if (g_device.id == 0) return;
    SDL_LockAudioDevice(g_device.id);
    g_device.movie.paused = paused;
    SDL_UnlockAudioDevice(g_device.id);
}

void QueueMovieAudio(const uint8_t* pcm, size_t bytes) {
    if (g_device.id == 0 || bytes == 0) return;
    SDL_LockAudioDevice(g_device.id);
    if (g_device.movie.stream != nullptr) {
        SDL_AudioStreamPut(g_device.movie.stream, pcm, static_cast<int>(bytes));
    }
    SDL_UnlockAudioDevice(g_device.id);
}

namespace {

double DeviceSeconds(uint64_t samples) {
    const double per_second =
        static_cast<double>(g_device.spec.freq) * static_cast<double>(g_device.spec.channels);
    return per_second > 0.0 ? static_cast<double>(samples) / per_second : 0.0;
}

} // namespace

double MovieAudioQueued() {
    if (g_device.id == 0) return 0.0;
    SDL_LockAudioDevice(g_device.id);
    const int bytes = g_device.movie.stream != nullptr
        ? SDL_AudioStreamAvailable(g_device.movie.stream) : 0;
    SDL_UnlockAudioDevice(g_device.id);
    return DeviceSeconds(static_cast<uint64_t>(bytes) / sizeof(int16_t));
}

double MovieAudioPlayed() {
    if (g_device.id == 0) return 0.0;
    SDL_LockAudioDevice(g_device.id);
    const uint64_t played = g_device.movie.played;
    SDL_UnlockAudioDevice(g_device.id);
    return DeviceSeconds(played);
}

void CloseMovieAudio() {
    if (g_device.id == 0) return;
    SDL_LockAudioDevice(g_device.id);
    SDL_AudioStream* stream = g_device.movie.stream;
    g_device.movie.stream = nullptr;
    g_device.movie.played = 0;
    SDL_UnlockAudioDevice(g_device.id);
    if (stream != nullptr) SDL_FreeAudioStream(stream);
}

} // namespace HAL::Audio
