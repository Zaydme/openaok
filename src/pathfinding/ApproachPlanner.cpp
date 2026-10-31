#include "pathfinding/ApproachPlanner.h"

#include "core/FixedMath.h"

namespace Pathfinding {

namespace {

using ECS::Components::Vector2D;

// The edge to stand on: the target's ground pushed out by the standoff.
struct Edge {
    Core::Fixed32 lo_x, lo_y, hi_x, hi_y;
    // How far this outline stands off the ground it surrounds. The wall itself
    // is this much inside each edge.
    Core::Fixed32 pad;

    Core::Fixed32 Width() const { return hi_x - lo_x; }
    Core::Fixed32 Height() const { return hi_y - lo_y; }
    Core::Fixed32 Perimeter() const {
        return (Width() + Height()) * Core::Fixed32(2);
    }
    Vector2D Centre() const {
        return {(lo_x + hi_x) / Core::Fixed32(2),
                (lo_y + hi_y) / Core::Fixed32(2)};
    }

    // A distance clockwise from the top-left corner, as a point.
    Vector2D At(Core::Fixed32 t) const {
        const Core::Fixed32 perimeter = Perimeter();
        while (t.value < 0) t = t + perimeter;
        while (t >= perimeter) t = t - perimeter;

        const Core::Fixed32 w = Width();
        const Core::Fixed32 h = Height();
        if (t < w) return {lo_x + t, lo_y};
        t = t - w;
        if (t < h) return {hi_x, lo_y + t};
        t = t - h;
        if (t < w) return {hi_x - t, hi_y};
        t = t - w;
        return {lo_x, hi_y - t};
    }

    // The stretch of perimeter belonging to the face a point sits on, as the
    // distances of its two ends. Candidates are tried inside this before
    // anywhere else.
    void FaceSpan(Vector2D point, Core::Fixed32& lo, Core::Fixed32& hi) const {
        const Core::Fixed32 w = Width();
        const Core::Fixed32 h = Height();
        const Core::Fixed32 tol = Core::Fixed32(1) / Core::Fixed32(1000);
        if (Core::FixedMath::Abs(point.y - lo_y) <= tol) { lo = Core::Fixed32(0); hi = w; return; }
        if (Core::FixedMath::Abs(point.x - hi_x) <= tol) { lo = w; hi = w + h; return; }
        if (Core::FixedMath::Abs(point.y - hi_y) <= tol) { lo = w + h; hi = w + h + w; return; }
        lo = w + h + w;
        hi = Perimeter();
    }

    // A point on the edge, as a distance clockwise from the top-left corner.
    Core::Fixed32 Parameter(Vector2D point) const {
        const Core::Fixed32 w = Width();
        const Core::Fixed32 h = Height();
        // Which side it is on, by whichever coordinate is pinned to a limit.
        // Compared with a tolerance because the point comes from a ray
        // intersection, so it lands on the limit only to within rounding.
        const Core::Fixed32 tol = Core::Fixed32(1) / Core::Fixed32(1000);
        if (Core::FixedMath::Abs(point.y - lo_y) <= tol) return point.x - lo_x;
        if (Core::FixedMath::Abs(point.x - hi_x) <= tol) return w + (point.y - lo_y);
        if (Core::FixedMath::Abs(point.y - hi_y) <= tol) {
            return w + h + (hi_x - point.x);
        }
        return w + h + w + (hi_y - point.y);
    }

    // Where a caller standing at `from` meets this edge: the face it is in
    // front of, then the point on that face nearest to it.
    Vector2D Facing(Vector2D from, Core::Fixed32 inset) const {
        const Vector2D centre = Centre();
        const Core::Fixed32 dx = from.x - centre.x;
        const Core::Fixed32 dy = from.y - centre.y;
        if (dx.value == 0 && dy.value == 0) return At(Core::Fixed32(0));

        const Core::Fixed32 half_w = Width() / Core::Fixed32(2);
        const Core::Fixed32 half_h = Height() / Core::Fixed32(2);
        const Core::Fixed32 abs_x = Core::FixedMath::Abs(dx);
        const Core::Fixed32 abs_y = Core::FixedMath::Abs(dy);

        // Which face the caller is in front of. Cross-multiplied rather than
        // divided so a zero component needs no special case.
        const bool through_x = abs_x * half_h >= abs_y * half_w;

        // On that face, the point nearest the caller: its own coordinate,
        // clamped to the face. The inset keeps it off the vertices.
        //
        // Clamped to the *wall's* own extent, not the outline's: a spot past a
        // corner of the outline is further from the building than the
        // standoff, and the worker would never count as having arrived.
        const Core::Fixed32 span_w = half_w - pad;
        const Core::Fixed32 span_h = half_h - pad;

        if (through_x) {
            const Core::Fixed32 x = dx.value >= 0 ? centre.x + half_w
                                                  : centre.x - half_w;
            const Core::Fixed32 y = Core::FixedMath::Clamp(
                from.y, centre.y - span_h + inset, centre.y + span_h - inset);
            return {x, y};
        }

        const Core::Fixed32 y = dy.value >= 0 ? centre.y + half_h
                                              : centre.y - half_h;
        const Core::Fixed32 x = Core::FixedMath::Clamp(
            from.x, centre.x - span_w + inset, centre.x + span_w - inset);
        return {x, y};
    }
};

} // namespace

ApproachResult FindStandingSpot(const Physics::Grid& grid,
                                const ApproachRequest& request) {
    const Edge edge{request.target.min_x - request.standoff,
                    request.target.min_y - request.standoff,
                    request.target.max_x + request.standoff,
                    request.target.max_y + request.standoff,
                    request.standoff};

    // Step along the edge by a worker's width, so consecutive candidates are
    // exactly far enough apart to stand side by side.
    const Core::Fixed32 step =
        request.clearance.value > 0 ? request.clearance
                                    : Core::Fixed32(1) / Core::Fixed32(2);
    const Core::Fixed32 perimeter = edge.Perimeter();
    // Enough probes to walk right round the edge and meet in the middle, so a
    // failure really means the whole edge is unusable.
    const int probes =
        static_cast<int>((perimeter / step).ToInt()) + 2;

    // Half a worker's width clear of each corner, so a spot always sits on a
    // face with room beside it.
    const Core::Fixed32 inset = step / Core::Fixed32(2);
    const Vector2D nearest = edge.Facing(request.from, inset);
    const Core::Fixed32 start = edge.Parameter(nearest);

    Core::Fixed32 face_lo(0);
    Core::Fixed32 face_hi(0);
    edge.FaceSpan(nearest, face_lo, face_hi);

    ApproachResult result;
    auto usable = [&](const Vector2D& spot) {
        if (!grid.Passable(request.layer, Physics::TileOf(spot.x), Physics::TileOf(spot.y))) {
            return false;
        }
        for (size_t i = 0; i < request.taken_count; ++i) {
            const Core::Fixed32 gap = Core::FixedMath::Distance(
                spot.x - request.taken[i].x, spot.y - request.taken[i].y);
            if (gap < (request.taken_room != nullptr ? request.taken_room[i] : request.clearance)) {
                result.full = true;
                return false;
            }
        }
        return true;
    };


    // Two passes. The first stays on the face the caller is standing in front
    // of, so a crowd fills that wall before anyone is sent elsewhere; only when
    // it is full does the second pass walk the rest of the perimeter.
    for (int pass = 0; pass < 2; ++pass) {
        // On the caller's own face the search is fine-grained, so a worker
        // can tuck in exactly one clearance from its neighbour; past it,
        // whole worker-widths are enough.
        const Core::Fixed32 grain =
            (pass == 0) ? step / Core::Fixed32(4) : step;
        const int limit = (pass == 0)
            ? static_cast<int>((perimeter / grain).ToInt()) + 2 : probes;

        for (int probe = 0; probe <= limit; ++probe) {
            // 0, +1, -1, +2, -2, ... outward from the nearest point, so the
            // answer is always the closest usable spot to the caller.
            const Core::Fixed32 offset = grain * Core::Fixed32((probe + 1) / 2);
            const Core::Fixed32 t =
                (probe % 2 == 0) ? start + offset : start - offset;
            if (pass == 0 && (t < face_lo || t > face_hi)) continue;

            const Vector2D spot = edge.At(t);
            if (!usable(spot)) continue;

            result.found = true;
            result.full = false;
            result.spot = spot;
            return result;
        }
    }
    return result;
}

} // namespace Pathfinding
