// The scenario writer: Scenario::Read's inverse, field for field in the same
// order, for version 1.18 with player data 1.20 and triggers 1.6 -- the
// version of every game file and the one the scenario editor saves.
//
// What the game plays is written from the File's fields; what it does not
// (the editor's copies, the sixteen-slot tables' last eight slots, unnamed
// fields) from File::kept, as the reader found it. A file read and written
// back is therefore the file read, once inflated (src/verify/EditorCheck.cpp
// tests this on the game's 32 scenarios).
#include "scenario/Scenario.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <fstream>

#include "hal/stb/stb_image.h"
#include "scenario/Deflate.h"
#include "scenario/Encoding.h"

namespace Scenario {

namespace Encoding {

std::string Cp1252(const std::string& utf8) {
    // Windows-1252's own 0x80-0x9F, as Utf8 maps them.
    static const uint16_t kHigh[32] = {
        0x20AC, 0x0081, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160,
        0x2039, 0x0152, 0x008D, 0x017D, 0x008F, 0x0090, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022,
        0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x009D, 0x017E, 0x0178};
    std::string out;
    out.reserve(utf8.size());
    for (size_t i = 0; i < utf8.size();) {
        const auto c = static_cast<uint8_t>(utf8[i]);
        uint32_t code = c;
        size_t length = 1;
        if (c >= 0xF0) {
            length = 4;
            code = c & 0x07u;
        } else if (c >= 0xE0) {
            length = 3;
            code = c & 0x0Fu;
        } else if (c >= 0xC0) {
            length = 2;
            code = c & 0x1Fu;
        }
        for (size_t k = 1; k < length && i + k < utf8.size(); ++k) {
            code = (code << 6) | (static_cast<uint8_t>(utf8[i + k]) & 0x3Fu);
        }
        i += length;
        if (code < 0x80 || (code >= 0xA0 && code <= 0xFF)) {
            out += static_cast<char>(code);
            continue;
        }
        char byte = '?';
        for (int k = 0; k < 32; ++k) {
            if (kHigh[k] == code) byte = static_cast<char>(0x80 + k);
        }
        out += byte;
    }
    return out;
}

uint32_t BitsFromFixed(Core::Fixed32 value) {
    if (value.value == 0) return 0;
    const bool negative = value.value < 0;
    const uint64_t magnitude = negative ? static_cast<uint64_t>(0) - static_cast<uint64_t>(value.value)
                                        : static_cast<uint64_t>(value.value);
    int high = 63;
    while (((magnitude >> high) & 1u) == 0) --high;
    // value = magnitude * 2^-32 = 1.f * 2^(high - 32).
    int exponent = high - Core::Fixed32::FRACTIONAL_BITS;
    uint64_t mantissa = 0;
    if (high > 23) {
        const int shift = high - 23;
        mantissa = magnitude >> shift;
        const uint64_t rest = magnitude & ((uint64_t{1} << shift) - 1);
        const uint64_t half = uint64_t{1} << (shift - 1);
        if (rest > half || (rest == half && (mantissa & 1u) != 0)) {
            ++mantissa;
            if (mantissa == (uint64_t{1} << 24)) {
                mantissa >>= 1;
                ++exponent;
            }
        }
    } else {
        mantissa = magnitude << (23 - high);
    }
    return (negative ? 0x80000000u : 0u) | (static_cast<uint32_t>(exponent + 127) << 23) |
           static_cast<uint32_t>(mantissa & 0x7FFFFFu);
}

} // namespace Encoding

namespace {

using Encoding::BitsFromFixed;
using Encoding::Cp1252;
using Encoding::FixedFromBits;
using Encoding::Utf8;

constexpr uint32_t kSeparator = 0xFFFFFF9Du;
// The table widths of player data 1.20.
constexpr int kTechSlots = 30;
constexpr int kUnitSlots = 30;
constexpr int kBuildingSlots = 20;
constexpr int kEffectFields = 23;
constexpr int kConditionFields = 16;

struct Out {
    std::vector<uint8_t> bytes;

    void U8(uint8_t v) { bytes.push_back(v); }
    void U16(uint16_t v) {
        U8(static_cast<uint8_t>(v & 0xFFu));
        U8(static_cast<uint8_t>(v >> 8));
    }
    void I16(int16_t v) { U16(static_cast<uint16_t>(v)); }
    void U32(uint32_t v) {
        for (int i = 0; i < 4; ++i) U8(static_cast<uint8_t>((v >> (8 * i)) & 0xFFu));
    }
    void I32(int32_t v) { U32(static_cast<uint32_t>(v)); }
    void U64(uint64_t v) {
        U32(static_cast<uint32_t>(v & 0xFFFFFFFFu));
        U32(static_cast<uint32_t>(v >> 32));
    }
    void Raw(const std::string& s) { bytes.insert(bytes.end(), s.begin(), s.end()); }
    void Zeros(size_t n) { bytes.insert(bytes.end(), n, 0); }
    void Float(Core::Fixed32 v) { U32(BitsFromFixed(v)); }
    // A float the file had, while it still stands for the same value.
    void Float(Core::Fixed32 v, uint32_t written) {
        U32(FixedFromBits(written) == v ? written : BitsFromFixed(v));
    }
};

// A text as the editor stores it: Windows-1252, ending in a NUL when it has
// words and `terminated` (each field's own habit), and when empty either
// nothing or a lone NUL.
std::string Stored(const std::string& text, bool terminated, bool nul_when_empty) {
    if (text.empty()) return nul_when_empty ? std::string(1, '\0') : std::string();
    std::string s = Cp1252(text);
    if (terminated) s.push_back('\0');
    return s;
}
void Str16(Out& out, const std::string& stored) {
    out.U16(static_cast<uint16_t>(std::min<size_t>(stored.size(), 0xFFFFu)));
    out.Raw(stored.substr(0, 0xFFFFu));
}
void Str32(Out& out, const std::string& stored) {
    out.I32(static_cast<int32_t>(stored.size()));
    out.Raw(stored);
}
// The texts that never end in a NUL: file names, cinematics, the AI's names.
void Bare16(Out& out, const std::string& text) { Str16(out, Stored(text, false, false)); }

// A 256-byte name field: as written while it still says the same thing (the
// editor leaves its buffer's old bytes past the NUL), else the name and
// zeros.
std::string NameField(const std::string& written, const std::string& name) {
    if (written.size() == 256 && Utf8(written) == name) return written;
    std::string s = Cp1252(name).substr(0, 255);
    s.resize(256, '\0');
    return s;
}

// A disable table's row: as written, its first `list.size()` replaced.
std::vector<int32_t> Row(const std::vector<int32_t>& written, const std::vector<int32_t>& list,
                         int slots) {
    std::vector<int32_t> row = written;
    row.resize(static_cast<size_t>(slots), -1);
    for (size_t i = 0; i < list.size() && i < row.size(); ++i) row[i] = list[i];
    return row;
}

// The editor's own second stance table, each player's toward Gaia and the
// eight: 0 Gaia, 1 itself, then 2 ally, 3 neutral, 4 enemy for the stance.
uint32_t EditorStance(const Player& player, int self, int toward) {
    if (toward == 0) return 0;
    if (toward == self) return 1;
    switch (player.stance[toward]) {
    case 0: return 2;
    case 1: return 3;
    default: return 4;
    }
}

void WritePlayerData(Out& out, const File& file) {
    const Kept& kept = file.kept;
    out.U32(file.next_unit_id);
    out.U32(kept.player_data_version_bits);
    for (int s = 0; s < kFileSlots; ++s) {
        const Kept::Slot& slot = kept.slots[s];
        if (s < kPlayers) {
            out.Raw(NameField(slot.name, file.players[s + 1].name));
        } else {
            std::string name = slot.name;
            name.resize(256, '\0');
            out.Raw(name);
        }
    }
    for (int s = 0; s < kFileSlots; ++s) {
        out.I32(s < kPlayers ? file.players[s + 1].name_string : kept.slots[s].name_string);
    }
    for (int s = 0; s < kFileSlots; ++s) {
        const Kept::Slot& slot = kept.slots[s];
        if (s < kPlayers) {
            const Player& p = file.players[s + 1];
            out.U32(p.active ? 1u : 0u);
            out.U32(p.human ? 1u : 0u);
            out.U32(static_cast<uint32_t>(p.civ));
        } else {
            out.U32(slot.enabled);
            out.U32(slot.human);
            out.U32(slot.civ);
        }
        out.U32(slot.unknown);
    }
    out.U8(kept.conquest);
    out.U16(static_cast<uint16_t>(kept.timeline_events.size() / 28u));
    out.U16(kept.timeline_next);
    out.U32(kept.timeline_time);
    out.Raw(kept.timeline_events);

    Bare16(out, file.original_name);
    for (int t = kInstructions; t <= kHistory; ++t) out.I32(file.texts[t].string_id);
    for (int t = kInstructions; t <= kHistory; ++t) {
        Str16(out, Stored(file.texts[t].text, true, file.texts[t].nul_when_empty));
    }
    Bare16(out, file.pregame_cinematic);
    Bare16(out, file.victory_cinematic);
    Bare16(out, file.loss_cinematic);
    Bare16(out, file.background);

    out.U32(kept.bitmap_included);
    out.U32(kept.bitmap_width);
    out.U32(kept.bitmap_height);
    out.I16(kept.bitmap_unknown);
    if (kept.bitmap_included != 0) out.Raw(kept.bitmap_bytes);

    for (int s = 0; s < kFileSlots; ++s) {
        Bare16(out, s < kPlayers ? file.players[s + 1].ai_name : kept.slots[s].ai_name);
    }
    for (int s = 0; s < kFileSlots; ++s) Bare16(out, kept.slots[s].city_name);
    for (int s = 0; s < kFileSlots; ++s) Bare16(out, kept.slots[s].personality_name);
    for (int s = 0; s < kFileSlots; ++s) {
        const Kept::Slot& slot = kept.slots[s];
        const std::string& script = s < kPlayers ? file.players[s + 1].ai_script : slot.ai_script;
        out.U32(static_cast<uint32_t>(slot.ai_file.size()));
        out.U32(static_cast<uint32_t>(slot.city_file.size()));
        out.U32(static_cast<uint32_t>(script.size()));
        out.Raw(slot.ai_file);
        out.Raw(slot.city_file);
        out.Raw(script);
    }
    for (int s = 0; s < kFileSlots; ++s) {
        out.U8(s < kPlayers ? file.players[s + 1].ai_type : kept.slots[s].ai_type);
    }
    out.U32(kSeparator);

    for (int s = 0; s < kFileSlots; ++s) {
        const uint32_t* written = kept.slots[s].stockpile;   // gold, wood, food, stone, ore, goods
        if (s < kPlayers) {
            const Player& p = file.players[s + 1];
            out.U32(static_cast<uint32_t>(p.gold));
            out.U32(static_cast<uint32_t>(p.wood));
            out.U32(static_cast<uint32_t>(p.food));
            out.U32(static_cast<uint32_t>(p.stone));
        } else {
            for (int r = 0; r < 4; ++r) out.U32(written[r]);
        }
        out.U32(written[4]);
        out.U32(written[5]);
    }
    out.U32(kSeparator);

    out.U32(file.victory.conquest ? 1u : 0u);
    out.U32(kept.victory_unknown[0]);
    out.I32(file.victory.relics);
    out.U32(kept.victory_unknown[1]);
    out.I32(file.victory.explored);
    out.U32(kept.victory_unknown[2]);
    out.U32(file.victory.all ? 1u : 0u);
    out.I32(file.victory.mode);
    out.I32(file.victory.score);
    out.I32(file.victory.time);

    // The 16-by-16 stances: players 1-8 toward each other as their own
    // blocks say, 3 everywhere else.
    for (int s = 0; s < kFileSlots; ++s) {
        for (int t = 0; t < kFileSlots; ++t) {
            const bool played = s < kPlayers && t < kPlayers && s != t;
            out.U32(played ? file.players[s + 1].stance[t + 1] : 3u);
        }
    }
    for (int s = 0; s < kFileSlots; ++s) {
        std::string block = kept.slots[s].old_victory;
        block.resize(180u * 4u, '\0');
        out.Raw(block);
    }
    out.U32(kSeparator);
    for (int s = 0; s < kFileSlots; ++s) {
        out.U32(s < kPlayers ? (file.players[s + 1].allied_victory ? 1u : 0u)
                             : kept.slots[s].allied_victory);
    }

    // The disables: counts, then the tables whole.
    auto table = [&](int which, int slots, std::vector<int32_t> Kept::Slot::*written,
                     std::vector<int32_t> Player::*list) {
        for (int s = 0; s < kFileSlots; ++s) {
            uint32_t count = kept.slots[s].disabled[which];
            if (s < kPlayers) {
                count = static_cast<uint32_t>(
                    std::min<size_t>((file.players[s + 1].*list).size(), static_cast<size_t>(slots)));
            }
            out.U32(count);
        }
        for (int s = 0; s < kFileSlots; ++s) {
            static const std::vector<int32_t> kNone;
            const std::vector<int32_t>& now = s < kPlayers ? file.players[s + 1].*list : kNone;
            for (int32_t id : Row(kept.slots[s].*written, now, slots)) out.I32(id);
        }
    };
    table(0, kTechSlots, &Kept::Slot::techs, &Player::disabled_techs);
    table(1, kUnitSlots, &Kept::Slot::units, &Player::disabled_units);
    table(2, kBuildingSlots, &Kept::Slot::buildings, &Player::disabled_buildings);
    out.U32(kept.after_disables[0]);
    out.U32(kept.after_disables[1]);
    out.U32(file.all_techs ? 1u : 0u);
    for (int s = 0; s < kFileSlots; ++s) {
        out.I32(s < kPlayers ? file.players[s + 1].starting_age : kept.slots[s].starting_age);
    }
    out.U32(kSeparator);
    out.I32(file.camera_x);
    out.I32(file.camera_y);
}

void WriteMap(Out& out, const File& file) {
    out.U32(static_cast<uint32_t>(file.width));
    out.U32(static_cast<uint32_t>(file.height));
    const size_t tiles = static_cast<size_t>(file.width) * static_cast<size_t>(file.height);
    for (size_t i = 0; i < tiles; ++i) {
        out.U8(i < file.terrain.size() ? file.terrain[i] : 0);
        out.U8(i < file.elevation.size() ? file.elevation[i] : 0);
        out.U8(0);
    }
}

void WriteUnits(Out& out, const File& file) {
    out.U32(file.kept.unit_sets);
    for (int p = 1; p <= kPlayers; ++p) {
        const Player& player = file.players[p];
        out.Float(Core::Fixed32(player.food));
        out.Float(Core::Fixed32(player.wood));
        out.Float(Core::Fixed32(player.gold));
        out.Float(Core::Fixed32(player.stone));
        out.U32(file.kept.slots[p - 1].ore_goods_bits[0]);
        out.U32(file.kept.slots[p - 1].ore_goods_bits[1]);
    }
    for (uint32_t set = 0; set < file.kept.unit_sets; ++set) {
        static const std::vector<Unit> kNone;
        const std::vector<Unit>& units =
            set < static_cast<uint32_t>(Core::kMaxPlayers) ? file.players[set].units : kNone;
        out.U32(static_cast<uint32_t>(units.size()));
        for (const Unit& unit : units) {
            out.Float(unit.x, unit.written[0]);
            out.Float(unit.y, unit.written[1]);
            out.Float(unit.z, unit.written[2]);
            out.U32(static_cast<uint32_t>(unit.id));
            out.U16(static_cast<uint16_t>(unit.unit_id));
            out.U8(unit.state);
            out.Float(unit.rotation, unit.written[3]);
            out.U16(static_cast<uint16_t>(unit.frame));
            // Held by nothing is 0 in 1.18 (the reader makes it -1).
            out.I32(unit.garrisoned_in < 0 ? 0 : unit.garrisoned_in);
        }
    }
}

void WritePlayerSettings(Out& out, const File& file) {
    out.U32(file.kept.settings_count);
    for (int p = 1; p <= kPlayers; ++p) {
        const Player& player = file.players[p];
        const Kept::Settings& kept = file.kept.settings[p - 1];
        std::string label = kept.label;
        if (label.empty()) label = "Player " + std::to_string(p) + std::string(1, '\0');
        Str16(out, label);
        out.Float(player.camera_x);
        out.Float(player.camera_y);
        out.I16(kept.view[0]);
        out.I16(kept.view[1]);
        out.U8(player.allied_victory ? 1 : 0);
        out.U16(static_cast<uint16_t>(Core::kMaxPlayers));
        for (int q = 0; q < Core::kMaxPlayers; ++q) out.U8(player.stance[q]);
        for (int q = 0; q < Core::kMaxPlayers; ++q) out.U32(EditorStance(player, p, q));
        out.I32(player.colour);
        if (!kept.victory.empty()) {
            out.Raw(kept.victory);
        } else {
            // A new player's: version 1.0, no conditions, no points.
            out.U32(0x3F800000u);
            out.U32(0);
            out.U8(0);
            out.U32(0);
            out.U32(0);
        }
    }
}

// An order list that is the count long, or the identity.
void Order(Out& out, const std::vector<int32_t>& order, size_t count) {
    if (order.size() == count) {
        for (int32_t i : order) out.I32(i);
    } else {
        for (size_t i = 0; i < count; ++i) out.I32(static_cast<int32_t>(i));
    }
}

void WriteTriggers(Out& out, const File& file) {
    out.U64(file.kept.trigger_version_bits);
    out.U8(static_cast<uint8_t>(file.objectives_state));
    out.U32(static_cast<uint32_t>(file.triggers.size()));
    for (const Trigger& t : file.triggers) {
        out.I32(t.enabled ? 1 : 0);
        out.U8(t.looping ? 1 : 0);
        out.I32(t.string_id);
        out.U8(t.objective ? 1 : 0);
        out.I32(t.objective_order);
        out.I32(t.start_time);
        // Always NUL-ended, even empty.
        Str32(out, Stored(t.description, true, true));
        Str32(out, Stored(t.name, true, true));

        out.I32(static_cast<int32_t>(t.effects.size()));
        for (const Effect& e : t.effects) {
            const int32_t selected =
                e.units.empty() ? e.selected_count : static_cast<int32_t>(e.units.size());
            const int32_t fields[kEffectFields] = {
                e.ai_goal,    e.amount,     e.resource,     e.diplomacy,  selected,
                e.location_unit, e.object,  e.player,       e.target_player, e.technology,
                e.string_id,  e.sound_id,   e.display_time, e.trigger,    e.location.x,
                e.location.y, e.area.x0,    e.area.y0,      e.area.x1,    e.area.y1,
                e.group,      e.kind,       e.panel};
            out.I32(e.type);
            out.I32(kEffectFields);
            for (int32_t v : fields) out.I32(v);
            Str32(out, Stored(e.message, true, e.message_nul_when_empty));
            Str32(out, Stored(e.sound, true, e.sound_nul_when_empty));
            for (int32_t i = 0; i < selected; ++i) {
                out.I32(static_cast<size_t>(i) < e.units.size() ? e.units[static_cast<size_t>(i)] : -1);
            }
        }
        Order(out, t.effect_order, t.effects.size());

        out.I32(static_cast<int32_t>(t.conditions.size()));
        for (const Condition& c : t.conditions) {
            const int32_t fields[kConditionFields] = {
                c.amount,   c.resource, c.unit,     c.next_unit, c.object,  c.player,
                c.technology, c.timer,  c.trigger,  c.area.x0,   c.area.y0, c.area.x1,
                c.area.y1,  c.group,    c.kind,     c.ai_signal};
            out.I32(c.type);
            out.I32(kConditionFields);
            for (int32_t v : fields) out.I32(v);
        }
        Order(out, t.condition_order, t.conditions.size());
    }
    Order(out, file.trigger_order, file.triggers.size());

    out.U32(file.kept.include_ai);
    out.U32(file.kept.ai_errors);
    if (file.kept.ai_errors != 0) {
        std::string table = file.kept.ai_error_table;
        table.resize(99u * 4u, '\0');
        out.Raw(table);
    }
    if (file.kept.include_ai != 0) {
        if (file.kept.ai_files.empty()) out.U32(0);
        else out.Raw(file.kept.ai_files);
    }
}

} // namespace

std::vector<uint8_t> Write(const File& file) {
    Out body;
    WritePlayerData(body, file);
    WriteMap(body, file);
    WriteUnits(body, file);
    WritePlayerSettings(body, file);
    WriteTriggers(body, file);

    Out header;
    header.I32(file.kept.save_type);
    std::string version = file.version.size() == 4 ? file.version : std::string("1.18");
    if (version[2] >= '1') header.U32(file.save_time);
    Str32(header, Stored(file.header_instructions, true, file.kept.header_nul_when_empty));
    header.I32(file.kept.victory_type);
    header.I32(file.enabled_players);

    Out out;
    out.Raw(version);
    out.U32(static_cast<uint32_t>(header.bytes.size()));
    out.bytes.insert(out.bytes.end(), header.bytes.begin(), header.bytes.end());
    const std::vector<uint8_t> packed = Deflate(body.bytes.data(), body.bytes.size());
    out.bytes.insert(out.bytes.end(), packed.begin(), packed.end());
    return std::move(out.bytes);
}

bool Save(const std::string& path, const File& file, std::string* error) {
    const std::vector<uint8_t> bytes = Write(file);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        if (error != nullptr) *error = "cannot open the file to write";
        return false;
    }
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!out) {
        if (error != nullptr) *error = "cannot write the file";
        return false;
    }
    return true;
}

bool Expand(const std::vector<uint8_t>& bytes, std::vector<uint8_t>& out, std::string* error) {
    auto fail = [&](const char* why) {
        if (error != nullptr) *error = why;
        return false;
    };
    if (bytes.size() < 8) return fail("not a scenario");
    const uint32_t header = static_cast<uint32_t>(bytes[4]) | (static_cast<uint32_t>(bytes[5]) << 8) |
                            (static_cast<uint32_t>(bytes[6]) << 16) |
                            (static_cast<uint32_t>(bytes[7]) << 24);
    const size_t packed_at = 8u + static_cast<size_t>(header);
    if (packed_at > bytes.size()) return fail("the header is cut short");
    int size = 0;
    char* inflated = stbi_zlib_decode_noheader_malloc(reinterpret_cast<const char*>(bytes.data() + packed_at),
                                                      static_cast<int>(bytes.size() - packed_at), &size);
    if (inflated == nullptr || size < 0) {
        std::free(inflated);
        return fail("the scenario's data does not inflate");
    }
    out.assign(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(packed_at));
    out.insert(out.end(), reinterpret_cast<uint8_t*>(inflated), reinterpret_cast<uint8_t*>(inflated) + size);
    std::free(inflated);
    return true;
}

} // namespace Scenario
