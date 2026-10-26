#pragma once
#include <SDL2/SDL.h>
#include <cstdint>
#include <string>

// The narrated slides before a campaign scenario, and after one won (the
// "Multimedia Screen"), and the custom show played once before William
// Wallace's first scenario (INTRO.MM). Everything comes from CAMPAIGN/MEDIA:
//
// - c<N>s<S>_beg.mm / _end.mm, a script of text lines, one item a line: its
//   id, TEXT, PICT, SND or WND, a rectangle, a start and a display time in
//   milliseconds, a frame (a picture's frame of c<N>s<S>_beg.slp, a text's
//   font), the text in quotes and its colour. A line whose first word is
//   not a number (the header, "rem") is passed over.
// - backgrd<N>.sin, .slp and .pal: the screen's art and its palette.
// - cam<N>.bln: twenty 256x256 tables, the palette's colours mixed at twenty
//   strengths: how a picture or a text fades in over the art and out again
//   in 256 colours.
// - SOUND/CAMPAIGN/c<N>s<S>.mp3 (c<N>s<S>end.mp3), the narration, named by
//   the script's SND line.
//
// A picture or a text fades in over 400 ms from its start, in steps of a
// tenth; 500 ms before its start and display time are up it fades out over
// the same (the script reader takes 100 and the fade off the display time).
// The show ends at its WND line's time, or when the player presses a mouse
// button, Escape or Space. Its files missing, there is no show. The intro
// fades over 500 ms and each of its full-screen pictures replaces the last
// (the screen's mode 2).
//
// One show at a time, composed in palette indexes (the art, the pictures and
// text drawn over it, the screen) and turned into colour only to be shown.
namespace Screens::Slides {

struct Show {
    std::string name;      // the script's name without ".mm": "c1s1_beg", "intro"
    int campaign = 0;      // its art's number: backgrd<N>, cam<N>.bln; 0 for a custom show
};

// The show of campaign `campaign` (1-8) scenario `scenario` (from 1): its
// beginning, or its end after a win.
Show OfScenario(int campaign, int scenario, bool beginning);
// A named show of its own art: "intro".
Show Custom(const std::string& name);

// Loads the show and starts its clock at 0. False, and nothing to play,
// when its files cannot be read; the caller then goes straight on. `sound`
// false keeps it silent. The renderer sets the texts.
bool Start(const Show& show, bool sound, SDL_Renderer* r);
// The show as it stands `ms` into it: the script's items started, faded and
// ended, the screen composed. Called with a rising clock.
void Advance(uint32_t ms);
// A mouse button, Escape or Space: the show ends.
void Skip();
bool Done();
// The screen at 800x600.
void Draw(SDL_Renderer* r);
// Lets the show go: its narration stops.
void Stop();

// The intro plays before William Wallace's first scenario once each time a
// campaign's map has been opened: opening a map arms it (ArmIntro) and
// taking it to play disarms it (TakeIntro).
void ArmIntro();
bool TakeIntro();

} // namespace Screens::Slides
