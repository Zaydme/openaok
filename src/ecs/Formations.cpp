#include "ecs/Formations.h"

#include <algorithm>

#include "core/FixedMath.h"
#include "ecs/Combat.h"
#include "ecs/components/Building.h"
#include "ecs/components/Formation.h"
#include "ecs/components/Transform.h"

namespace ECS::Formations {

namespace {

// Between neighbours in a rank: the spacing units keep when standing, so a
// formation arrives without anyone having to shuffle apart.
const Core::Fixed32 kSpacing = Core::Fixed32(7) / Core::Fixed32(10);

int IntSqrtCeil(int n) {
    int r = 0;
    while (r * r < n) ++r;
    return r;
}

struct Slot {
    int rank = 0;        // 0 is the front
    Core::Fixed32 side;  // across the line of march, in spacings from the middle
    Core::Fixed32 back;  // behind the front, in spacings
};

// Ranks of `width`, front to back, each centred.
void Ranks(int count, int width, Core::Fixed32 gap, bool stagger, std::vector<Slot>& out) {
    for (int i = 0; i < count; ++i) {
        const int rank = i / width;
        const int in_rank = std::min(width, count - rank * width);
        const int col = i % width;
        Slot slot;
        slot.rank = rank;
        Core::Fixed32 side = (Core::Fixed32(col * 2 - (in_rank - 1)) / Core::Fixed32(2)) * gap;
        if (stagger && (rank % 2) == 1) side = side + gap / Core::Fixed32(2);
        slot.side = side;
        slot.back = Core::Fixed32(rank) * gap;
        out.push_back(slot);
    }
}

} // namespace

std::vector<std::pair<entt::entity, Components::Vector2D>> Layout(
    const entt::registry& registry, const std::vector<entt::entity>& units,
    Components::Vector2D destination, Core::Formation formation) {
    std::vector<std::pair<entt::entity, Components::Vector2D>> out;

    // Who marches: units, front to back -- hand-to-hand fighters, then those
    // that shoot -- and within each, a fixed order so every machine agrees.
    std::vector<entt::entity> melee;
    std::vector<entt::entity> ranged;
    Core::Fixed32 cx{};
    Core::Fixed32 cy{};
    for (auto unit : units) {
        if (!registry.valid(unit) || registry.all_of<Components::Building>(unit)) continue;
        if (!registry.all_of<Components::Transform>(unit)) continue;
        const auto* rules = Combat::FightingRules(registry, unit);
        if (rules != nullptr && rules->speed.value <= 0) continue;
        const auto& at = registry.get<Components::Transform>(unit).position;
        cx = cx + at.x;
        cy = cy + at.y;
        (rules != nullptr && rules->projectile >= 0 ? ranged : melee).push_back(unit);
    }
    std::sort(melee.begin(), melee.end());
    std::sort(ranged.begin(), ranged.end());
    std::vector<entt::entity> order = melee;
    order.insert(order.end(), ranged.begin(), ranged.end());
    const int n = static_cast<int>(order.size());
    if (n == 0) return out;
    cx = cx / Core::Fixed32(n);
    cy = cy / Core::Fixed32(n);

    // Facing: from where the group is to where it is going. A group already
    // there faces down the screen.
    Core::Fixed32 fx = destination.x - cx;
    Core::Fixed32 fy = destination.y - cy;
    if (Core::FixedMath::Distance(fx, fy) < Core::Fixed32(1) / Core::Fixed32(10)) {
        fx = Core::Fixed32(1);
        fy = Core::Fixed32(1);
    }
    const auto forward = Core::FixedMath::NormalizeAndScale(fx, fy, Core::Fixed32(1));
    const Components::Vector2D across{Core::Fixed32(0) - forward.y, forward.x};

    // Fighters and shooters are ranked apart: the fighters' ranks first, the
    // shooters' starting a rank behind the last of them, never sharing one.
    auto ranked = [&](Core::Fixed32 gap, bool stagger, std::vector<Slot>& out) {
        const int width = std::max(1, IntSqrtCeil(n * 2));
        const int fighters = static_cast<int>(melee.size());
        Ranks(fighters, width, gap, stagger, out);
        std::vector<Slot> rear;
        Ranks(n - fighters, width, gap, stagger, rear);
        const int behind = fighters == 0 ? 0 : (fighters + width - 1) / width;
        for (auto slot : rear) {
            slot.rank += behind;
            slot.back = slot.back + Core::Fixed32(behind) * gap;
            out.push_back(slot);
        }
    };

    std::vector<Slot> slots;
    switch (formation) {
    case Core::Formation::Line:
        ranked(kSpacing, false, slots);
        break;
    case Core::Formation::Staggered:
        ranked(kSpacing * Core::Fixed32(2), true, slots);
        break;
    case Core::Formation::Flank: {
        // Two halves, a gap between them as wide as half the group's front.
        const int left = (n + 1) / 2;
        const int width = std::max(1, IntSqrtCeil(left));
        std::vector<Slot> half;
        Ranks(left, width, kSpacing, false, half);
        const Core::Fixed32 apart = Core::Fixed32(width) * kSpacing;
        for (auto s : half) { s.side = s.side - apart; slots.push_back(s); }
        half.clear();
        Ranks(n - left, width, kSpacing, false, half);
        for (auto s : half) { s.side = s.side + apart; slots.push_back(s); }
        break;
    }
    case Core::Formation::Box: {
        // Round the outside of a square first -- the fighters, who come first
        // in the order -- then filling in from the middle out.
        const int side = std::max(2, IntSqrtCeil(n));
        std::vector<Slot> ring;
        std::vector<Slot> inner;
        for (int r = 0; r < side; ++r) {
            for (int c = 0; c < side; ++c) {
                Slot s;
                s.rank = r;
                s.side = (Core::Fixed32(c * 2 - (side - 1)) / Core::Fixed32(2)) * kSpacing;
                s.back = Core::Fixed32(r) * kSpacing;
                const bool edge = r == 0 || c == 0 || r == side - 1 || c == side - 1;
                (edge ? ring : inner).push_back(s);
            }
        }
        slots = ring;
        slots.insert(slots.end(), inner.begin(), inner.end());
        slots.resize(static_cast<size_t>(n));
        break;
    }
    }

    for (int i = 0; i < n; ++i) {
        const Slot& s = slots[static_cast<size_t>(i)];
        Components::Vector2D at{
            destination.x + across.x * s.side - forward.x * s.back,
            destination.y + across.y * s.side - forward.y * s.back};
        out.emplace_back(order[static_cast<size_t>(i)], at);
    }
    return out;
}

void Lock(entt::registry& registry, const std::vector<entt::entity>& units, Core::Formation formation) {
    for (entt::entity unit : units) Leave(registry, unit);
    auto* groups = registry.ctx().find<Groups>();
    if (groups == nullptr) groups = &registry.ctx().emplace<Groups>();
    const uint32_t group = groups->next++;
    for (entt::entity unit : units) {
        if (registry.valid(unit)) registry.emplace_or_replace<Components::InFormation>(unit, group, formation);
    }
}

void Disband(entt::registry& registry, const std::vector<entt::entity>& units) {
    std::vector<uint32_t> groups;
    for (entt::entity unit : units) {
        if (const auto* in = registry.try_get<Components::InFormation>(unit)) groups.push_back(in->group);
    }
    std::vector<entt::entity> out;
    auto members = registry.view<const Components::InFormation>();
    for (auto e : members) {
        if (std::find(groups.begin(), groups.end(), members.get<const Components::InFormation>(e).group) != groups.end()) {
            out.push_back(e);
        }
    }
    for (entt::entity e : out) registry.remove<Components::InFormation>(e);
}

void Leave(entt::registry& registry, entt::entity unit) {
    const auto* in = registry.try_get<Components::InFormation>(unit);
    if (in == nullptr) return;
    const uint32_t group = in->group;
    registry.remove<Components::InFormation>(unit);
    std::vector<entt::entity> left;
    auto members = registry.view<const Components::InFormation>();
    for (auto e : members) {
        if (members.get<const Components::InFormation>(e).group == group) left.push_back(e);
    }
    if (left.size() == 1) registry.remove<Components::InFormation>(left.front());
}

std::vector<entt::entity> Members(const entt::registry& registry, entt::entity unit) {
    const auto* in = registry.try_get<Components::InFormation>(unit);
    if (in == nullptr) return {unit};
    std::vector<entt::entity> out;
    auto members = registry.view<const Components::InFormation, const Components::Transform>();
    for (auto e : members) {
        if (members.get<const Components::InFormation>(e).group == in->group) out.push_back(e);
    }
    std::sort(out.begin(), out.end());
    return out;
}

} // namespace ECS::Formations
