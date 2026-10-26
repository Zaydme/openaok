#pragma once

// The music, streamed from the disc's GAME/SOUND/STREAM: OPEN at the menus,
// a civilisation's theme when it is chosen in the game setup screen (RANDOM
// for a random civ), CREDITS over the credits, WON1 or LOST when a game ends
// -- and in a game, the playlist MUSIC1.MP3, MUSIC2.MP3, ... The disc's data
// carries no playlist files, so a game plays none unless they are put there.
//
// Presentation only: nothing here reaches the simulation.
namespace Audio::Music {

void Menus();
void Credits();
void Game();
void Ended(bool won);
// The theme of dat civ `civ`, 0 for a random civ.
void Civ(int civ);

// The Options screen's music volume, 0 (off) to 10.
void SetVolume(int volume);

// Once a frame: keeps the track decoding, and moves the playlist on.
void Update();

} // namespace Audio::Music
