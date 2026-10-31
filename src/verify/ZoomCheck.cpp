#include "verify/Harness.h"

#include <cmath>
#include <cstdio>

#include "rendering/Renderer.h"

namespace Verify {

namespace {

// The world point (in unzoomed screen pixels) under the frame point (x, y).
void Under(const Rendering::Renderer& r, float x, float y, bool target, float& wx, float& wy) {
    const float zoom = target ? r.target_camera_zoom : r.camera_zoom;
    wx = (target ? r.target_camera_x : r.camera_x) + x / zoom;
    wy = (target ? r.target_camera_y : r.camera_y) + y / zoom;
}

bool Near(float a, float b) { return std::fabs(a - b) < 0.01f; }

} // namespace

bool CheckZoom() {
    const char* verdict = nullptr;
    Rendering::Renderer r;
    r.camera_x = r.target_camera_x = 1000.0f;
    r.camera_y = r.target_camera_y = 500.0f;
    const float x = 300.0f, y = 200.0f;
    float before_x = 0.0f, before_y = 0.0f;
    Under(r, x, y, true, before_x, before_y);

    // A step out, eased: the point under the pointer stays under it.
    r.ZoomBy(1.0f / Rendering::Renderer::kZoomStep, x, y, false);
    float after_x = 0.0f, after_y = 0.0f;
    Under(r, x, y, true, after_x, after_y);
    if (!Near(r.target_camera_zoom, 1.0f / Rendering::Renderer::kZoomStep) || r.camera_zoom != 1.0f) {
        verdict = "an eased step did not set the zoom to come";
    } else if (!Near(before_x, after_x) || !Near(before_y, after_y)) {
        verdict = "zooming moved the point under the pointer";
    }

    // All the way out and in: no further than the limits.
    if (verdict == nullptr) {
        for (int i = 0; i < 40; ++i) r.ZoomBy(0.5f, x, y, false);
        if (r.target_camera_zoom != Rendering::Renderer::kZoomOut) verdict = "the view zoomed out past its limit";
        for (int i = 0; i < 40; ++i) r.ZoomBy(2.0f, x, y, false);
        if (verdict == nullptr && r.target_camera_zoom != Rendering::Renderer::kZoomIn) {
            verdict = "the view zoomed in past its limit";
        }
    }

    // A pinch at once: the view itself, not only the zoom to come.
    if (verdict == nullptr) {
        Rendering::Renderer p;
        Under(p, x, y, false, before_x, before_y);
        p.ZoomBy(0.8f, x, y, true);
        Under(p, x, y, false, after_x, after_y);
        if (!Near(p.camera_zoom, 0.8f) || !Near(before_x, after_x) || !Near(before_y, after_y)) {
            verdict = "a pinch did not zoom the view at once about the fingers";
        }
    }
    std::printf("zoom=%s\n", verdict == nullptr ? "ok" : verdict);
    return verdict == nullptr;
}

} // namespace Verify
