#include "verify/Harness.h"

#include <SDL2/SDL.h>

#include <cstdio>

#include "rendering/UIRenderer.h"

namespace Verify {

// The rollover help's box is as tall as its lines: a help of two lines has a
// box three lines shorter than one of five, both standing on the same bottom,
// at each of the original's sizes.
bool CheckHelpBox() {
    const char* verdict = nullptr;
    static const SDL_Point kFrames[3] = {{800, 600}, {1024, 768}, {1280, 1024}};
    for (const SDL_Point& frame : kFrames) {
        const SDL_Rect none = Rendering::UIRenderer::HelpBox(0, frame.x, frame.y);
        const SDL_Rect one = Rendering::UIRenderer::HelpBox(1, frame.x, frame.y);
        const SDL_Rect two = Rendering::UIRenderer::HelpBox(2, frame.x, frame.y);
        const SDL_Rect five = Rendering::UIRenderer::HelpBox(5, frame.x, frame.y);
        const int line = one.h - none.h;
        if (line <= 0 || two.h != none.h + 2 * line || five.h != two.h + 3 * line) {
            verdict = "the help's box is not as tall as its lines";
        } else if (two.y + two.h != five.y + five.h || two.x != five.x || two.w != five.w) {
            verdict = "the help's box moved its bottom or sides with its lines";
        }
        if (verdict != nullptr) break;
    }
    std::printf("helpbox=%s\n", verdict == nullptr ? "ok" : verdict);
    return verdict == nullptr;
}

} // namespace Verify
