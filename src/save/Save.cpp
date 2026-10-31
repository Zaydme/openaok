#include "save/Save.h"

#include <cstring>
#include <fstream>
#include <iterator>
#include <type_traits>

#include "ai/Brain.h"
#include "core/GameSetup.h"
#include "core/Random.h"
#include "ecs/Chat.h"
#include "ecs/Combat.h"
#include "ecs/Formations.h"
#include "ecs/Diplomacy.h"
#include "ecs/Digest.h"
#include "ecs/Events.h"
#include "ecs/Flares.h"
#include "ecs/Fog.h"
#include "ecs/Market.h"
#include "ecs/Players.h"
#include "ecs/Score.h"
#include "ecs/Timeline.h"
#include "ecs/Triggers.h"
#include "ecs/Victory.h"
#include "ecs/Vision.h"
#include "ecs/components/Animation.h"
#include "ecs/components/Building.h"
#include "ecs/components/Combat.h"
#include "ecs/components/Formation.h"
#include "ecs/components/Doppelganger.h"
#include "ecs/components/Duty.h"
#include "ecs/components/Garrison.h"
#include "ecs/components/Gate.h"
#include "ecs/components/Grazer.h"
#include "ecs/components/Health.h"
#include "ecs/components/Identity.h"
#include "ecs/components/MinimapMarker.h"
#include "ecs/components/Monk.h"
#include "ecs/components/Mortal.h"
#include "ecs/components/Owner.h"
#include "ecs/components/Packing.h"
#include "ecs/components/PartOf.h"
#include "ecs/components/Path.h"
#include "ecs/components/ProductionQueue.h"
#include "ecs/components/Projectile.h"
#include "ecs/components/RallyPoint.h"
#include "ecs/components/Relic.h"
#include "ecs/components/Capturable.h"
#include "ecs/components/Felled.h"
#include "ecs/components/Resource.h"
#include "ecs/components/Selectable.h"
#include "ecs/components/Sight.h"
#include "ecs/components/Sprite.h"
#include "ecs/components/SpritePieces.h"
#include "ecs/components/Trader.h"
#include "ecs/components/Transform.h"
#include "ecs/components/Waypoints.h"
#include "ecs/components/Worker.h"
#include "net/Wire.h"

namespace Save {

namespace {

using Net::Wire::Reader;
using Net::Wire::Writer;
namespace C = ECS::Components;

constexpr char kMagic[8] = {'A', 'O', 'K', 'C', 'S', 'A', 'V', 'E'};
// The save format's version; a save of any other version is refused.
constexpr uint32_t kVersion = 7;

// Plain data goes out as it lies in memory.
template <typename T>
void PutPod(Writer& w, const T& value) {
    static_assert(std::is_trivially_copyable_v<T>, "needs its own Put");
    const auto* p = reinterpret_cast<const uint8_t*>(&value);
    w.bytes.insert(w.bytes.end(), p, p + sizeof(T));
}
template <typename T>
void GetPod(Reader& r, T& value) {
    static_assert(std::is_trivially_copyable_v<T>, "needs its own Get");
    if (r.at + sizeof(T) > r.bytes.size()) {
        r.ok = false;
        return;
    }
    std::memcpy(&value, r.bytes.data() + r.at, sizeof(T));
    r.at += sizeof(T);
}
template <typename T>
void PutPods(Writer& w, const std::vector<T>& values) {
    w.U32(static_cast<uint32_t>(values.size()));
    for (const T& v : values) PutPod(w, v);
}
template <typename T>
void GetPods(Reader& r, std::vector<T>& values) {
    const uint32_t n = r.U32();
    if (!r.ok || n > r.bytes.size()) {
        r.ok = false;
        return;
    }
    values.resize(n);
    for (T& v : values) GetPod(r, v);
}

// The components that hold vectors, field by field.
void Put(Writer& w, const C::Path& p) {
    PutPods(w, p.waypoints);
    w.U64(p.current_waypoint_index);
    PutPod(w, p.goal);
    w.U8(p.has_goal ? 1 : 0);
    w.Fixed(p.speed_cap);
    w.Entity(p.target);
    w.Fixed(p.range);
    PutPod(w, p.replans);
}
void Get(Reader& r, C::Path& p) {
    GetPods(r, p.waypoints);
    p.current_waypoint_index = static_cast<size_t>(r.U64());
    GetPod(r, p.goal);
    p.has_goal = r.U8() != 0;
    p.speed_cap = r.Fixed();
    p.target = r.Entity();
    p.range = r.Fixed();
    GetPod(r, p.replans);
}
void Put(Writer& w, const C::Garrison& g) {
    PutPods(w, g.inside);
    w.U8(g.bell_rung ? 1 : 0);
}
void Get(Reader& r, C::Garrison& g) {
    GetPods(r, g.inside);
    g.bell_rung = r.U8() != 0;
}
void Put(Writer& w, const C::RelicShrine& s) {
    PutPods(w, s.relics);
    w.Fixed(s.gold);
}
void Get(Reader& r, C::RelicShrine& s) {
    GetPods(r, s.relics);
    s.gold = r.Fixed();
}
template <typename T>
void Put(Writer& w, const T& value) { PutPod(w, value); }
template <typename T>
void Get(Reader& r, T& value) { GetPod(r, value); }

// The players' histories: the readings and the events, then the battle in
// progress and what the next reading compares against.
void PutTimeline(Writer& w, const ECS::Timeline::Table& t) {
    for (const ECS::Timeline::Record& record : t.records) {
        PutPods(w, record.samples);
        PutPods(w, record.events);
        w.I32(record.seconds);
        w.U8(record.fighting ? 1 : 0);
        w.Fixed(record.weight);
        PutPod(w, record.last);
    }
}
void GetTimeline(Reader& r, ECS::Timeline::Table& t) {
    for (ECS::Timeline::Record& record : t.records) {
        GetPods(r, record.samples);
        GetPods(r, record.events);
        record.seconds = r.I32();
        record.fighting = r.U8() != 0;
        record.weight = r.Fixed();
        GetPod(r, record.last);
    }
}

// What EnTT's snapshot hands an archive: counts, entities, components.
struct Out {
    Writer& w;
    void operator()(std::underlying_type_t<entt::entity> n) { w.U32(n); }
    void operator()(entt::entity e) { w.Entity(e); }
    template <typename T>
    void operator()(const T& component) { Put(w, component); }
};
struct In {
    Reader& r;
    void operator()(std::underlying_type_t<entt::entity>& n) { n = r.U32(); }
    void operator()(entt::entity& e) { e = r.Entity(); }
    template <typename T>
    void operator()(T& component) { Get(r, component); }
};

// Every component the simulation keeps, in one list: what is written, what
// is read back, and what Uncovered checks the registry against.
template <typename F>
void EachComponent(F&& f) {
    f.template operator()<C::Animation>("Animation");
    f.template operator()<C::Assaulting>("Assaulting");
    f.template operator()<C::Boarding>("Boarding");
    f.template operator()<C::Building>("Building");
    f.template operator()<C::Capturable>("Capturable");
    f.template operator()<C::Combat>("Combat");
    f.template operator()<C::Corpse>("Corpse");
    f.template operator()<C::Doppelganger>("Doppelganger");
    f.template operator()<C::Duty>("Duty");
    f.template operator()<C::Dying>("Dying");
    f.template operator()<C::Felled>("Felled");
    f.template operator()<C::Garrison>("Garrison");
    f.template operator()<C::Garrisoned>("Garrisoned");
    f.template operator()<C::Gate>("Gate");
    f.template operator()<C::Grazer>("Grazer");
    f.template operator()<C::Health>("Health");
    f.template operator()<C::Identity>("Identity");
    f.template operator()<C::InFormation>("InFormation");
    f.template operator()<C::Landing>("Landing");
    f.template operator()<C::MinimapMarker>("MinimapMarker");
    f.template operator()<C::Monk>("Monk");
    f.template operator()<C::Owner>("Owner");
    f.template operator()<C::PartOf>("PartOf");
    f.template operator()<C::Path>("Path");
    f.template operator()<C::ProductionQueue>("ProductionQueue");
    f.template operator()<C::Projectile>("Projectile");
    f.template operator()<C::RallyPoint>("RallyPoint");
    f.template operator()<C::RelicBearer>("RelicBearer");
    f.template operator()<C::RelicErrand>("RelicErrand");
    f.template operator()<C::RelicShrine>("RelicShrine");
    f.template operator()<C::Remembered>("Remembered");
    f.template operator()<C::RulesOf>("RulesOf");
    f.template operator()<C::Resource>("Resource");
    f.template operator()<C::Selectable>("Selectable");
    f.template operator()<C::Sight>("Sight");
    f.template operator()<C::Sprite>("Sprite");
    f.template operator()<C::SpritePieces>("SpritePieces");
    f.template operator()<C::Trader>("Trader");
    f.template operator()<C::Transform>("Transform");
    f.template operator()<C::Transforming>("Transforming");
    f.template operator()<C::Waypoints>("Waypoints");
    f.template operator()<C::Worker>("Worker");
}

// The sizes of everything written raw: a build that lays them out
// differently refuses the save rather than misreading it.
uint64_t Fingerprint() {
    uint64_t h = 1469598103934665603ull;
    auto mix = [&](uint64_t v) {
        h ^= v;
        h *= 1099511628211ull;
    };
    EachComponent([&]<typename T>(const char*) { mix(sizeof(T)); });
    mix(sizeof(ECS::Players::TechStatus));
    mix(sizeof(Core::Rules::UnitRules));
    mix(sizeof(ECS::Victory::Outcome));
    mix(sizeof(AI::Brain));
    mix(sizeof(Core::Random));
    mix(sizeof(ECS::Combat::HuntRandom));
    mix(sizeof(ECS::Formations::Groups));
    mix(sizeof(ECS::Market::Prices));
    mix(sizeof(ECS::Diplomacy::Table));
    mix(sizeof(ECS::Flares::Flare));
    mix(sizeof(ECS::Fog::Ground));
    mix(sizeof(ECS::Score::Table));
    mix(sizeof(ECS::Timeline::Sample));
    mix(sizeof(ECS::Timeline::Event));
    mix(sizeof(ECS::Timeline::Last));
    mix(sizeof(Physics::Tile));
    mix(sizeof(ECS::Triggers::Run));
    mix(sizeof(ECS::Triggers::Placed));
    return h;
}

// A scenario game's triggers: where each stands, the file's units' entities,
// what the AI and the interface have told them, and the display log.
void PutTriggers(Writer& w, const ECS::Triggers::State& s, const ECS::Triggers::Display* d) {
    w.U64(s.tick);
    PutPods(w, s.runs);
    w.I32(s.check_ms);
    PutPods(w, s.placed);
    for (const auto& words : s.goals) {
        for (uint32_t v : words) w.U32(v);
    }
    for (uint32_t v : s.signals) w.U32(v);
    for (const auto& chosen : s.selected) PutPods(w, chosen);
    PutPods(w, s.locked_gates);
    PutPods(w, s.unsupported);
    w.U16(s.declared);
    const uint32_t lines = d != nullptr ? static_cast<uint32_t>(d->lines.size()) : 0u;
    w.U32(lines);
    for (uint32_t i = 0; i < lines; ++i) {
        const auto& line = d->lines[i];
        w.U64(line.tick);
        w.U8(static_cast<uint8_t>(line.kind));
        PutPod(w, line.player);
        w.I32(line.trigger);
        w.I32(line.string_id);
        w.Text(line.text);
        w.Text(line.sound);
        w.I32(line.sound_id);
        w.I32(line.seconds);
        w.I32(line.panel);
        w.I32(line.x);
        w.I32(line.y);
    }
}
void GetTriggers(Reader& r, ECS::Triggers::State& s, ECS::Triggers::Display& d) {
    s.tick = r.U64();
    GetPods(r, s.runs);
    s.check_ms = r.I32();
    GetPods(r, s.placed);
    for (auto& words : s.goals) {
        for (uint32_t& v : words) v = r.U32();
    }
    for (uint32_t& v : s.signals) v = r.U32();
    for (auto& chosen : s.selected) GetPods(r, chosen);
    GetPods(r, s.locked_gates);
    GetPods(r, s.unsupported);
    s.declared = r.U16();
    const uint32_t lines = r.U32();
    if (!r.ok || lines > r.bytes.size()) {
        r.ok = false;
        return;
    }
    d.lines.resize(lines);
    for (auto& line : d.lines) {
        line.tick = r.U64();
        line.kind = static_cast<ECS::Triggers::Show>(r.U8());
        GetPod(r, line.player);
        line.trigger = r.I32();
        line.string_id = r.I32();
        line.text = r.Text();
        line.sound = r.Text();
        line.sound_id = r.I32();
        line.seconds = r.I32();
        line.panel = r.I32();
        line.x = r.I32();
        line.y = r.I32();
    }
}

void PutPlayers(Writer& w, const ECS::Players::Table& table) {
    for (const auto& p : table.players) {
        w.U8(p.active ? 1 : 0);
        PutPod(w, p.id);
        w.I32(p.civ);
        PutPod(w, p.team);
        PutPods(w, p.resources);
        PutPods(w, p.units);
        PutPods(w, p.art);
        PutPods(w, p.techs);
    }
    w.I32(table.population_limit);
}
void GetPlayers(Reader& r, ECS::Players::Table& table) {
    for (auto& p : table.players) {
        p.active = r.U8() != 0;
        GetPod(r, p.id);
        p.civ = r.I32();
        GetPod(r, p.team);
        GetPods(r, p.resources);
        GetPods(r, p.units);
        GetPods(r, p.art);
        GetPods(r, p.techs);
    }
    table.population_limit = r.I32();
}

void PutChat(Writer& w, const ECS::Chat::Log& log) {
    w.U32(static_cast<uint32_t>(log.lines.size()));
    for (const auto& line : log.lines) {
        w.U64(line.tick);
        PutPod(w, line.from);
        w.U16(line.to);
        w.U8(static_cast<uint8_t>(line.audience));
        w.Text(line.text);
        w.I32(line.taunt);
    }
}
void GetChat(Reader& r, ECS::Chat::Log& log) {
    const uint32_t n = r.U32();
    if (!r.ok || n > r.bytes.size()) {
        r.ok = false;
        return;
    }
    log.lines.resize(n);
    for (auto& line : log.lines) {
        line.tick = r.U64();
        GetPod(r, line.from);
        line.to = r.U16();
        line.audience = static_cast<ECS::Chat::Audience>(r.U8());
        line.text = r.Text();
        line.taunt = r.I32();
    }
}

// The computer players. Their programs are the scripts as the lobby loads
// them, which starting the game again rebuilds identically; what is saved is
// what changes: each brain's random stream, its rules' memory and its
// executor's, and what they share.
void PutMind(Writer& w, const AI::Mind& m) {
    PutPods(w, m.enabled);
    PutPod(w, m.goals);
    PutPod(w, m.sn);
    PutPod(w, m.timers);
    w.I32(m.random_number);
    PutPod(w, m.escrow_percent);
    PutPod(w, m.escrow);
    PutPod(w, m.last_stock);
    w.U8(m.stock_seen ? 1 : 0);
    PutPod(w, m.taunts);
    PutPod(w, m.events);
    PutPod(w, m.signals);
    PutPod(w, m.stance);
    PutPod(w, m.difficulty_parameters);
    w.I32(m.doctrine);
    w.I32(m.age);
    w.U64(static_cast<uint64_t>(m.age_since_ms));
    PutPod(w, m.found);
    PutPod(w, m.tribute);
    PutPod(w, m.tribute_memory);
}
void GetMind(Reader& r, AI::Mind& m) {
    GetPods(r, m.enabled);
    GetPod(r, m.goals);
    GetPod(r, m.sn);
    GetPod(r, m.timers);
    m.random_number = r.I32();
    GetPod(r, m.escrow_percent);
    GetPod(r, m.escrow);
    GetPod(r, m.last_stock);
    m.stock_seen = r.U8() != 0;
    GetPod(r, m.taunts);
    GetPod(r, m.events);
    GetPod(r, m.signals);
    GetPod(r, m.stance);
    GetPod(r, m.difficulty_parameters);
    m.doctrine = r.I32();
    m.age = r.I32();
    m.age_since_ms = static_cast<int64_t>(r.U64());
    GetPod(r, m.found);
    GetPod(r, m.tribute);
    GetPod(r, m.tribute_memory);
}
void PutGroup(Writer& w, const AI::Group& g) {
    PutPods(w, g.units);
    PutPod(w, g.target);
    w.I32(g.idle_s);
    PutPods(w, g.ships);
    w.I32(g.stage);
    w.I32(g.stage_s);
}
void GetGroup(Reader& r, AI::Group& g) {
    GetPods(r, g.units);
    GetPod(r, g.target);
    g.idle_s = r.I32();
    GetPods(r, g.ships);
    g.stage = r.I32();
    g.stage_s = r.I32();
}
void PutBrains(Writer& w, const AI::Brains& table) {
    w.U32(static_cast<uint32_t>(table.brains.size()));
    for (const AI::Brain& b : table.brains) {
        PutPod(w, b.player);
        PutPod(w, b.rng);
        w.I32(b.think_ms);
        PutMind(w, b.mind);
        PutGroup(w, b.attack);
        PutGroup(w, b.boat_attack);
        PutPods(w, b.explorers);
        PutPods(w, b.gathering);
        w.U32(static_cast<uint32_t>(b.explore_groups.size()));
        for (const auto& group : b.explore_groups) PutPods(w, group);
        PutPod(w, b.boar);
        PutPods(w, b.hunters);
        w.I32(b.scout_ring);
        w.I32(b.scout_bearing);
        w.U64(static_cast<uint64_t>(b.town_struck_ms));
        PutPod(w, b.intruder);
        w.U64(b.events_tick);
        w.U64(b.chat_tick);
        w.U8(b.resigned ? 1 : 0);
        w.I32(b.wall_x);
        w.I32(b.wall_y);
        w.I32(b.wall_perimeter);
    }
    w.U64(static_cast<uint64_t>(table.now_ms));
    PutPod(w, table.shared_goals);
    PutPod(w, table.ages);
    PutPod(w, table.age_since);
    w.U32(static_cast<uint32_t>(table.chat.size()));
    for (const AI::ChatLine& line : table.chat) {
        w.U64(static_cast<uint64_t>(line.ms));
        PutPod(w, line.from);
        w.U16(line.to);
        w.I32(line.string_id);
        w.Text(line.text);
    }
}
// Onto the brains starting the game built: the same seats in the same order.
void GetBrains(Reader& r, AI::Brains& table) {
    if (r.U32() != table.brains.size()) {
        r.ok = false;
        return;
    }
    for (AI::Brain& b : table.brains) {
        Core::PlayerId player = 0;
        GetPod(r, player);
        if (player != b.player) r.ok = false;
        GetPod(r, b.rng);
        b.think_ms = r.I32();
        GetMind(r, b.mind);
        GetGroup(r, b.attack);
        GetGroup(r, b.boat_attack);
        GetPods(r, b.explorers);
        GetPods(r, b.gathering);
        b.explore_groups.resize(std::min<uint32_t>(r.U32(), 64));
        for (auto& group : b.explore_groups) GetPods(r, group);
        GetPod(r, b.boar);
        GetPods(r, b.hunters);
        b.scout_ring = r.I32();
        b.scout_bearing = r.I32();
        b.town_struck_ms = static_cast<int64_t>(r.U64());
        GetPod(r, b.intruder);
        b.events_tick = r.U64();
        b.chat_tick = r.U64();
        b.resigned = r.U8() != 0;
        b.wall_x = r.I32();
        b.wall_y = r.I32();
        b.wall_perimeter = r.I32();
        if (!r.ok) return;
    }
    table.now_ms = static_cast<int64_t>(r.U64());
    GetPod(r, table.shared_goals);
    GetPod(r, table.ages);
    GetPod(r, table.age_since);
    const uint32_t n = r.U32();
    if (!r.ok || n > r.bytes.size()) {
        r.ok = false;
        return;
    }
    table.chat.resize(n);
    for (AI::ChatLine& line : table.chat) {
        line.ms = static_cast<int64_t>(r.U64());
        GetPod(r, line.from);
        line.to = r.U16();
        line.string_id = r.I32();
        line.text = r.Text();
    }
}

void Fail(std::string* error, const char* why) {
    if (error != nullptr) *error = why;
}

} // namespace

bool Write(const ECS::Simulation& sim, std::vector<uint8_t>& out) {
    const entt::registry& registry = sim.GetRegistry();
    Writer w;
    w.bytes.insert(w.bytes.end(), std::begin(kMagic), std::end(kMagic));
    w.U32(kVersion);
    w.U64(Fingerprint());
    Net::Wire::Write(w, sim.Setup());
    w.U64(sim.Tick());
    w.U32(static_cast<uint32_t>(sim.player_starts.size()));
    for (const auto& start : sim.player_starts) {
        w.I32(start.first);
        w.I32(start.second);
    }

    // The entities and every pool, each in its own order.
    Out archive{w};
    entt::snapshot snapshot{registry};
    snapshot.get<entt::entity>(archive);
    EachComponent([&]<typename T>(const char*) { snapshot.get<T>(archive); });

    // The context.
    const auto& ctx = registry.ctx();
    const auto* random = ctx.find<Core::Random>();
    const auto* players = ctx.find<ECS::Players::Table>();
    const auto* victory = ctx.find<ECS::Victory::State>();
    const auto* vision = ctx.find<ECS::Vision::Map>();
    const auto* brains = ctx.find<AI::Brains>();
    const auto* prices = ctx.find<ECS::Market::Prices>();
    const auto* diplomacy = ctx.find<ECS::Diplomacy::Table>();
    const auto* chat = ctx.find<ECS::Chat::Log>();
    const auto* flares = ctx.find<ECS::Flares::Log>();
    const auto* fog = ctx.find<ECS::Fog::Memory>();
    const auto* score = ctx.find<ECS::Score::Table>();
    const auto* timeline = ctx.find<ECS::Timeline::Table>();
    const auto* hunt = ctx.find<ECS::Combat::HuntRandom>();
    if (!random || !players || !victory || !vision || !brains || !prices || !diplomacy ||
        !chat || !flares || !fog || !score || !timeline || !hunt) {
        return false;
    }
    PutPod(w, *random);
    PutPod(w, *hunt);
    const auto* groups = ctx.find<ECS::Formations::Groups>();
    PutPod(w, groups != nullptr ? *groups : ECS::Formations::Groups{});
    PutPlayers(w, *players);
    PutPod(w, victory->outcome);
    Net::Wire::Write(w, victory->setup);
    w.I32(victory->check_ms);
    w.I32(victory->wonder_id);
    w.I32(victory->relic_id);
    w.I32(victory->king_id);
    w.I32(vision->width);
    w.I32(vision->height);
    w.U8(static_cast<uint8_t>(vision->reveal));
    PutPods(w, vision->explored);
    PutPods(w, vision->visible);
    w.U16(vision->explore_all);
    w.U16(vision->see_all);
    PutBrains(w, *brains);
    PutPod(w, *prices);
    PutPod(w, *diplomacy);
    PutChat(w, *chat);
    w.I32(flares->lifetime_ms);
    PutPods(w, flares->flares);
    // The ground each player remembers out of sight; its pictures are
    // entities, written above.
    for (const auto& ground : fog->ground) PutPods(w, ground);
    w.U32(fog->revision);
    // The score's own state: its clock, the most ever held, the ages' times.
    // Its counters are in the players' resources, already written.
    PutPod(w, *score);
    PutTimeline(w, *timeline);
    const auto* events = ctx.find<ECS::Events::Log>();
    w.U64(events != nullptr ? events->tick : 0);
    PutPods(w, events != nullptr ? events->events : std::vector<ECS::Events::Event>{});
    // A scenario game's triggers; the scenario itself comes back from its
    // file, as the map of a random map game comes back from its script.
    const auto* triggers = ctx.find<ECS::Triggers::State>();
    w.U8(triggers != nullptr ? 1 : 0);
    if (triggers != nullptr) PutTriggers(w, *triggers, ctx.find<ECS::Triggers::Display>());

    // The ground and what stands on it.
    PutPods(w, sim.GetGrid().Tiles());

    // What the world must come back as.
    w.U64(ECS::Digest::Of(registry));
    out = std::move(w.bytes);
    return true;
}

bool ReadSetup(const std::vector<uint8_t>& in, Core::GameSetup& setup) {
    Reader r(in);
    if (in.size() < sizeof(kMagic) || std::memcmp(in.data(), kMagic, sizeof(kMagic)) != 0) return false;
    r.at = sizeof(kMagic);
    if (r.U32() != kVersion || r.U64() != Fingerprint()) return false;
    return Net::Wire::Read(r, setup) && r.ok;
}

bool Read(ECS::Simulation& sim, const std::vector<uint8_t>& in, std::string* error) {
    Reader r(in);
    if (in.size() < sizeof(kMagic) || std::memcmp(in.data(), kMagic, sizeof(kMagic)) != 0) {
        Fail(error, "not a saved game");
        return false;
    }
    r.at = sizeof(kMagic);
    if (r.U32() != kVersion || r.U64() != Fingerprint()) {
        Fail(error, "saved by a different build");
        return false;
    }
    Core::GameSetup setup;
    if (!Net::Wire::Read(r, setup)) {
        Fail(error, "the saved lobby is damaged");
        return false;
    }
    const uint64_t tick = r.U64();
    std::vector<std::pair<int, int>> starts(r.U32());
    for (auto& start : starts) {
        start.first = r.I32();
        start.second = r.I32();
    }
    if (!r.ok) {
        Fail(error, "the save is cut short");
        return false;
    }

    // The world as starting the game built it -- the map's own tables, the
    // grid's layers -- and then the saved state over all of it.
    sim.Init(setup);
    entt::registry registry;
    In archive{r};
    entt::snapshot_loader loader{registry};
    loader.get<entt::entity>(archive);
    EachComponent([&]<typename T>(const char*) { loader.get<T>(archive); });

    // The context: what Init built is the starting point; each entry is then
    // overwritten from the save.
    auto& ctx = sim.GetRegistry().ctx();
    registry.ctx().insert_or_assign(setup);
    Core::Random random{1};
    GetPod(r, random);
    registry.ctx().emplace<Core::Random>(random);
    ECS::Combat::HuntRandom hunt;
    GetPod(r, hunt);
    registry.ctx().insert_or_assign(hunt);
    ECS::Formations::Groups groups;
    GetPod(r, groups);
    registry.ctx().insert_or_assign(groups);
    ECS::Players::Table players = ctx.get<ECS::Players::Table>();
    GetPlayers(r, players);
    registry.ctx().insert_or_assign(std::move(players));
    ECS::Victory::State victory = ctx.get<ECS::Victory::State>();
    GetPod(r, victory.outcome);
    Net::Wire::Read(r, victory.setup);
    victory.check_ms = r.I32();
    victory.wonder_id = r.I32();
    victory.relic_id = r.I32();
    victory.king_id = r.I32();
    registry.ctx().insert_or_assign(std::move(victory));
    ECS::Vision::Map vision;
    vision.width = r.I32();
    vision.height = r.I32();
    vision.reveal = static_cast<Core::Reveal>(r.U8());
    GetPods(r, vision.explored);
    GetPods(r, vision.visible);
    vision.explore_all = r.U16();
    vision.see_all = r.U16();
    registry.ctx().insert_or_assign(std::move(vision));
    AI::Brains brains = ctx.get<AI::Brains>();
    GetBrains(r, brains);
    registry.ctx().insert_or_assign(std::move(brains));
    ECS::Market::Prices prices;
    GetPod(r, prices);
    registry.ctx().insert_or_assign(prices);
    ECS::Diplomacy::Table diplomacy;
    GetPod(r, diplomacy);
    registry.ctx().insert_or_assign(diplomacy);
    ECS::Chat::Log chat;
    GetChat(r, chat);
    registry.ctx().insert_or_assign(std::move(chat));
    ECS::Flares::Log flares;
    flares.lifetime_ms = r.I32();
    GetPods(r, flares.flares);
    registry.ctx().insert_or_assign(std::move(flares));
    ECS::Fog::Memory fog;
    for (auto& ground : fog.ground) GetPods(r, ground);
    fog.revision = r.U32();
    registry.ctx().insert_or_assign(std::move(fog));
    ECS::Score::Table score;
    GetPod(r, score);
    registry.ctx().insert_or_assign(score);
    ECS::Timeline::Table timeline;
    GetTimeline(r, timeline);
    registry.ctx().insert_or_assign(std::move(timeline));
    // The news of the last few seconds: the computer players read it (who
    // struck their town), so a loaded game must have what the saved one had.
    ECS::Events::Log events;
    events.tick = r.U64();
    GetPods(r, events.events);
    registry.ctx().insert_or_assign(std::move(events));
    // A scenario game: the file Init read again, and the triggers' state.
    if (r.U8() != 0) {
        ECS::Triggers::State triggers;
        if (const auto* built = ctx.find<ECS::Triggers::State>()) triggers = *built;
        ECS::Triggers::Display display;
        GetTriggers(r, triggers, display);
        registry.ctx().insert_or_assign(std::move(triggers));
        registry.ctx().insert_or_assign(std::move(display));
    }
    if (auto* file = ctx.find<Scenario::File>()) registry.ctx().insert_or_assign(std::move(*file));

    std::vector<Physics::Tile> tiles;
    GetPods(r, tiles);
    const uint64_t digest = r.U64();
    if (!r.ok) {
        Fail(error, "the save is cut short");
        return false;
    }
    if (!sim.GetGrid().RestoreTiles(tiles)) {
        Fail(error, "the saved map is the wrong size");
        return false;
    }
    sim.ReplaceRegistry(std::move(registry));
    sim.RestoreTick(tick);
    sim.player_starts = std::move(starts);
    if (ECS::Digest::Of(sim.GetRegistry()) != digest) {
        Fail(error, "the restored world is not the one saved");
        return false;
    }
    return true;
}

bool WriteFile(const ECS::Simulation& sim, const std::string& path) {
    std::vector<uint8_t> bytes;
    if (!Write(sim, bytes)) return false;
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return static_cast<bool>(out);
}

bool ReadFile(ECS::Simulation& sim, const std::string& path, std::string* error) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        Fail(error, "cannot open the file");
        return false;
    }
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return Read(sim, bytes, error);
}

bool ReadBytes(const std::string& path, std::vector<uint8_t>& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return true;
}

std::vector<std::string> Uncovered(const entt::registry& registry) {
    std::vector<entt::id_type> known;
    known.push_back(entt::type_hash<entt::entity>::value());
    EachComponent([&]<typename T>(const char*) { known.push_back(entt::type_hash<T>::value()); });
    std::vector<std::string> missing;
    for (auto [id, storage] : registry.storage()) {
        if (storage.empty()) continue;
        bool found = false;
        for (entt::id_type k : known) found = found || k == id;
        if (!found) missing.emplace_back(storage.type().name());
    }
    return missing;
}

} // namespace Save
