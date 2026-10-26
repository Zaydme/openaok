#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "core/Command.h"
#include "core/GameSetup.h"

// The bytes a network game sends: commands, the lobby's setup, and the frame
// every message is wrapped in. Little-endian, fixed widths, fixed-point values
// as their raw 64 bits: the same bytes on every machine.
// An entity travels as its id, which is the same on every machine because
// every machine builds the same world.
namespace Net::Wire {

struct Writer {
    std::vector<uint8_t> bytes;
    void U8(uint8_t v) { bytes.push_back(v); }
    void U16(uint16_t v);
    void U32(uint32_t v);
    void U64(uint64_t v);
    void I32(int32_t v) { U32(static_cast<uint32_t>(v)); }
    void Fixed(Core::Fixed32 v) { U64(static_cast<uint64_t>(v.value)); }
    void Entity(entt::entity e) { U32(static_cast<uint32_t>(entt::to_integral(e))); }
    void Text(const std::string& s);
};

struct Reader {
    const std::vector<uint8_t>& bytes;
    size_t at = 0;
    bool ok = true;
    explicit Reader(const std::vector<uint8_t>& b) : bytes(b) {}
    uint8_t U8();
    uint16_t U16();
    uint32_t U32();
    uint64_t U64();
    int32_t I32() { return static_cast<int32_t>(U32()); }
    Core::Fixed32 Fixed() { return Core::Fixed32(static_cast<int64_t>(U64()), true); }
    entt::entity Entity() { return static_cast<entt::entity>(U32()); }
    std::string Text();
    bool Done() const { return at >= bytes.size(); }
};

void Write(Writer& out, const Core::Command& command);
bool Read(Reader& in, Core::Command& command);

void Write(Writer& out, const Core::GameSetup& setup);
bool Read(Reader& in, Core::GameSetup& setup);

} // namespace Net::Wire
