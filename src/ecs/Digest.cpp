#include "ecs/Digest.h"

#include <algorithm>
#include <vector>

#include "core/Rules.h"
#include "ecs/Diplomacy.h"
#include "ecs/Market.h"
#include "ecs/Players.h"
#include "ecs/components/Health.h"
#include "ecs/components/Identity.h"
#include "ecs/components/Owner.h"
#include "ecs/components/Transform.h"

namespace ECS::Digest {

uint64_t Of(const entt::registry& registry) {
    // FNV-1a.
    uint64_t hash = 1469598103934665603ull;
    auto mix = [&hash](uint64_t v) {
        for (int b = 0; b < 8; ++b) {
            hash ^= static_cast<uint8_t>(v >> (b * 8));
            hash *= 1099511628211ull;
        }
    };
    std::vector<entt::entity> all;
    for (auto e : registry.view<const Components::Identity>()) all.push_back(e);
    std::sort(all.begin(), all.end());
    for (entt::entity e : all) {
        mix(static_cast<uint64_t>(entt::to_integral(e)));
        mix(static_cast<uint64_t>(registry.get<Components::Identity>(e).unit_id));
        if (const auto* t = registry.try_get<Components::Transform>(e)) {
            mix(static_cast<uint64_t>(t->position.x.value));
            mix(static_cast<uint64_t>(t->position.y.value));
        }
        if (const auto* h = registry.try_get<Components::Health>(e)) {
            mix(static_cast<uint64_t>(h->current_hp));
        }
        if (const auto* o = registry.try_get<Components::Owner>(e)) mix(o->player_id);
    }
    for (int p = 0; p < Core::kMaxPlayers; ++p) {
        const auto player = static_cast<Core::PlayerId>(p);
        const Players::Player* seat = Players::Find(registry, player);
        if (seat == nullptr) continue;
        for (int r : {Core::Rules::Res::Food, Core::Rules::Res::Wood, Core::Rules::Res::Stone,
                      Core::Rules::Res::Gold}) {
            mix(static_cast<uint64_t>(Players::Resource(*seat, r).value));
        }
        for (int q = 0; q < Core::kMaxPlayers; ++q) {
            mix(static_cast<uint64_t>(Diplomacy::StanceOf(registry, player,
                                                          static_cast<Core::PlayerId>(q))));
        }
        mix(Diplomacy::AlliedVictory(registry, player) ? 1u : 0u);
    }
    for (int r : {Core::Rules::Res::Food, Core::Rules::Res::Wood, Core::Rules::Res::Stone}) {
        mix(static_cast<uint64_t>(Market::BasePrice(registry, r)));
    }
    return hash;
}

} // namespace ECS::Digest
