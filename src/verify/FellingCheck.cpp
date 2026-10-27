#include "verify/Harness.h"

#include <cstdio>
#include <string>

#include "core/Command.h"
#include "core/CommandQueue.h"
#include "core/ConfigManager.h"
#include "core/FixedMath.h"
#include "core/Rules.h"
#include "ecs/Felling.h"
#include "ecs/Simulation.h"
#include "ecs/components/Animation.h"
#include "ecs/components/Felled.h"
#include "ecs/components/Identity.h"
#include "ecs/components/Owner.h"
#include "ecs/components/Resource.h"
#include "ecs/components/Sprite.h"
#include "ecs/components/Transform.h"
#include "ecs/components/Worker.h"

namespace Verify {

bool CheckFelling() {
    ECS::Simulation headless;
    headless.Init();
    auto& reg = headless.GetRegistry();
    std::string verdict = "ok";

    // One of the player's villagers, and the standing tree nearest it.
    entt::entity cutter = entt::null;
    auto villagers = reg.view<ECS::Components::Worker, ECS::Components::Transform, ECS::Components::Owner>();
    for (auto v : villagers) {
        if (villagers.get<ECS::Components::Owner>(v).player_id == Core::PLAYER_LOCAL) {
            cutter = v;
            break;
        }
    }
    entt::entity tree = entt::null;
    if (cutter != entt::null) {
        const auto& at = reg.get<ECS::Components::Transform>(cutter).position;
        Core::Fixed32 nearest{};
        auto stock = reg.view<ECS::Components::Resource, ECS::Components::Transform>();
        for (auto item : stock) {
            if (!ECS::Felling::Standing(reg, item)) continue;
            const auto& p = stock.get<ECS::Components::Transform>(item).position;
            const Core::Fixed32 d = Core::FixedMath::Abs(p.x - at.x) + Core::FixedMath::Abs(p.y - at.y);
            if (tree == entt::null || d < nearest) {
                tree = item;
                nearest = d;
            }
        }
    }

    if (cutter == entt::null || tree == entt::null) {
        verdict = "no villager or no standing tree";
    } else {
        const int wood = reg.get<ECS::Components::Resource>(tree).current_amount;
        Core::CommandQueue::Get().PushCommand(Core::GatherCommand{cutter, tree, Core::PLAYER_LOCAL});
        int felling_ticks = 0;
        int felled_at = -1;
        bool cut = false;
        for (int t = 0; t < 4000 && verdict == "ok" && !cut; ++t) {
            headless.Update(16);
            if (!reg.valid(cutter) || !reg.valid(tree)) {
                verdict = "the villager or the tree is gone";
                break;
            }
            const auto& worker = reg.get<ECS::Components::Worker>(cutter);
            const auto& anim = reg.get<ECS::Components::Animation>(cutter);
            const bool felled = reg.all_of<ECS::Components::Felled>(tree);
            const int left = reg.get<ECS::Components::Resource>(tree).current_amount;
            if (worker.state != Core::UnitState::Gathering) continue;
            if (!felled) {
                // Felling: no wood yet, and the felling stroke on show.
                ++felling_ticks;
                if (left != wood) verdict = "wood cut from a standing tree";
                else if (anim.state != ECS::Components::AnimState::Attack) verdict = "felling without the felling stroke";
                continue;
            }
            if (felled_at < 0) {
                felled_at = t;
                // Down: drawn as its felled graphic.
                const auto* rules = Core::Rules::Unit(reg.get<ECS::Components::Identity>(tree).unit_id);
                const auto art = Core::ConfigManager::Get().GetGraphic(rules != nullptr ? rules->dying_graphic : -1);
                if (!art.Valid() || reg.get<ECS::Components::Sprite>(tree).slp_id != art.slp_id) {
                    verdict = "a felled tree not drawn as its felled graphic";
                }
            }
            if (anim.state == ECS::Components::AnimState::Attack) verdict = "still felling a felled tree";
            if (left < wood) cut = true;
        }
        if (verdict == "ok" && felled_at < 0) verdict = "the tree was never felled";
        if (verdict == "ok" && felling_ticks == 0) verdict = "felled without a stroke";
        if (verdict == "ok" && !cut) verdict = "no wood cut from the felled tree";
    }

    std::printf("felling=%s\n", verdict.c_str());
    return verdict == "ok";
}

} // namespace Verify
