#include "scenario/Scenario.h"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>

#include "core/Strings.h"
#include "hal/stb/stb_image.h"
#include "scenario/Campaign.h"
#include "scenario/Encoding.h"

namespace Scenario {

namespace Encoding {

// The file's text is Windows-1252, the interface's UTF-8 (LANGUAGE.DLL's
// strings are extracted to UTF-8 too). Trailing NULs -- the editor writes
// one after most strings -- are dropped.
std::string Utf8(const std::string& cp1252) {
    // 0x80-0x9F: Windows-1252's own; the rest of the upper half is Latin-1.
    static const uint16_t kHigh[32] = {
        0x20AC, 0x0081, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160,
        0x2039, 0x0152, 0x008D, 0x017D, 0x008F, 0x0090, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022,
        0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x009D, 0x017E, 0x0178};
    size_t end = cp1252.size();
    while (end > 0 && cp1252[end - 1] == '\0') --end;
    std::string out;
    out.reserve(end);
    for (size_t i = 0; i < end; ++i) {
        const auto c = static_cast<uint8_t>(cp1252[i]);
        if (c == 0) break;
        uint32_t code = c;
        if (c >= 0x80 && c < 0xA0) code = kHigh[c - 0x80];
        if (code < 0x80) {
            out += static_cast<char>(code);
        } else if (code < 0x800) {
            out += static_cast<char>(0xC0 | (code >> 6));
            out += static_cast<char>(0x80 | (code & 0x3F));
        } else {
            out += static_cast<char>(0xE0 | (code >> 12));
            out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (code & 0x3F));
        }
    }
    return out;
}

// A float of the file as fixed point, exactly, from its bits: no float
// arithmetic, so every machine gets the same value. A value below 2^-32 is
// zero; nothing in a scenario is that small or that large.
Core::Fixed32 FixedFromBits(uint32_t bits) {
    const bool negative = (bits >> 31) != 0;
    const int exponent = static_cast<int>((bits >> 23) & 0xFFu);
    if (exponent == 0 || exponent == 0xFF) return Core::Fixed32(0);
    const int64_t mantissa = static_cast<int64_t>((bits & 0x7FFFFFu) | 0x800000u);
    // value = mantissa * 2^(exponent - 150); raw 32.32 = value * 2^32.
    const int shift = exponent - 150 + Core::Fixed32::FRACTIONAL_BITS;
    int64_t raw = 0;
    if (shift >= 39) {
        raw = INT64_MAX / 2;
    } else if (shift >= 0) {
        raw = mantissa << shift;
    } else if (shift > -25) {
        raw = mantissa >> (-shift);
    }
    return Core::Fixed32(negative ? -raw : raw, true);
}

} // namespace Encoding

namespace {

using Encoding::FixedFromBits;
using Encoding::Utf8;

// Between the sections of the player data.
constexpr uint32_t kSeparator = 0xFFFFFF9Du;

// The bytes of a file, read in order; a read past the end marks it bad and
// returns zeros, so a damaged file fails once, at the end, rather than at
// every field.
struct Bytes {
    const uint8_t* data = nullptr;
    size_t size = 0;
    size_t at = 0;
    bool ok = true;
    const char* why = nullptr;

    bool Have(size_t n) {
        if (!ok || at + n > size || at + n < at) {
            Fail("the file is cut short");
            return false;
        }
        return true;
    }
    void Fail(const char* reason) {
        if (ok) why = reason;
        ok = false;
    }
    uint8_t U8() { return Have(1) ? data[at++] : 0; }
    int8_t I8() { return static_cast<int8_t>(U8()); }
    uint16_t U16() {
        if (!Have(2)) return 0;
        const uint16_t v = static_cast<uint16_t>(data[at] | (data[at + 1] << 8));
        at += 2;
        return v;
    }
    int16_t I16() { return static_cast<int16_t>(U16()); }
    uint32_t U32() {
        if (!Have(4)) return 0;
        const uint32_t v = static_cast<uint32_t>(data[at]) | (static_cast<uint32_t>(data[at + 1]) << 8) |
                           (static_cast<uint32_t>(data[at + 2]) << 16) |
                           (static_cast<uint32_t>(data[at + 3]) << 24);
        at += 4;
        return v;
    }
    int32_t I32() { return static_cast<int32_t>(U32()); }
    uint64_t U64() {
        const uint64_t lo = U32();
        return lo | (static_cast<uint64_t>(U32()) << 32);
    }
    void Skip(size_t n) {
        if (Have(n)) at += n;
    }
    std::string Raw(size_t n) {
        if (!Have(n)) return {};
        std::string s(reinterpret_cast<const char*>(data + at), n);
        at += n;
        return s;
    }
};

// A text of the file. `nul_when_empty`, when given, says whether an empty
// one was written as a lone NUL (the editor wrote both forms).
std::string Str16(Bytes& in, bool* nul_when_empty = nullptr) {
    const std::string raw = in.Raw(in.U16());
    if (nul_when_empty != nullptr) *nul_when_empty = !raw.empty();
    return Utf8(raw);
}
std::string Str32(Bytes& in, bool* nul_when_empty = nullptr) {
    const int32_t n = in.I32();
    if (n < 0) {
        in.Fail("a string of negative length");
        return {};
    }
    const std::string raw = in.Raw(static_cast<size_t>(n));
    if (nul_when_empty != nullptr) *nul_when_empty = !raw.empty();
    return Utf8(raw);
}

// A version number stored as a float or double, in hundredths or tenths,
// rounded. Read once at load; nothing the simulation computes.
int32_t Scaled(uint32_t float_bits, int scale) {
    float f = 0.0f;
    std::memcpy(&f, &float_bits, sizeof(f));
    return static_cast<int32_t>(std::lround(static_cast<double>(f) * scale));
}
int32_t ScaledDouble(uint64_t double_bits, int scale) {
    double d = 0.0;
    std::memcpy(&d, &double_bits, sizeof(d));
    return static_cast<int32_t>(std::lround(d * scale));
}

// The format's internal version, from the file's own, in thousandths: 1.135
// is 1135.
int InternalVersion(const std::string& version) {
    if (version == "1.22") return 1160;
    if (version == "1.20" || version == "1.21") return 1140;
    if (version == "1.19") return 1135;
    if (version == "1.17" || version == "1.18") return 1130;
    if (version == "1.14" || version == "1.15" || version == "1.16") return 1120;
    return 0;
}

bool ReadPlayerData(Bytes& in, File& file) {
    const int pv = file.player_data_version;   // hundredths
    Kept& kept = file.kept;
    std::string names[kFileSlots];
    if (pv > 113) {
        for (int s = 0; s < kFileSlots; ++s) {
            kept.slots[s].name = in.Raw(256);
            names[s] = Utf8(kept.slots[s].name);
        }
        if (pv > 115) {
            for (int s = 0; s < kFileSlots; ++s) {
                const int32_t id = in.I32();
                if (s < kPlayers) file.players[s + 1].name_string = id;
                else kept.slots[s].name_string = id;
            }
        }
        for (int s = 0; s < kFileSlots; ++s) {
            const uint32_t enabled = in.U32();
            const uint32_t human = in.U32();
            const uint32_t civ = in.U32();
            kept.slots[s].unknown = in.U32();   // unnamed; 4 in every known file
            if (s < kPlayers) {
                Player& p = file.players[s + 1];
                p.active = enabled != 0;
                p.human = human != 0;
                p.civ = static_cast<int32_t>(civ);
                p.name = names[s];
            } else {
                kept.slots[s].enabled = enabled;
                kept.slots[s].human = human;
                kept.slots[s].civ = civ;
            }
        }
    } else {
        in.Fail("player data older than 1.14 (Age of Empires) is not read");
        return false;
    }
    if (pv > 106) kept.conquest = in.U8();

    // The timeline: the editor's recorded events, 28 bytes each.
    const uint16_t events = in.U16();
    kept.timeline_next = in.U16();
    kept.timeline_time = in.U32();
    kept.timeline_events = in.Raw(static_cast<size_t>(events) * 28u);

    file.original_name = Str16(in);
    if (pv > 115) {
        for (int t = kInstructions; t <= kHistory; ++t) file.texts[t].string_id = in.I32();
    }
    if (pv > 121) file.texts[kScouts].string_id = in.I32();
    file.texts[kInstructions].text = Str16(in, &file.texts[kInstructions].nul_when_empty);
    if (pv > 110) {
        for (int t = kHints; t <= kHistory; ++t) {
            file.texts[t].text = Str16(in, &file.texts[t].nul_when_empty);
        }
    }
    if (pv > 121) file.texts[kScouts].text = Str16(in, &file.texts[kScouts].nul_when_empty);
    for (Text& text : file.texts) {
        if (!text.text.empty()) text.nul_when_empty = false;
    }
    file.pregame_cinematic = Str16(in);
    file.victory_cinematic = Str16(in);
    file.loss_cinematic = Str16(in);
    if (pv > 108) file.background = Str16(in);

    if (pv > 107) {
        const uint32_t included = in.U32();
        const uint32_t width = in.U32();
        const uint32_t height = in.U32();
        kept.bitmap_included = included;
        kept.bitmap_width = width;
        kept.bitmap_height = height;
        kept.bitmap_unknown = in.I16();
        const size_t bitmap_at = in.at;
        if (included != 0) {
            // A BITMAPINFOHEADER, the palette, the pixels.
            const uint32_t header = in.U32();
            const int32_t w = in.I32();
            const int32_t h = in.I32();
            in.U16();                       // planes
            const uint16_t bpp = in.U16();
            in.U32();                       // compression
            const uint32_t raw_size = in.U32();
            in.I32();
            in.I32();
            const uint32_t used = in.U32();
            in.U32();
            if (header != 40 || bpp != 8) {
                in.Fail("an instructions map of an unsupported format");
                return false;
            }
            const uint32_t colours = used != 0 ? used : 256u;
            file.bitmap.width = w != 0 ? w : static_cast<int32_t>(width);
            file.bitmap.height = h != 0 ? (h < 0 ? -h : h) : static_cast<int32_t>(height);
            for (uint32_t i = 0; i < colours && in.ok; ++i) {
                const uint32_t quad = in.U32();   // blue, green, red, unused
                file.bitmap.palette.push_back(quad & 0x00FFFFFFu);
            }
            const uint32_t bytes = raw_size != 0 ? raw_size
                                                 : static_cast<uint32_t>(file.bitmap.width * file.bitmap.height);
            const std::string pixels = in.Raw(bytes);
            file.bitmap.pixels.assign(pixels.begin(), pixels.end());
            if (in.ok) {
                kept.bitmap_bytes.assign(reinterpret_cast<const char*>(in.data + bitmap_at),
                                         in.at - bitmap_at);
            }
        }
    }

    std::string ai_names[kFileSlots];
    for (std::string& name : ai_names) name = Str16(in);
    for (int s = 0; s < kFileSlots; ++s) kept.slots[s].city_name = Str16(in);
    if (pv > 107) {
        for (int s = 0; s < kFileSlots; ++s) kept.slots[s].personality_name = Str16(in);
    }
    for (int s = 0; s < kFileSlots; ++s) {
        const uint32_t ai_size = in.U32();
        const uint32_t city_size = in.U32();
        const uint32_t per_size = pv > 107 ? in.U32() : 0;
        kept.slots[s].ai_file = in.Raw(ai_size);
        kept.slots[s].city_file = in.Raw(city_size);
        const std::string script = in.Raw(per_size);
        if (s < kPlayers) {
            file.players[s + 1].ai_name = ai_names[s];
            file.players[s + 1].ai_script = script;
        } else {
            kept.slots[s].ai_name = ai_names[s];
            kept.slots[s].ai_script = script;
        }
    }
    if (pv > 110 && pv != 114) {
        for (int s = 0; s < kFileSlots; ++s) {
            const uint8_t type = in.U8();
            if (s < kPlayers) file.players[s + 1].ai_type = type;
            else kept.slots[s].ai_type = type;
        }
    }
    if (in.U32() != kSeparator) {
        in.Fail("no separator before the stockpiles");
        return false;
    }

    // The stockpiles as the editor's Players screen keeps them. The same
    // figures come again, as floats, after the map; the game plays those.
    for (int s = 0; s < kFileSlots; ++s) {
        uint32_t* stockpile = kept.slots[s].stockpile;   // gold, wood, food, stone, ore, goods
        for (int r = 0; r < 4; ++r) stockpile[r] = in.U32();
        if (pv > 116) {
            stockpile[4] = in.U32();
            stockpile[5] = in.U32();
        }
        if (pv > 123) in.U32();
    }
    if (in.U32() != kSeparator) {
        in.Fail("no separator before the victory conditions");
        return false;
    }

    file.victory.conquest = in.U32() != 0;
    kept.victory_unknown[0] = in.U32();
    file.victory.relics = in.I32();
    kept.victory_unknown[1] = in.U32();
    file.victory.explored = in.I32();
    kept.victory_unknown[2] = in.U32();
    file.victory.all = in.U32() != 0;
    if (pv > 112) {
        file.victory.mode = in.I32();
        file.victory.score = in.I32();
        file.victory.time = in.I32();
    }

    // The 16-by-16 stances of the player data, and the old per-player victory
    // conditions triggers replaced. The stances used are the per-player
    // copies after the units, which also say how each regards Gaia; this
    // table says the same of players 1-8 (each to itself 3, and every other
    // entry 3), so the writer makes it again from them.
    in.Skip(16u * 16u * 4u);
    for (int s = 0; s < kFileSlots; ++s) kept.slots[s].old_victory = in.Raw(180u * 4u);
    if (in.U32() != kSeparator) {
        in.Fail("no separator before the allied victories");
        return false;
    }
    for (int s = 0; s < kFileSlots; ++s) {
        const uint32_t allied = in.U32();
        if (s < kPlayers) file.players[s + 1].allied_victory = allied != 0;
        else kept.slots[s].allied_victory = allied;
    }
    if (pv > 122) in.U32();

    if (pv > 103) {
        uint32_t tech_count[kFileSlots] = {};
        if (pv > 117) {
            for (uint32_t& n : tech_count) n = in.U32();
        }
        const int tech_slots = pv <= 114 ? 20 : (pv < 130 ? 30 : 60);
        // Each table whole, as written; players 1-8 take what its count
        // covers, slots 9-16 keep their count.
        auto table = [&](const uint32_t (&count)[kFileSlots], int slots, int which,
                         std::vector<int32_t> Kept::Slot::*raw, std::vector<int32_t> Player::*list) {
            for (int s = 0; s < kFileSlots; ++s) {
                std::vector<int32_t>& written = kept.slots[s].*raw;
                written.resize(static_cast<size_t>(slots));
                for (int i = 0; i < slots; ++i) {
                    const int32_t id = in.I32();
                    written[static_cast<size_t>(i)] = id;
                    if (s < kPlayers && static_cast<uint32_t>(i) < count[s]) {
                        (file.players[s + 1].*list).push_back(id);
                    }
                }
                if (s >= kPlayers) kept.slots[s].disabled[which] = count[s];
            }
        };
        table(tech_count, tech_slots, 0, &Kept::Slot::techs, &Player::disabled_techs);
        if (pv > 117) {
            uint32_t unit_count[kFileSlots] = {};
            for (uint32_t& n : unit_count) n = in.U32();
            table(unit_count, pv < 130 ? 30 : 60, 1, &Kept::Slot::units, &Player::disabled_units);
            uint32_t building_count[kFileSlots] = {};
            for (uint32_t& n : building_count) n = in.U32();
            table(building_count, pv < 130 ? 20 : 60, 2, &Kept::Slot::buildings,
                  &Player::disabled_buildings);
        }
    }
    if (pv > 104) kept.after_disables[0] = in.U32();
    if (pv > 111) {
        kept.after_disables[1] = in.U32();
        file.all_techs = in.U32() != 0;
    }
    if (pv > 105) {
        for (int s = 0; s < kFileSlots; ++s) {
            const int32_t age = in.I32();
            if (s < kPlayers) file.players[s + 1].starting_age = age;
            else kept.slots[s].starting_age = age;
        }
    }
    if (in.U32() != kSeparator) {
        in.Fail("no separator before the view");
        return false;
    }
    if (pv > 118) {
        file.camera_x = in.I32();
        file.camera_y = in.I32();
    }
    if (pv > 120) in.I32();
    if (pv > 123) in.Skip(16);
    return in.ok;
}

bool ReadUnits(Bytes& in, File& file, int internal) {
    const uint32_t sets = in.U32();
    file.kept.unit_sets = sets;
    if (sets > static_cast<uint32_t>(Core::kMaxPlayers) + 8u) {
        in.Fail("an impossible number of players' units");
        return false;
    }
    // The stockpiles the game plays: floats, whole numbers in every file.
    for (int p = 1; p <= kPlayers; ++p) {
        Player& player = file.players[p];
        player.food = FixedFromBits(in.U32()).ToInt();
        player.wood = FixedFromBits(in.U32()).ToInt();
        player.gold = FixedFromBits(in.U32()).ToInt();
        player.stone = FixedFromBits(in.U32()).ToInt();
        uint32_t* ore_goods = file.kept.slots[p - 1].ore_goods_bits;
        if (internal > 1120) ore_goods[0] = in.U32();   // ore
        if (internal < 1150) ore_goods[1] = in.U32();   // goods
        if (internal >= 1140) in.U32();                 // population limit
        if (internal > 1130 && (internal < 1140 || internal > 1150)) in.U32();
    }
    const bool frames = file.version != "1.14";
    for (uint32_t set = 0; set < sets && in.ok; ++set) {
        const uint32_t count = in.U32();
        if (count > 100000u) {
            in.Fail("an impossible number of units");
            return false;
        }
        std::vector<Unit> units(count);
        for (Unit& unit : units) {
            unit.written[0] = in.U32();
            unit.written[1] = in.U32();
            unit.written[2] = in.U32();
            unit.x = FixedFromBits(unit.written[0]);
            unit.y = FixedFromBits(unit.written[1]);
            unit.z = FixedFromBits(unit.written[2]);
            unit.id = static_cast<int32_t>(in.U32());
            unit.unit_id = in.U16();
            unit.state = in.U8();
            unit.written[3] = in.U32();
            unit.rotation = FixedFromBits(unit.written[3]);
            unit.frame = frames ? in.U16() : 0;
            unit.garrisoned_in = in.I32();
            // Before 1.21 a unit held by nothing says 0.
            if (unit.garrisoned_in == 0 && internal <= 1140 && file.version != "1.21") {
                unit.garrisoned_in = -1;
            }
        }
        if (set < static_cast<uint32_t>(Core::kMaxPlayers)) file.players[set].units = std::move(units);
    }
    return in.ok;
}

bool ReadPlayerSettings(Bytes& in, File& file, int internal) {
    // 9: Gaia and eight players, of which the eight follow.
    file.kept.settings_count = in.U32();
    for (int p = 1; p <= kPlayers && in.ok; ++p) {
        Player& player = file.players[p];
        Kept::Settings& kept = file.kept.settings[p - 1];
        // "Player 1": the editor's label, not the player's name.
        kept.label = in.Raw(in.U16());
        player.camera_x = FixedFromBits(in.U32());
        player.camera_y = FixedFromBits(in.U32());
        kept.view[0] = in.I16();
        kept.view[1] = in.I16();
        in.U8();      // allied victory again, as the editor's checkbox
        const uint16_t players = in.U16();
        if (players != Core::kMaxPlayers) {
            in.Fail("a diplomacy table not of Gaia and eight players");
            return false;
        }
        for (uint16_t q = 0; q < players; ++q) player.stance[q] = in.U8();
        // The editor's own copy, which the writer makes again from these.
        if (internal >= 1080) in.Skip(static_cast<size_t>(players) * 4u);
        player.colour = in.I32();
        const size_t victory_at = in.at;
        const int32_t conditions_version = Scaled(in.U32(), 10);
        const uint32_t conditions = in.U32();
        in.U8();
        if (conditions > 100) {
            in.Fail("an impossible number of victory conditions");
            return false;
        }
        in.Skip(static_cast<size_t>(conditions) * 44u);
        if (conditions_version >= 10) {
            in.U32();
            in.U32();
        }
        if (conditions_version > 10) {
            in.I32();
            in.I32();
        }
        if (internal > 1130 && (internal < 1140 || internal > 1150)) in.I32();
        if (in.ok) {
            kept.victory.assign(reinterpret_cast<const char*>(in.data + victory_at), in.at - victory_at);
        }
    }
    return in.ok;
}

std::vector<int32_t> Ints(Bytes& in, int32_t n) {
    std::vector<int32_t> out;
    if (n < 0 || n > 100000) {
        in.Fail("an impossible list");
        return out;
    }
    out.resize(static_cast<size_t>(n));
    for (int32_t& v : out) v = in.I32();
    return out;
}

bool ReadTriggers(Bytes& in, File& file) {
    file.kept.trigger_version_bits = in.U64();
    file.trigger_version = ScaledDouble(file.kept.trigger_version_bits, 10);
    const int tv = file.trigger_version;   // tenths
    if (tv > 14) file.objectives_state = in.I8();
    const uint32_t count = in.U32();
    if (count > 10000u) {
        in.Fail("an impossible number of triggers");
        return false;
    }
    file.triggers.resize(count);
    for (Trigger& t : file.triggers) {
        t.enabled = in.I32() != 0;
        t.looping = in.I8() != 0;
        t.string_id = in.I32();
        t.objective = in.I8() != 0;
        t.objective_order = in.I32();
        if (tv > 15) t.start_time = in.I32();
        t.description = Str32(in);
        t.name = Str32(in);

        const int32_t effects = in.I32();
        if (effects < 0 || effects > 10000) {
            in.Fail("an impossible number of effects");
            return false;
        }
        t.effects.resize(static_cast<size_t>(effects));
        for (Effect& e : t.effects) {
            e.type = in.I32();
            const int32_t used = tv > 10 ? in.I32() : 16;
            int32_t* fields[] = {&e.ai_goal, &e.amount, &e.resource, &e.diplomacy, &e.selected_count,
                                 &e.location_unit, &e.object, &e.player, &e.target_player,
                                 &e.technology, &e.string_id, &e.sound_id, &e.display_time,
                                 &e.trigger, &e.location.x, &e.location.y, &e.area.x0, &e.area.y0,
                                 &e.area.x1, &e.area.y1, &e.group, &e.kind, &e.panel};
            constexpr int32_t kFields = static_cast<int32_t>(sizeof(fields) / sizeof(fields[0]));
            if (used < 0 || used > 64) {
                in.Fail("an effect with an impossible field count");
                return false;
            }
            for (int32_t i = 0; i < used; ++i) {
                const int32_t v = in.I32();
                if (i < kFields) *fields[i] = v;
            }
            e.message = Str32(in, &e.message_nul_when_empty);
            e.sound = Str32(in, &e.sound_nul_when_empty);
            if (!e.message.empty()) e.message_nul_when_empty = false;
            if (!e.sound.empty()) e.sound_nul_when_empty = false;
            if (tv > 11 && used >= 5 && e.selected_count > 0) e.units = Ints(in, e.selected_count);
        }
        if (tv > 12) t.effect_order = Ints(in, effects);

        const int32_t conditions = in.I32();
        if (conditions < 0 || conditions > 10000) {
            in.Fail("an impossible number of conditions");
            return false;
        }
        t.conditions.resize(static_cast<size_t>(conditions));
        for (Condition& c : t.conditions) {
            c.type = in.I32();
            const int32_t used = tv > 10 ? in.I32() : 13;
            int32_t* fields[] = {&c.amount, &c.resource, &c.unit, &c.next_unit, &c.object, &c.player,
                                 &c.technology, &c.timer, &c.trigger, &c.area.x0, &c.area.y0,
                                 &c.area.x1, &c.area.y1, &c.group, &c.kind, &c.ai_signal};
            constexpr int32_t kFields = static_cast<int32_t>(sizeof(fields) / sizeof(fields[0]));
            if (used < 0 || used > 64) {
                in.Fail("a condition with an impossible field count");
                return false;
            }
            for (int32_t i = 0; i < used; ++i) {
                const int32_t v = in.I32();
                if (i < kFields) *fields[i] = v;
            }
        }
        if (tv > 12) t.condition_order = Ints(in, conditions);
        if (!in.ok) return false;
    }
    if (tv > 13) file.trigger_order = Ints(in, static_cast<int32_t>(count));
    return in.ok;
}

} // namespace

std::string Show(const std::string& text, int32_t string_id) {
    if (string_id >= 0) {
        const std::string& dll = Core::Strings::Get(string_id);
        if (!dll.empty()) return dll;
    }
    return text;
}

std::string Show(const Text& text) { return Show(text.text, text.string_id); }

const char* ConditionName(int type) {
    static const char* const kNames[] = {
        "None", "Bring Object to Area", "Bring Object to Object", "Own Objects",
        "Own Fewer Objects", "Objects in Area", "Destroy Object", "Capture Object",
        "Accumulate Attribute", "Research Technology", "Timer", "Object Selected", "AI Signal",
        "Player Defeated", "Object Has Target", "Object Visible", "Object Not Visible",
        "Researching Technology", "Units Garrisoned", "Difficulty Level"};
    return type >= 0 && type < static_cast<int>(sizeof(kNames) / sizeof(kNames[0])) ? kNames[type]
                                                                                    : "Unknown";
}

const char* EffectName(int type) {
    static const char* const kNames[] = {
        "None", "Change Diplomacy", "Research Technology", "Send Chat", "Play Sound",
        "Send Tribute", "Unlock Gate", "Lock Gate", "Activate Trigger", "Deactivate Trigger",
        "AI Script Goal", "Create Object", "Task Object", "Declare Victory", "Kill Object",
        "Remove Object", "Change View", "Unload", "Change Ownership", "Patrol",
        "Display Instructions", "Clear Instructions", "Freeze Unit", "Use Advanced Buttons"};
    return type >= 0 && type < static_cast<int>(sizeof(kNames) / sizeof(kNames[0])) ? kNames[type]
                                                                                    : "Unknown";
}

const Unit* File::FindUnit(int32_t id, int* player) const {
    for (int p = 0; p < Core::kMaxPlayers; ++p) {
        for (const Unit& unit : players[p].units) {
            if (unit.id == id) {
                if (player != nullptr) *player = p;
                return &unit;
            }
        }
    }
    return nullptr;
}

bool Read(const std::vector<uint8_t>& bytes, File& out, std::string* error) {
    out = File{};
    auto fail = [&](const char* why) {
        if (error != nullptr) *error = why;
        return false;
    };
    if (bytes.size() < 12) return fail("not a scenario");
    out.version.assign(reinterpret_cast<const char*>(bytes.data()), 4);
    if (out.version[0] != '1' || out.version[1] != '.') return fail("not a scenario");
    const int internal = InternalVersion(out.version);
    if (internal == 0) return fail("a scenario version this reader does not know");

    // The plain header, which the game's lists read without inflating the rest.
    Bytes header{bytes.data(), bytes.size(), 4};
    const uint32_t header_size = header.U32();
    if (8u + static_cast<size_t>(header_size) > bytes.size()) return fail("the header is cut short");
    Bytes head{bytes.data() + 8, header_size, 0};
    out.kept.save_type = head.I32();   // 2
    if (out.version[2] >= '1') out.save_time = head.U32();
    out.header_instructions = Str32(head, &out.kept.header_nul_when_empty);
    if (!out.header_instructions.empty()) out.kept.header_nul_when_empty = false;
    out.kept.victory_type = head.I32();   // unused
    out.enabled_players = head.I32();

    // The rest, raw deflate.
    const size_t packed_at = 8u + header_size;
    int inflated_size = 0;
    char* inflated = stbi_zlib_decode_noheader_malloc(
        reinterpret_cast<const char*>(bytes.data() + packed_at),
        static_cast<int>(bytes.size() - packed_at), &inflated_size);
    if (inflated == nullptr || inflated_size <= 0) {
        std::free(inflated);
        return fail("the scenario's data does not inflate");
    }
    std::vector<uint8_t> data(reinterpret_cast<uint8_t*>(inflated),
                              reinterpret_cast<uint8_t*>(inflated) + inflated_size);
    std::free(inflated);

    Bytes in{data.data(), data.size(), 0};
    out.next_unit_id = in.U32();
    out.kept.player_data_version_bits = in.U32();
    out.player_data_version = Scaled(out.kept.player_data_version_bits, 100);
    if (!ReadPlayerData(in, out)) return fail(in.why != nullptr ? in.why : "damaged player data");

    out.width = static_cast<int32_t>(in.U32());
    out.height = static_cast<int32_t>(in.U32());
    if (out.width <= 0 || out.height <= 0 || out.width > 1024 || out.height > 1024) {
        return fail("an impossible map size");
    }
    const size_t tiles = static_cast<size_t>(out.width) * static_cast<size_t>(out.height);
    out.terrain.resize(tiles);
    out.elevation.resize(tiles);
    for (size_t i = 0; i < tiles && in.ok; ++i) {
        out.terrain[i] = in.U8();
        out.elevation[i] = in.U8();
        in.U8();
    }
    if (!ReadUnits(in, out, internal)) return fail(in.why != nullptr ? in.why : "damaged units");
    if (!ReadPlayerSettings(in, out, internal)) {
        return fail(in.why != nullptr ? in.why : "damaged player settings");
    }
    if (!ReadTriggers(in, out)) return fail(in.why != nullptr ? in.why : "damaged triggers");

    // The AI scripts and their error tables, carried by 1.18 and later.
    if (out.version >= "1.18") {
        const uint32_t include = in.U32();
        const uint32_t errors = in.U32();
        out.kept.include_ai = include;
        out.kept.ai_errors = errors;
        if (errors != 0) out.kept.ai_error_table = in.Raw(99u * 4u);
        if (include != 0) {
            const size_t files_at = in.at;
            const uint32_t files = in.U32();
            for (uint32_t f = 0; f < files && in.ok; ++f) {
                in.Skip(in.U32());
                in.Skip(in.U32());
            }
            if (in.ok) {
                out.kept.ai_files.assign(reinterpret_cast<const char*>(in.data + files_at),
                                         in.at - files_at);
            }
        }
    }
    if (!in.ok) return fail(in.why != nullptr ? in.why : "the file is cut short");
    if (in.at != in.size) return fail("bytes left over after the triggers");
    return true;
}

bool Load(const std::string& path, int entry, File& out, std::string* error) {
    std::vector<uint8_t> bytes;
    if (entry >= 0) {
        if (!ReadCampaignScenario(path, entry, bytes, error)) return false;
    } else {
        std::ifstream in(path, std::ios::binary);
        if (!in) {
            if (error != nullptr) *error = "cannot open the file";
            return false;
        }
        bytes.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }
    return Read(bytes, out, error);
}

void SetupFor(const File& file, Core::GameSetup& setup) {
    setup.map_size = file.width;
    setup.players[Core::PLAYER_GAIA] = Core::PlayerSetup{true, 0, 0};
    for (int p = 1; p <= kPlayers; ++p) {
        const Player& player = file.players[p];
        Core::PlayerSetup& seat = setup.players[p];
        seat = Core::PlayerSetup{};
        seat.active = player.active;
        if (!player.active) continue;
        seat.civ = player.civ;
        seat.team = 0;   // stances come from the file, not from teams
        seat.computer = !player.human;
        seat.name = Show(player.name, player.name_string);
        seat.colour = player.colour >= 0 && player.colour < kPlayers ? player.colour : -1;
        seat.starting_age = player.starting_age < 0 ? 0 : player.starting_age;
    }
    setup.starting_age = 0;
    setup.resources = Core::StartingResources::Standard;
    setup.victory = file.victory.mode == VictoryMode::Standard ? Core::VictoryKind::Standard
                                                               : Core::VictoryKind::Conquest;
    setup.regicide = false;
    setup.all_techs = file.all_techs;
    // The file has no Lock Teams; its stances are for its triggers to change.
    setup.lock_teams = false;
}

bool SetupFor(const std::string& path, int entry, Core::GameSetup& setup, std::string* error) {
    File file;
    if (!Load(path, entry, file, error)) return false;
    setup.scenario = path;
    setup.scenario_entry = entry;
    SetupFor(file, setup);
    return true;
}

} // namespace Scenario
