#pragma once
#include <cstdint>
#include <string>
#include <vector>

// The art and sound the rules refer to by id, from media.json.
//
// A graphic can be a composite: a list of deltas, each another graphic drawn
// at a pixel offset: a burning building's damage graphic is a dozen flame
// graphics placed over the walls, and the rules only name the composite. The
// sound table maps a sound id to the WAVs
// it may play, each with a probability, some only for one civilisation.
namespace Core::Media {

struct Delta {
    int graphic = -1;
    int offset_x = 0;   // screen pixels from the owner's anchor
    int offset_y = 0;
    int angle = -1;     // the facing it applies to; -1 for every facing
};

// A sound tied to one frame of a graphic: a sword's ring on the frame the
// blow lands, an axe's chop on the frame it bites. The dat keeps up to three
// per facing.
struct FrameSound {
    int frame = -1;
    int sound = -1;
};

constexpr int kFrameSoundsPerAngle = 3;

struct AngleSounds {
    FrameSound sounds[kFrameSoundsPerAngle];
};

struct SoundItem {
    int resource = -1;  // file id in sounds.drs
    int probability = 100;
    int civ = -1;       // -1: every civilisation
};

struct Sound {
    bool valid = false;
    int delay = 0;
    int total = 100;
    std::vector<SoundItem> items;
};

bool Load(const std::string& path);

// The graphic's deltas; empty when it is not a composite.
const std::vector<Delta>& Deltas(int graphic_id);

// The sound a graphic plays as it starts, or -1.
int GraphicSound(int graphic_id);

// The sounds a graphic plays on particular frames, one entry per facing;
// empty when it has none.
const std::vector<AngleSounds>& FrameSounds(int graphic_id);

// A sound table entry, or null.
const Sound* SoundById(int sound_id);

} // namespace Core::Media
