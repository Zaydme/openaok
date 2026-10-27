#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

#include "hal/SlpFile.h"

// The sound device: plays the game's WAVs straight out of its DRS archives.
//
// Every sound the game makes is a file in a DRS archive, by id -- unit voices
// and combat in sounds.drs, the interface's alarms and bells in interfac.drs --
// so this knows nothing about units or rules. It is told a file id, a volume
// and a pan, and mixes it with whatever else is playing. Files are decoded on
// first use and kept, converted once to the device's own format so the mixer
// only ever adds samples.
//
// Presentation only. Nothing here is read by the simulation.
namespace HAL::Audio {

// Opens the default output device. False, and every Play a no-op, when there
// is none (a headless run stays silent).
bool Open();
void Close();

// Archives searched for a file id, in the order given. `OpenArchive` reads
// one and keeps it; `UseArchive` borrows one already held open elsewhere,
// which must outlive the device.
bool OpenArchive(const std::string& path);
void UseArchive(const DrsArchive& archive);

// Starts a file. `volume` 0..1, `pan` -1 (left) .. 1 (right). False if the
// file is absent, or every voice is busy with something louder.
bool Play(int file_id, float volume, float pan);

// Whether a voice is playing this file now: a voice line is not started over
// itself.
bool Playing(int file_id);

// Starts an MP3 file on disk (the taunts, loose files Taunt/*.mp3), decoded
// on first use by minimp3 (third_party/minimp3) and kept. Centred, at
// `volume` 0..1. False if it cannot be read or decoded, or no voice is free.
bool PlayMp3(const std::string& path, float volume);

// Overall loudness, 0..1.
void SetVolume(float volume);

// The music channel: one MP3 track at a time, streamed (decoded a second or
// so ahead of where it plays) on a channel of its own, so no sound effect
// ever takes its place, at its own volume. `loop` starts it over at its end.
bool PlayMusic(const std::string& path, bool loop);
void StopMusic();
bool MusicPlaying();
// Once a frame: keeps the decoding ahead of the playing.
void PumpMusic();
void SetMusicVolume(float volume);

// The narration channel: a campaign slide show's spoken MP3, or a scenario's,
// streamed like the music but on a channel of its own under the sound volume.
// One at a time: a new one stops the last.
bool PlayNarration(const std::string& path);
void StopNarration();
bool NarrationPlaying();
// Once a frame while one plays: keeps its decoding ahead.
void PumpNarration();

// The movie channel: a movie's PCM soundtrack, handed over as the file is
// read and played as it arrives, on a channel of its own at full level,
// outside the sound and music volumes. `bits` 8 (unsigned) or 16 (signed),
// as a WAVE file has them.
bool OpenMovieAudio(int rate, int channels, int bits);
void QueueMovieAudio(const uint8_t* pcm, size_t bytes);
// Pauses the soundtrack where it is (when the game loses the focus), or
// resumes it.
void PauseMovieAudio(bool paused);
// Seconds of the soundtrack queued and not yet played.
double MovieAudioQueued();
// Seconds of it played since it was opened: the clock a movie's pictures
// keep time by.
double MovieAudioPlayed();
void CloseMovieAudio();

} // namespace HAL::Audio
