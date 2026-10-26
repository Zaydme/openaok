#pragma once
#include <string>

namespace HAL {
class Window;
}

// The start-up movies: GAME/AVI's MSLOGO (Microsoft), ESLOGO (Ensemble
// Studios) and AGE2 (the intro).
//
// - They play at start-up, one after another, before the main menu and its
//   music, and nowhere else: the menus, history and credits have no movie.
// - A left or right button pressed anywhere, Enter or Space ends the movie
//   showing and starts the next; Escape ends them all. Held keys repeat, and
//   repeats skip too. There is no pointer.
// - Each 320x240 picture is shown at twice its size, 640x480, centred on
//   black. Full screen, the display is switched to 640x480, so the movie
//   fills it; in a window it sits in the middle of the game's screen.
// - The movie's end is noticed by a poll once a second from its start, so
//   its last picture stands until the next whole second.
// - Losing the focus pauses it and regaining it resumes it.
// - A movie whose file is missing is passed over as if it had played.
// - --no-movies skips them all.
namespace Screens::Movies {

enum class Outcome {
    Finished,   // played, or skipped through, to the menus
    Quit,       // the window was closed
};

// The start-up sequence, or the one movie `only` names (mslogo, eslogo,
// age2) when it is not empty.
Outcome PlayStartup(HAL::Window& window, const std::string& only);

// The movie screen as it stands at picture `frame` of `name`,
// drawn into the window's frame and not presented. False if the movie
// cannot be read to that frame.
bool DrawStill(HAL::Window& window, const std::string& name, int frame);

} // namespace Screens::Movies
