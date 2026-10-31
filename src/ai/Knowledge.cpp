#include "ai/Knowledge.h"

#include "ai/Brain.h"
#include "ecs/Diplomacy.h"
#include "ecs/Vision.h"

namespace AI::Knowledge {

namespace {

// The computer players who tell this one what they find.
template <class F>
bool AnySharing(const Turn& turn, F&& knows) {
    if (turn.brains == nullptr || turn.registry == nullptr) return false;
    for (const Brain& b : turn.brains->brains) {
        if (b.player == turn.player) continue;
        if (b.mind.sn[static_cast<size_t>(Lang::SN::CoopShareInformation)] != 1) continue;
        if (ECS::Diplomacy::StanceOf(*turn.registry, b.player, turn.player) != ECS::Diplomacy::Stance::Ally ||
            ECS::Diplomacy::StanceOf(*turn.registry, turn.player, b.player) != ECS::Diplomacy::Stance::Ally) {
            continue;
        }
        if (knows(b.player)) return true;
    }
    return false;
}

} // namespace

bool Sees(const Turn& turn, entt::entity e) {
    const entt::registry& registry = *turn.registry;
    if (ECS::Vision::Shows(registry, turn.player, e)) return true;
    return AnySharing(turn, [&](Core::PlayerId p) { return ECS::Vision::Shows(registry, p, e); });
}

bool Explored(const Turn& turn, int x, int y) {
    const entt::registry& registry = *turn.registry;
    if (ECS::Vision::Explored(registry, turn.player, x, y)) return true;
    return AnySharing(turn, [&](Core::PlayerId p) { return ECS::Vision::Explored(registry, p, x, y); });
}

} // namespace AI::Knowledge
