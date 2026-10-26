#include "hal/Avi.h"

#include <algorithm>
#include <cstring>

namespace HAL::Avi {

namespace {

uint16_t U16(const uint8_t* p) {
    uint16_t v;
    std::memcpy(&v, p, sizeof(v));
    return v;
}

uint32_t U32(const uint8_t* p) {
    uint32_t v;
    std::memcpy(&v, p, sizeof(v));
    return v;
}

constexpr uint32_t kRiff = FourCC("RIFF");
constexpr uint32_t kAvi = FourCC("AVI ");
constexpr uint32_t kList = FourCC("LIST");
constexpr uint32_t kHdrl = FourCC("hdrl");
constexpr uint32_t kStrl = FourCC("strl");
constexpr uint32_t kAvih = FourCC("avih");
constexpr uint32_t kStrh = FourCC("strh");
constexpr uint32_t kStrf = FourCC("strf");
constexpr uint32_t kMovi = FourCC("movi");
constexpr uint32_t kRec = FourCC("rec ");
constexpr uint32_t kIdx1 = FourCC("idx1");
constexpr uint32_t kVids = FourCC("vids");
constexpr uint32_t kAuds = FourCC("auds");

enum class Kind { None, Video, Audio };

struct Parse {
    File& file;
    std::vector<Kind> streams;   // by stream number, in strl order
    uint64_t movi_data = 0;      // the 'movi' type field's offset
    uint64_t movi_end = 0;
    bool have_movi = false;
};

bool ReadAt(std::ifstream& s, uint64_t at, void* into, size_t bytes) {
    s.clear();
    s.seekg(static_cast<std::streamoff>(at));
    return static_cast<bool>(s.read(static_cast<char*>(into), static_cast<std::streamsize>(bytes)));
}

// One strl: the stream's header and format.
void ReadStreamList(Parse& p, const uint8_t* data, size_t size) {
    Kind kind = Kind::None;
    size_t at = 0;
    while (at + 8 <= size) {
        const uint32_t id = U32(data + at);
        const uint32_t length = U32(data + at + 4);
        const uint8_t* body = data + at + 8;
        if (at + 8 + length > size) break;
        if (id == kStrh && length >= 48) {
            const uint32_t type = U32(body);
            if (type == kVids) {
                kind = Kind::Video;
                p.file.fourcc = U32(body + 4);
                p.file.scale = std::max<uint32_t>(1, U32(body + 20));
                p.file.rate = U32(body + 24);
            } else if (type == kAuds) {
                kind = Kind::Audio;
            }
        } else if (id == kStrf) {
            if (kind == Kind::Video && length >= 20) {
                // BITMAPINFOHEADER: size, width, height, planes, bits, compression.
                p.file.width = static_cast<int>(U32(body + 4));
                p.file.height = static_cast<int>(U32(body + 8));
                if (p.file.fourcc == 0) p.file.fourcc = U32(body + 16);
            } else if (kind == Kind::Audio && length >= 16) {
                // WAVEFORMATEX: tag, channels, rate, bytes a second, block, bits.
                p.file.audio_format = U16(body);
                p.file.channels = U16(body + 2);
                p.file.sample_rate = static_cast<int>(U32(body + 4));
                p.file.block_align = U16(body + 12);
                p.file.bits = U16(body + 14);
            }
        }
        at += 8 + length + (length & 1);
    }
    p.streams.push_back(kind);
}

void ReadHeaderList(Parse& p, const uint8_t* data, size_t size) {
    size_t at = 0;
    while (at + 8 <= size) {
        const uint32_t id = U32(data + at);
        const uint32_t length = U32(data + at + 4);
        if (at + 8 + length > size) break;
        if (id == kAvih && length >= 40) {
            p.file.micros_per_frame = U32(data + at + 8);
        } else if (id == kList && length >= 4 && U32(data + at + 8) == kStrl) {
            ReadStreamList(p, data + at + 12, length - 4);
        }
        at += 8 + length + (length & 1);
    }
}

// The stream a chunk id ("00dc", "01wb") belongs to, and what kind it is.
Kind KindOf(const Parse& p, uint32_t id) {
    const int tens = static_cast<int>(id & 0xFF) - '0';
    const int ones = static_cast<int>((id >> 8) & 0xFF) - '0';
    if (tens < 0 || tens > 9 || ones < 0 || ones > 9) return Kind::None;
    const size_t stream = static_cast<size_t>(tens * 10 + ones);
    if (stream >= p.streams.size()) return Kind::None;
    const uint32_t type = id >> 16;
    const Kind kind = p.streams[stream];
    if (kind == Kind::Video && (type == FourCC("..dc") >> 16 || type == FourCC("..db") >> 16)) {
        return Kind::Video;
    }
    if (kind == Kind::Audio && type == FourCC("..wb") >> 16) return Kind::Audio;
    return Kind::None;
}

void Add(Parse& p, Kind kind, uint64_t data_offset, uint32_t size) {
    File& f = p.file;
    if (kind == Kind::Video) {
        f.frames.push_back(Chunk{data_offset, size});
    } else if (kind == Kind::Audio) {
        f.audio.push_back(Chunk{data_offset, size});
        f.audio_bytes += size;
    } else {
        return;
    }
    f.largest_chunk = std::max(f.largest_chunk, size);
}

// idx1: sixteen bytes an entry -- id, flags, offset, size. The offsets count
// from the movi list's type field in most files and from the file's start
// in some; the first entry says which.
bool ReadIndex(Parse& p, const uint8_t* data, size_t size) {
    if (!p.have_movi || size < 16) return false;
    std::ifstream& s = p.file.stream;
    uint64_t base = p.movi_data;
    for (size_t at = 0; at + 16 <= size; at += 16) {
        const uint32_t id = U32(data + at);
        if (KindOf(p, id) == Kind::None) continue;
        const uint32_t offset = U32(data + at + 8);
        uint8_t header[4];
        if (ReadAt(s, base + offset, header, 4) && U32(header) == id) break;
        if (ReadAt(s, offset, header, 4) && U32(header) == id) {
            base = 0;
            break;
        }
        return false;
    }
    for (size_t at = 0; at + 16 <= size; at += 16) {
        const uint32_t id = U32(data + at);
        const Kind kind = KindOf(p, id);
        if (kind == Kind::None) continue;
        const uint64_t chunk = base + U32(data + at + 8);
        const uint32_t length = U32(data + at + 12);
        if (chunk + 8 + length > p.movi_end) return false;
        Add(p, kind, chunk + 8, length);
    }
    return !p.file.frames.empty();
}

// Without an index: every chunk of movi in turn, into 'rec ' groups.
void Walk(Parse& p, uint64_t at, uint64_t end) {
    std::ifstream& s = p.file.stream;
    uint8_t header[12];
    while (at + 8 <= end) {
        if (!ReadAt(s, at, header, 12)) break;
        const uint32_t id = U32(header);
        const uint32_t length = U32(header + 4);
        if (id == kList && U32(header + 8) == kRec) {
            Walk(p, at + 12, std::min(end, at + 8 + length));
        } else {
            Add(p, KindOf(p, id), at + 8, length);
        }
        at += 8 + static_cast<uint64_t>(length) + (length & 1);
    }
}

} // namespace

bool Open(const std::string& path, File& out, std::string& error) {
    out.stream.close();
    out = File{};
    out.stream.open(path, std::ios::binary);
    if (!out.stream) {
        error = "cannot open " + path;
        return false;
    }
    out.stream.seekg(0, std::ios::end);
    const auto file_size = static_cast<uint64_t>(out.stream.tellg());
    uint8_t riff[12];
    if (!ReadAt(out.stream, 0, riff, 12) || U32(riff) != kRiff || U32(riff + 8) != kAvi) {
        error = path + " is not an AVI";
        return false;
    }
    Parse p{out, {}, 0, 0, false};
    std::vector<uint8_t> index;
    const uint64_t end = std::min<uint64_t>(file_size, 8 + static_cast<uint64_t>(U32(riff + 4)));
    uint64_t at = 12;
    uint8_t header[12];
    while (at + 8 <= end && ReadAt(out.stream, at, header, 12)) {
        const uint32_t id = U32(header);
        const uint32_t length = U32(header + 4);
        if (id == kList && U32(header + 8) == kHdrl && length >= 4) {
            std::vector<uint8_t> list(length - 4);
            if (!ReadAt(out.stream, at + 12, list.data(), list.size())) break;
            ReadHeaderList(p, list.data(), list.size());
        } else if (id == kList && U32(header + 8) == kMovi) {
            p.movi_data = at + 8;
            p.movi_end = std::min(end, at + 8 + length);
            p.have_movi = true;
        } else if (id == kIdx1) {
            index.resize(length);
            if (!ReadAt(out.stream, at + 8, index.data(), index.size())) index.clear();
        }
        at += 8 + static_cast<uint64_t>(length) + (length & 1);
    }
    if (out.width <= 0 || out.rate == 0) {
        error = path + " has no video stream";
        return false;
    }
    if (!p.have_movi) {
        error = path + " has no movi list";
        return false;
    }
    if (!ReadIndex(p, index.data(), index.size())) {
        out.frames.clear();
        out.audio.clear();
        out.audio_bytes = 0;
        out.largest_chunk = 0;
        Walk(p, p.movi_data + 4, p.movi_end);
    }
    if (out.frames.empty()) {
        error = path + " has no video frames";
        return false;
    }
    if (out.audio_format != 1) {
        // Only PCM is played; anything else is left silent.
        out.audio.clear();
        out.audio_bytes = 0;
    }
    return true;
}

bool Read(File& file, const Chunk& chunk, std::vector<uint8_t>& buffer) {
    buffer.resize(chunk.size);
    return chunk.size == 0 || ReadAt(file.stream, chunk.offset, buffer.data(), chunk.size);
}

} // namespace HAL::Avi
