#include "net/Wire.h"

#include <type_traits>

namespace Net::Wire {

void Writer::U16(uint16_t v) {
    U8(static_cast<uint8_t>(v));
    U8(static_cast<uint8_t>(v >> 8));
}

void Writer::U32(uint32_t v) {
    for (int b = 0; b < 4; ++b) U8(static_cast<uint8_t>(v >> (8 * b)));
}

void Writer::U64(uint64_t v) {
    for (int b = 0; b < 8; ++b) U8(static_cast<uint8_t>(v >> (8 * b)));
}

void Writer::Text(const std::string& s) {
    U16(static_cast<uint16_t>(s.size()));
    bytes.insert(bytes.end(), s.begin(), s.end());
}

uint8_t Reader::U8() {
    if (at >= bytes.size()) {
        ok = false;
        return 0;
    }
    return bytes[at++];
}

uint16_t Reader::U16() {
    const uint16_t lo = U8();
    return static_cast<uint16_t>(lo | (uint16_t(U8()) << 8));
}

uint32_t Reader::U32() {
    uint32_t v = 0;
    for (int b = 0; b < 4; ++b) v |= uint32_t(U8()) << (8 * b);
    return v;
}

uint64_t Reader::U64() {
    uint64_t v = 0;
    for (int b = 0; b < 8; ++b) v |= uint64_t(U8()) << (8 * b);
    return v;
}

std::string Reader::Text() {
    const uint16_t length = U16();
    if (at + length > bytes.size()) {
        ok = false;
        return {};
    }
    std::string s(bytes.begin() + static_cast<long>(at), bytes.begin() + static_cast<long>(at + length));
    at += length;
    return s;
}

namespace {

void Units(Writer& out, const std::vector<entt::entity>& units) {
    out.U16(static_cast<uint16_t>(units.size()));
    for (entt::entity e : units) out.Entity(e);
}

std::vector<entt::entity> Units(Reader& in) {
    std::vector<entt::entity> units(in.U16());
    for (auto& e : units) e = in.Entity();
    return units;
}

// Each command's fields, written and read in the same order below.
template <typename C>
void Put(Writer& out, const C& c) {
    using T = std::decay_t<C>;
    if constexpr (std::is_same_v<T, Core::MoveCommand>) {
        out.Entity(c.entity); out.Fixed(c.target_x); out.Fixed(c.target_y);
    } else if constexpr (std::is_same_v<T, Core::GatherCommand>) {
        out.Entity(c.entity); out.Entity(c.target_resource);
    } else if constexpr (std::is_same_v<T, Core::AttackCommand>) {
        out.Entity(c.entity); out.Entity(c.target_entity);
    } else if constexpr (std::is_same_v<T, Core::BuildCommand>) {
        out.Entity(c.worker); out.I32(c.unit_id); out.I32(c.target_x); out.I32(c.target_y);
    } else if constexpr (std::is_same_v<T, Core::DeleteCommand>) {
        out.Entity(c.building);
    } else if constexpr (std::is_same_v<T, Core::TrainCommand>) {
        out.Entity(c.building); out.I32(c.unit_id);
    } else if constexpr (std::is_same_v<T, Core::CancelTrainCommand>) {
        out.Entity(c.building); out.I32(c.slot);
    } else if constexpr (std::is_same_v<T, Core::ResearchCommand>) {
        out.Entity(c.building); out.I32(c.tech_id);
    } else if constexpr (std::is_same_v<T, Core::TreasonCommand>) {
        out.Entity(c.castle);
    } else if constexpr (std::is_same_v<T, Core::RallyCommand>) {
        out.Entity(c.building); out.Fixed(c.x); out.Fixed(c.y); out.Entity(c.target);
    } else if constexpr (std::is_same_v<T, Core::FormationMoveCommand>) {
        Units(out, c.units); out.Fixed(c.target_x); out.Fixed(c.target_y);
        out.U8(static_cast<uint8_t>(c.formation));
    } else if constexpr (std::is_same_v<T, Core::StanceCommand>) {
        out.Entity(c.entity); out.U8(c.stance);
    } else if constexpr (std::is_same_v<T, Core::StopCommand>) {
        out.Entity(c.entity);
    } else if constexpr (std::is_same_v<T, Core::GarrisonCommand>) {
        out.Entity(c.unit); out.Entity(c.building);
    } else if constexpr (std::is_same_v<T, Core::DropOffCommand>) {
        out.Entity(c.unit); out.Entity(c.building);
    } else if constexpr (std::is_same_v<T, Core::UngarrisonCommand>) {
        out.Entity(c.building);
    } else if constexpr (std::is_same_v<T, Core::TownBellCommand>) {
        out.Entity(c.town_center);
    } else if constexpr (std::is_same_v<T, Core::BackToWorkCommand>) {
        out.Entity(c.building);
    } else if constexpr (std::is_same_v<T, Core::PatrolCommand>) {
        Units(out, c.units); out.Fixed(c.target_x); out.Fixed(c.target_y);
    } else if constexpr (std::is_same_v<T, Core::GuardCommand>) {
        Units(out, c.units); out.Entity(c.charge);
    } else if constexpr (std::is_same_v<T, Core::FollowCommand>) {
        Units(out, c.units); out.Entity(c.charge);
    } else if constexpr (std::is_same_v<T, Core::HealCommand>) {
        out.Entity(c.unit); out.Entity(c.target);
    } else if constexpr (std::is_same_v<T, Core::RelicCommand>) {
        out.Entity(c.unit); out.Entity(c.target);
    } else if constexpr (std::is_same_v<T, Core::UnloadCommand>) {
        out.Entity(c.transport); out.Fixed(c.target_x); out.Fixed(c.target_y);
    } else if constexpr (std::is_same_v<T, Core::ResignCommand>) {
        // Nothing but who: the player byte follows.
    } else if constexpr (std::is_same_v<T, Core::MarketCommand>) {
        out.Entity(c.market); out.U8(c.resource); out.U8(c.lots); out.U8(c.buy ? 1 : 0);
    } else if constexpr (std::is_same_v<T, Core::TradeCommand>) {
        out.Entity(c.unit); out.Entity(c.partner);
    } else if constexpr (std::is_same_v<T, Core::TributeCommand>) {
        out.U8(c.recipient); out.U8(c.resource); out.I32(c.amount);
    } else if constexpr (std::is_same_v<T, Core::DiplomacyCommand>) {
        out.U8(c.other); out.U8(c.stance);
    } else if constexpr (std::is_same_v<T, Core::AlliedVictoryCommand>) {
        out.U8(c.on ? 1 : 0);
    } else if constexpr (std::is_same_v<T, Core::ChatCommand>) {
        out.U8(c.audience); out.U8(c.to); out.Text(c.text);
    } else if constexpr (std::is_same_v<T, Core::FlareCommand>) {
        out.Fixed(c.x); out.Fixed(c.y);
    } else if constexpr (std::is_same_v<T, Core::WallCommand>) {
        Units(out, c.workers); out.I32(c.unit_id);
        out.I32(c.from_x); out.I32(c.from_y); out.I32(c.to_x); out.I32(c.to_y);
    } else if constexpr (std::is_same_v<T, Core::GateCommand>) {
        out.Entity(c.gate); out.U8(c.lock ? 1 : 0);
    } else if constexpr (std::is_same_v<T, Core::RepairCommand>) {
        Units(out, c.workers); out.Entity(c.target);
    } else if constexpr (std::is_same_v<T, Core::PackCommand>) {
        Units(out, c.units); out.U8(c.pack ? 1 : 0);
    } else if constexpr (std::is_same_v<T, Core::CheatCommand>) {
        out.I32(c.code);
    } else if constexpr (std::is_same_v<T, Core::ComputerResourceCommand>) {
        out.I32(c.resource); out.I32(c.amount);
    } else if constexpr (std::is_same_v<T, Core::SelectionCommand>) {
        Units(out, c.units);
    } else if constexpr (std::is_same_v<T, Core::WaypointCommand>) {
        Units(out, c.units); out.Fixed(c.target_x); out.Fixed(c.target_y);
        out.U8(static_cast<uint8_t>(c.formation));
    } else {
        static_assert(sizeof(T) == 0, "every command needs its bytes");
    }
    out.U8(c.player);
}

template <typename T>
T Take(Reader& in) {
    T c{};
    if constexpr (std::is_same_v<T, Core::MoveCommand>) {
        c.entity = in.Entity(); c.target_x = in.Fixed(); c.target_y = in.Fixed();
    } else if constexpr (std::is_same_v<T, Core::GatherCommand>) {
        c.entity = in.Entity(); c.target_resource = in.Entity();
    } else if constexpr (std::is_same_v<T, Core::AttackCommand>) {
        c.entity = in.Entity(); c.target_entity = in.Entity();
    } else if constexpr (std::is_same_v<T, Core::BuildCommand>) {
        c.worker = in.Entity(); c.unit_id = in.I32(); c.target_x = in.I32(); c.target_y = in.I32();
    } else if constexpr (std::is_same_v<T, Core::DeleteCommand>) {
        c.building = in.Entity();
    } else if constexpr (std::is_same_v<T, Core::TrainCommand>) {
        c.building = in.Entity(); c.unit_id = in.I32();
    } else if constexpr (std::is_same_v<T, Core::CancelTrainCommand>) {
        c.building = in.Entity(); c.slot = in.I32();
    } else if constexpr (std::is_same_v<T, Core::ResearchCommand>) {
        c.building = in.Entity(); c.tech_id = in.I32();
    } else if constexpr (std::is_same_v<T, Core::TreasonCommand>) {
        c.castle = in.Entity();
    } else if constexpr (std::is_same_v<T, Core::RallyCommand>) {
        c.building = in.Entity(); c.x = in.Fixed(); c.y = in.Fixed(); c.target = in.Entity();
    } else if constexpr (std::is_same_v<T, Core::FormationMoveCommand>) {
        c.units = Units(in); c.target_x = in.Fixed(); c.target_y = in.Fixed();
        c.formation = static_cast<Core::Formation>(in.U8());
    } else if constexpr (std::is_same_v<T, Core::StanceCommand>) {
        c.entity = in.Entity(); c.stance = in.U8();
    } else if constexpr (std::is_same_v<T, Core::StopCommand>) {
        c.entity = in.Entity();
    } else if constexpr (std::is_same_v<T, Core::GarrisonCommand>) {
        c.unit = in.Entity(); c.building = in.Entity();
    } else if constexpr (std::is_same_v<T, Core::DropOffCommand>) {
        c.unit = in.Entity(); c.building = in.Entity();
    } else if constexpr (std::is_same_v<T, Core::UngarrisonCommand>) {
        c.building = in.Entity();
    } else if constexpr (std::is_same_v<T, Core::TownBellCommand>) {
        c.town_center = in.Entity();
    } else if constexpr (std::is_same_v<T, Core::BackToWorkCommand>) {
        c.building = in.Entity();
    } else if constexpr (std::is_same_v<T, Core::PatrolCommand>) {
        c.units = Units(in); c.target_x = in.Fixed(); c.target_y = in.Fixed();
    } else if constexpr (std::is_same_v<T, Core::GuardCommand>) {
        c.units = Units(in); c.charge = in.Entity();
    } else if constexpr (std::is_same_v<T, Core::FollowCommand>) {
        c.units = Units(in); c.charge = in.Entity();
    } else if constexpr (std::is_same_v<T, Core::HealCommand>) {
        c.unit = in.Entity(); c.target = in.Entity();
    } else if constexpr (std::is_same_v<T, Core::RelicCommand>) {
        c.unit = in.Entity(); c.target = in.Entity();
    } else if constexpr (std::is_same_v<T, Core::UnloadCommand>) {
        c.transport = in.Entity(); c.target_x = in.Fixed(); c.target_y = in.Fixed();
    } else if constexpr (std::is_same_v<T, Core::MarketCommand>) {
        c.market = in.Entity(); c.resource = in.U8(); c.lots = in.U8(); c.buy = in.U8() != 0;
    } else if constexpr (std::is_same_v<T, Core::TradeCommand>) {
        c.unit = in.Entity(); c.partner = in.Entity();
    } else if constexpr (std::is_same_v<T, Core::TributeCommand>) {
        c.recipient = in.U8(); c.resource = in.U8(); c.amount = in.I32();
    } else if constexpr (std::is_same_v<T, Core::DiplomacyCommand>) {
        c.other = in.U8(); c.stance = in.U8();
    } else if constexpr (std::is_same_v<T, Core::AlliedVictoryCommand>) {
        c.on = in.U8() != 0;
    } else if constexpr (std::is_same_v<T, Core::ChatCommand>) {
        c.audience = in.U8(); c.to = in.U8(); c.text = in.Text();
    } else if constexpr (std::is_same_v<T, Core::FlareCommand>) {
        c.x = in.Fixed(); c.y = in.Fixed();
    } else if constexpr (std::is_same_v<T, Core::WallCommand>) {
        c.workers = Units(in); c.unit_id = in.I32();
        c.from_x = in.I32(); c.from_y = in.I32(); c.to_x = in.I32(); c.to_y = in.I32();
    } else if constexpr (std::is_same_v<T, Core::GateCommand>) {
        c.gate = in.Entity(); c.lock = in.U8() != 0;
    } else if constexpr (std::is_same_v<T, Core::RepairCommand>) {
        c.workers = Units(in); c.target = in.Entity();
    } else if constexpr (std::is_same_v<T, Core::PackCommand>) {
        c.units = Units(in); c.pack = in.U8() != 0;
    } else if constexpr (std::is_same_v<T, Core::CheatCommand>) {
        c.code = in.I32();
    } else if constexpr (std::is_same_v<T, Core::ComputerResourceCommand>) {
        c.resource = in.I32(); c.amount = in.I32();
    } else if constexpr (std::is_same_v<T, Core::SelectionCommand>) {
        c.units = Units(in);
    } else if constexpr (std::is_same_v<T, Core::WaypointCommand>) {
        c.units = Units(in); c.target_x = in.Fixed(); c.target_y = in.Fixed();
        c.formation = static_cast<Core::Formation>(in.U8());
    }
    c.player = in.U8();
    return c;
}

template <size_t I = 0>
bool TakeIndex(Reader& in, size_t index, Core::Command& out) {
    if constexpr (I < std::variant_size_v<Core::Command>) {
        if (index == I) {
            out = Take<std::variant_alternative_t<I, Core::Command>>(in);
            return in.ok;
        }
        return TakeIndex<I + 1>(in, index, out);
    } else {
        (void)in;
        (void)index;
        (void)out;
        return false;
    }
}

} // namespace

void Write(Writer& out, const Core::Command& command) {
    out.U8(static_cast<uint8_t>(command.index()));
    std::visit([&](const auto& c) { Put(out, c); }, command);
}

bool Read(Reader& in, Core::Command& command) {
    const size_t index = in.U8();
    return in.ok && TakeIndex(in, index, command);
}

void Write(Writer& out, const Core::GameSetup& setup) {
    out.U32(setup.seed);
    out.Text(setup.map_script);
    out.I32(setup.map_size);
    for (int seat = 0; seat < Core::kMaxPlayers; ++seat) {
        const auto& p = setup.players[seat];
        out.U8(p.active ? 1 : 0);
        out.I32(p.civ);
        out.U8(p.team);
        out.U8(p.computer ? 1 : 0);
    }
    out.U8(static_cast<uint8_t>(setup.resources));
    out.I32(setup.population_limit);
    out.I32(setup.starting_age);
    out.U8(static_cast<uint8_t>(setup.reveal));
    out.U8(static_cast<uint8_t>(setup.victory));
    out.I32(setup.wonder_years);
    out.I32(setup.relic_years);
    // Each seat's name and colour, and the game options a network game must
    // agree on.
    for (int seat = 0; seat < Core::kMaxPlayers; ++seat) {
        out.Text(setup.players[seat].name);
        out.I32(setup.players[seat].colour);
    }
    out.U8(setup.regicide ? 1 : 0);
    out.U8(setup.death_match ? 1 : 0);
    out.I32(setup.difficulty);
    out.U8(setup.lock_teams ? 1 : 0);
    out.U8(setup.all_techs ? 1 : 0);
    out.U8(setup.cheats ? 1 : 0);
    // A game of a scenario: its file, and each seat's own age.
    out.Text(setup.scenario);
    out.I32(setup.scenario_entry);
    for (int seat = 0; seat < Core::kMaxPlayers; ++seat) out.I32(setup.players[seat].starting_age);
}

bool Read(Reader& in, Core::GameSetup& setup) {
    setup.seed = in.U32();
    setup.map_script = in.Text();
    setup.map_size = in.I32();
    for (int seat = 0; seat < Core::kMaxPlayers; ++seat) {
        auto& p = setup.players[seat];
        p.active = in.U8() != 0;
        p.civ = in.I32();
        p.team = in.U8();
        p.computer = in.U8() != 0;
    }
    setup.resources = static_cast<Core::StartingResources>(in.U8());
    setup.population_limit = in.I32();
    setup.starting_age = in.I32();
    setup.reveal = static_cast<Core::Reveal>(in.U8());
    setup.victory = static_cast<Core::VictoryKind>(in.U8());
    setup.wonder_years = in.I32();
    setup.relic_years = in.I32();
    for (int seat = 0; seat < Core::kMaxPlayers; ++seat) {
        setup.players[seat].name = in.Text();
        setup.players[seat].colour = in.I32();
    }
    setup.regicide = in.U8() != 0;
    setup.death_match = in.U8() != 0;
    setup.difficulty = in.I32();
    setup.lock_teams = in.U8() != 0;
    setup.all_techs = in.U8() != 0;
    setup.cheats = in.U8() != 0;
    setup.scenario = in.Text();
    setup.scenario_entry = in.I32();
    for (int seat = 0; seat < Core::kMaxPlayers; ++seat) setup.players[seat].starting_age = in.I32();
    return in.ok;
}

} // namespace Net::Wire
