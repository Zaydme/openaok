#pragma once
#include <algorithm>
#include <array>
#include <SDL2/SDL.h>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "SlpFile.h"

namespace HAL {

// Where one frame lives inside its graphic's atlas, plus the hotspot that
// anchors it on a tile.
struct GraphicFrame {
    SDL_Rect rect{0, 0, 0, 0};
    int hotspot_x = 0;
    int hotspot_y = 0;
};

// Every frame of one graphic packed into a single texture, so units in
// different poses batch and picking a frame is integer rect arithmetic.
struct GraphicAtlas {
    SDL_Texture* base = nullptr;   // colour, with player-colour pixels left out
    std::vector<GraphicFrame> frames;
    int width = 0;
    int height = 0;
    bool valid = false;
    // One bit per atlas pixel, set where the art is opaque: what a click
    // lands on, and whether a tree or building actually covers a unit.
    std::vector<uint8_t> opaque;

    bool Covers(int frame, int fx, int fy) const {
        if (opaque.empty() || frame < 0 || frame >= static_cast<int>(frames.size())) {
            return false;
        }
        const SDL_Rect& r = frames[frame].rect;
        if (fx < 0 || fy < 0 || fx >= r.w || fy >= r.h) return false;
        const size_t bit = static_cast<size_t>(r.y + fy) * static_cast<size_t>(width) +
                           static_cast<size_t>(r.x + fx);
        return (opaque[bit >> 3] >> (bit & 7)) & 1;
    }

    // Whether the art is opaque within `margin` pixels of the point, or the
    // point lies between opaque pixels on its row: on a unit, a few pixels
    // round it, and the gaps inside its outline.
    bool Holds(int frame, int fx, int fy, int margin) const {
        if (opaque.empty() || frame < 0 || frame >= static_cast<int>(frames.size())) {
            return false;
        }
        const SDL_Rect& r = frames[frame].rect;
        for (int y = std::max(0, fy - margin); y <= std::min(r.h - 1, fy + margin); ++y) {
            for (int x = std::max(0, fx - margin); x <= std::min(r.w - 1, fx + margin); ++x) {
                if (Covers(frame, x, y)) return true;
            }
        }
        if (fy < 0 || fy >= r.h || fx < 0 || fx >= r.w) return false;
        bool left = false;
        for (int x = 0; x < fx && !left; ++x) left = Covers(frame, x, fy);
        if (!left) return false;
        for (int x = fx + 1; x < r.w; ++x) {
            if (Covers(frame, x, fy)) return true;
        }
        return false;
    }
};

// Decodes SLPs out of the DRS archives and packs them into atlases.
class GraphicCache {
public:
    static GraphicCache& Get() {
        static GraphicCache instance;
        return instance;
    }

    // Loads the archive and palette. False if either cannot be read.
    bool Init(SDL_Renderer* renderer,
              const std::string& drs_path,
              const std::string& palette_path);

    // The interface art lives in its own archive but is packed exactly like
    // unit art: the panels are single huge frames, while the icon sheets hold a
    // hundred 36x36 frames and carry player-coloured pixels of their own, so a
    // unit's button shows the owner's colour the way its sprite does.
    //
    // The in-game interface is drawn in the game's own palette; the menus each
    // name their own in their .sin file, so an atlas is built per art and
    // palette.
    static constexpr uint32_t kGamePalette = 50500;
    bool OpenInterface(const std::string& drs_path);
    const GraphicAtlas* GetInterfaceAtlas(uint32_t slp_id, uint32_t palette_id = kGamePalette);
    SDL_Texture* GetInterfacePlayerLayer(uint32_t slp_id, int player);
    // Convenience for the single-frame panel art.
    SDL_Texture* GetInterfaceTexture(uint32_t slp_id, uint32_t palette_id = kGamePalette);
    // A palette from the interface archive by file id: the game's own when the
    // archive has no such file.
    const std::vector<PaletteColor>& InterfacePalette(uint32_t palette_id);
    // The same palette darkened by a percentage, each colour snapped to the
    // palette's own nearest, so a button's shaded inside is made of palette
    // colours. Usable as a palette id anywhere one is taken.
    static constexpr uint32_t ShadedPalette(uint32_t palette_id, int percent) {
        return palette_id | (static_cast<uint32_t>(percent) << 24);
    }
    bool InterfaceReady() const { return interface_ready; }
    // The archive itself, for the interface's sounds, which live in it too.
    const DrsArchive* InterfaceArchive() const {
        return interface_ready ? &interface_drs : nullptr;
    }

    // Terrain art is a third archive of its own. Each SLP is one ground type
    // holding a square tiling pattern of 97x49 diamonds -- up to 10x10 of them
    // -- so an atlas per terrain lets a whole chunk bake from one texture.
    bool OpenTerrain(const std::string& drs_path);
    const GraphicAtlas* GetTerrainAtlas(uint32_t slp_id);
    bool TerrainReady() const { return terrain_ready; }

    // gamedata.drs is text, not art: the random map scripts and the AI scripts.
    // It lives here because this is where the DRS archives are held open.
    bool OpenGameData(const std::string& drs_path);
    bool ReadGameDataText(uint32_t file_id, std::string& out) const;
    bool GameDataReady() const { return gamedata_ready; }
    void Shutdown();

    bool Ready() const { return ready; }

    // Atlas for one graphic, built on first use. Null if the SLP is absent or
    // fails to decode.
    const GraphicAtlas* GetAtlas(uint32_t slp_id);

    // Player-coloured pixels of one graphic, resolved through that player's
    // 16-entry palette ramp and packed to the same rects as the base atlas.
    // Drawn over the base to tint a unit without touching its other pixels.
    SDL_Texture* GetPlayerLayer(uint32_t slp_id, int player);

    // Frames marked as shadow pixels, black with alpha. Used for the graphics
    // that carry their shadow inline rather than in a companion SLP.
    SDL_Texture* GetShadowLayer(uint32_t slp_id);

    // Frames marked as outline pixels, white so a colour mod can tint them:
    // the silhouette drawn where a unit is hidden behind a building or tree.
    SDL_Texture* GetOutlineLayer(uint32_t slp_id);

    // How many textures the cache holds, and their pixels' size in bytes.
    void Count(int& textures, int64_t& bytes) const;

    // A player's colour, taken from the middle of their 16-entry palette ramp.
    PaletteColor GetPlayerColor(int player) const;

    // One entry of the game's 256-colour palette. The dat stores minimap
    // colours as indexes into it rather than as RGB.
    PaletteColor GetPaletteColor(size_t index) const;

    // Where each player's eight-entry colour ramp starts in the palette, in
    // player order. Supplied from the dat: the ramps are not evenly spaced.
    void SetPlayerRamps(const std::vector<int>& bases);

    // The colour each player wears, 0-7, by player number; -1 for the seat's
    // own (player n wears colour n - 1). Gaia wears grey.
    void SetPlayerColours(const std::array<int, 16>& slots);
    int ColourSlot(int player) const;

    size_t AtlasCount() const { return atlases.size(); }

private:
    GraphicCache() = default;
    ~GraphicCache() { Shutdown(); }
    GraphicCache(const GraphicCache&) = delete;
    GraphicCache& operator=(const GraphicCache&) = delete;

    // Which pixel class a layer draws.
    enum class Layer { Base, Player, Shadow, Outline };

    // Packs a graphic of any archive into the given cache.
    template <typename Key>
    const GraphicAtlas* AtlasFrom(const DrsArchive& src,
                                  std::unordered_map<Key, GraphicAtlas>& cache,
                                  Key key, uint32_t slp_id,
                                  const std::vector<PaletteColor>& colours);
    // `opaque`, for the base layer, receives the atlas's coverage bits.
    SDL_Texture* BuildLayer(const DrsArchive& src, uint32_t slp_id,
                            Layer layer, int player,
                            const std::vector<PaletteColor>& colours,
                            std::vector<uint8_t>* opaque = nullptr);
    size_t PaletteIndexForPlayer(int player, uint8_t shade) const;

    SDL_Renderer* sdl_renderer = nullptr;
    DrsArchive drs;
    std::vector<PaletteColor> palette;
    std::vector<int> player_ramps;
    std::array<int, 16> colour_slots{-1, -1, -1, -1, -1, -1, -1, -1,
                                     -1, -1, -1, -1, -1, -1, -1, -1};
    bool ready = false;

    std::unordered_map<uint32_t, GraphicAtlas> atlases;
    // Keyed by slp_id << 8 | player, so each player's tint is built once.
    std::unordered_map<uint64_t, SDL_Texture*> player_layers;
    std::unordered_map<uint32_t, SDL_Texture*> shadow_layers;
    std::unordered_map<uint32_t, SDL_Texture*> outline_layers;

    DrsArchive interface_drs;
    bool interface_ready = false;
    // Keyed by palette id in the high half and art id in the low.
    std::unordered_map<uint64_t, GraphicAtlas> interface_atlases;
    std::unordered_map<uint32_t, std::vector<PaletteColor>> interface_palettes;
    std::unordered_map<uint64_t, SDL_Texture*> interface_player_layers;

    DrsArchive terrain_drs;
    bool terrain_ready = false;
    std::unordered_map<uint32_t, GraphicAtlas> terrain_atlases;

    DrsArchive gamedata_drs;
    bool gamedata_ready = false;
};

} // namespace HAL
