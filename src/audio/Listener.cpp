#include "audio/Listener.h"

#include <algorithm>
#include <cmath>

#include "core/IsometricMath.h"

namespace Audio {

namespace {

// How far past the edge of the view a sound still carries, as a fraction of
// half the view, and how much softer the edge is than the middle.
constexpr float kCarry = 0.4f;
constexpr float kEdgeSoftening = 0.35f;
// Stereo spread: a source at the very edge is not wholly in one ear.
constexpr float kPanWidth = 0.75f;

} // namespace

Placement Place(const Listener& listener, Core::Fixed32 x, Core::Fixed32 y) {
    const SDL_FPoint at = Core::IsometricMath::WorldToScreen(x, y);
    const float half_w = listener.width * 0.5f;
    const float half_h = listener.height * 0.5f;
    const float dx = (at.x - (listener.left + half_w)) / half_w;
    const float dy = (at.y - (listener.top + half_h)) / half_h;
    const float reach = std::max(std::fabs(dx), std::fabs(dy));

    Placement placement;
    if (reach <= 1.0f) {
        placement.volume = 1.0f - kEdgeSoftening * reach;
    } else if (reach < 1.0f + kCarry) {
        placement.volume = (1.0f - kEdgeSoftening) * (1.0f + kCarry - reach) / kCarry;
    }
    placement.pan = std::clamp(dx, -1.0f, 1.0f) * kPanWidth;
    return placement;
}

} // namespace Audio
