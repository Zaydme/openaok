#pragma once

// Which WAV a dat sound plays.
//
// A sound in the dat is not a file but a list of them, each with a chance and
// most of them for one civilisation only: a villager's "move" is three generic
// voice lines and four more per civ, so a Teuton answers in German. The pick is
// presentation, made from this module's own generator, never the simulation's.
namespace Audio::SoundTable {

// The archive file for dat sound `sound_id` as heard by civilisation `civ`:
// one of that civ's own items if it has any, else one of the generic ones,
// chosen by their probabilities. -1 if the sound has nothing to play.
int Pick(int sound_id, int civ);

} // namespace Audio::SoundTable
