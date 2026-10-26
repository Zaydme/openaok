#pragma once
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

// Reader for the RIFF AVI files of GAME/AVI: the main header (avih), each
// stream's header and format (strh, strf), and where every video frame and
// audio block lies in the movi list -- from the idx1 index, or by walking
// movi (and its 'rec ' groups) when there is none.
//
// Only the index is held in memory; a chunk is read when it is wanted, into
// a buffer the caller keeps, so playing a 40 MB intro reads it a frame at a
// time and allocates nothing once the buffer has grown to the largest chunk.
namespace HAL::Avi {

// Where one chunk's data lies: past its own eight-byte header.
struct Chunk {
    uint64_t offset = 0;
    uint32_t size = 0;
};

struct File {
    std::ifstream stream;
    // The video: its compression (strh/strf), size and pace.
    uint32_t fourcc = 0;
    int width = 0;
    int height = 0;
    uint32_t rate = 0;    // frames per second = rate / scale
    uint32_t scale = 1;
    uint32_t micros_per_frame = 0;   // avih's, which says the same
    // A frame of no bytes is a dropped one: the last picture stands.
    std::vector<Chunk> frames;
    // The audio: interleaved PCM (format tag 1), or nothing.
    int audio_format = 0;
    int channels = 0;
    int sample_rate = 0;
    int bits = 0;
    int block_align = 0;
    std::vector<Chunk> audio;
    uint64_t audio_bytes = 0;
    uint32_t largest_chunk = 0;
};

// Opens and indexes a file. False, with `error` saying why, if it is not an
// AVI with a video stream.
bool Open(const std::string& path, File& out, std::string& error);

// Reads one chunk's bytes into `buffer`, which keeps its capacity.
bool Read(File& file, const Chunk& chunk, std::vector<uint8_t>& buffer);

constexpr uint32_t FourCC(const char (&s)[5]) {
    return static_cast<uint32_t>(static_cast<uint8_t>(s[0])) |
           static_cast<uint32_t>(static_cast<uint8_t>(s[1])) << 8 |
           static_cast<uint32_t>(static_cast<uint8_t>(s[2])) << 16 |
           static_cast<uint32_t>(static_cast<uint8_t>(s[3])) << 24;
}

} // namespace HAL::Avi
