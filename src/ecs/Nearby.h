#pragma once

#include <algorithm>
#include <cstdint>
#include <vector>

#include <entt/entt.hpp>

#include "core/FixedPoint.h"
#include "ecs/components/Transform.h"
#include "physics/Grid.h"

namespace ECS::Nearby {

// The entities of a view sorted into square buckets of the map by where they
// stand, so that what stands within reach of a point is found by visiting a
// few buckets rather than every entity. Built in one pass where it is used,
// so it holds the positions of that moment; anything off the map goes in the
// nearest edge bucket.
struct Buckets {
    static constexpr int kTiles = 8;   // tiles along a bucket's side
    int columns = 0;
    int rows = 0;
    std::vector<uint32_t> start;       // per bucket, where its entities begin; one past the last
    std::vector<entt::entity> entities;
    std::vector<uint32_t> bucket_of;   // scratch: each entity's bucket, in the view's order
    std::vector<entt::entity> taken;   // scratch: the entities taken, in the view's order
    std::vector<uint32_t> next;        // scratch: where each bucket's next entity goes
};

inline int BucketCoordinate(Core::Fixed32 at, int count) {
    return std::clamp(Physics::TileOf(at) / Buckets::kTiles, 0, count - 1);
}

// Sorts the entities of `view`, which has a Transform, that `take(entity)`
// takes into `buckets` for a map of `width` by `height` tiles.
template <typename View, typename Take>
void Build(Buckets& buckets, const View& view, int width, int height, Take&& take) {
    buckets.columns = std::max(1, (width + Buckets::kTiles - 1) / Buckets::kTiles);
    buckets.rows = std::max(1, (height + Buckets::kTiles - 1) / Buckets::kTiles);
    const size_t count = static_cast<size_t>(buckets.columns) * static_cast<size_t>(buckets.rows);
    buckets.start.assign(count + 1, 0);
    buckets.bucket_of.clear();
    buckets.taken.clear();
    for (auto e : view) {
        if (!take(e)) continue;
        const auto& at = view.template get<const Components::Transform>(e).position;
        const uint32_t b = static_cast<uint32_t>(BucketCoordinate(at.y, buckets.rows) * buckets.columns +
                                                 BucketCoordinate(at.x, buckets.columns));
        buckets.bucket_of.push_back(b);
        buckets.taken.push_back(e);
        ++buckets.start[b + 1];
    }
    for (size_t b = 0; b < count; ++b) buckets.start[b + 1] += buckets.start[b];
    buckets.entities.resize(buckets.taken.size());
    buckets.next.assign(buckets.start.begin(), buckets.start.end() - 1);
    for (size_t i = 0; i < buckets.taken.size(); ++i) {
        buckets.entities[buckets.next[buckets.bucket_of[i]]++] = buckets.taken[i];
    }
}

// The same with every entity of the view.
template <typename View>
void Build(Buckets& buckets, const View& view, int width, int height) {
    Build(buckets, view, width, height, [](entt::entity) { return true; });
}

// Calls `visit(entity)` for every entity in the buckets that overlap the
// square of half-side `reach` around `at`: a superset of those within it.
template <typename Visit>
void ForEachNear(const Buckets& buckets, Components::Vector2D at, Core::Fixed32 reach, Visit&& visit) {
    if (buckets.start.empty()) return;
    const int x0 = BucketCoordinate(at.x - reach, buckets.columns);
    const int x1 = BucketCoordinate(at.x + reach, buckets.columns);
    const int y0 = BucketCoordinate(at.y - reach, buckets.rows);
    const int y1 = BucketCoordinate(at.y + reach, buckets.rows);
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            const size_t b = static_cast<size_t>(y * buckets.columns + x);
            for (uint32_t i = buckets.start[b]; i < buckets.start[b + 1]; ++i) visit(buckets.entities[i]);
        }
    }
}

} // namespace ECS::Nearby
