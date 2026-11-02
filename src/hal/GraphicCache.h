#pragma once
#include <algorithm>
#include <array>
#include <SDL2/SDL.h>
#include <cstdint>
#include <deque>
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

// Every frame of one graphic packed into one block, so picking a frame is
// integer rect arithmetic. An interface or terrain graphic's block is a
// texture of its own, `base`; a world graphic's art lies on shared pages
// instead (GraphicCache::Art), its `base` null.
struct GraphicAtlas {
    SDL_Texture* base = nullptr;   // colour, with player-colour pixels left out
    std::vector<GraphicFrame> frames;
    int width = 0;
    int height = 0;
    bool valid = false;
    // Whether any frame has player-coloured pixels.
    bool coloured = false;
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

// One band of a graphic too tall for any texture the renderer allows: its
// shelves from atlas row `top` down, on `page`, their rects there moved by
// (x, y).
struct ArtBand {
    int top = 0;
    SDL_Texture* page = nullptr;
    int x = 0;
    int y = 0;
};

// Where one graphic's frames lie on a texture page: a frame's rect there is
// its atlas rect moved by (x, y). Graphics share pages, so sprites of
// different graphics drawn one after another are drawn in one batch. A
// graphic taller than a page may be lies in bands, each placed as a block of
// its own; `page` is then its first band's, and a frame's page is PageOf it.
struct ArtPlace {
    SDL_Texture* page = nullptr;
    int x = 0;
    int y = 0;
    const std::vector<ArtBand>* bands = nullptr;
    SDL_Rect On(const SDL_Rect& r) const {
        const ArtBand* band = BandOf(r);
        return band != nullptr ? SDL_Rect{r.x + band->x, r.y + band->y, r.w, r.h}
                               : SDL_Rect{r.x + x, r.y + y, r.w, r.h};
    }
    SDL_Texture* PageOf(const SDL_Rect& r) const {
        const ArtBand* band = BandOf(r);
        return band != nullptr ? band->page : page;
    }
    const ArtBand* BandOf(const SDL_Rect& r) const {
        if (bands == nullptr) return nullptr;
        const ArtBand* in = nullptr;
        for (const ArtBand& band : *bands) {
            if (band.top <= r.y) in = &band;
        }
        return in;
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

    // A world graphic's geometry -- frames, hotspots, where it is opaque --
    // built on first use. Null if the SLP is absent or fails to decode. Its
    // pixels are Art's, Shadow's and Outline's.
    const GraphicAtlas* GetAtlas(uint32_t slp_id);

    // A graphic's art in `player`'s colours: its colour pixels, and its
    // player-coloured ones resolved through the ramp of the colour the player
    // wears, on a shared page. One for every colour alike when the graphic
    // has no player-coloured pixels. No page when it draws nothing.
    ArtPlace Art(uint32_t slp_id, int player);

    // Frames marked as shadow pixels, black with alpha, on a page of shadows
    // only: every shadow draws at the shadow pass's alpha. Used for the
    // graphics that carry their shadow inline rather than in a companion SLP.
    ArtPlace Shadow(uint32_t slp_id);

    // Frames marked as outline pixels, white, on a page of outlines only, for
    // a colour mod to tint per player as each is drawn: the silhouette drawn
    // where a unit is hidden behind a building or tree.
    ArtPlace Outline(uint32_t slp_id);

    // How many textures the cache holds, and their pixels' size in bytes.
    void Count(int& textures, int64_t& bytes) const;

    // Frees the textures of pages grown into bigger ones, once the frame that
    // may still have drawn from them is shown: after every present.
    void EndFrame();

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

    // Takes no texture with a side over `side`, as a renderer allowing no
    // more would: for checks, before any art is placed.
    void LimitTextureSide(int side);

private:
    GraphicCache() = default;
    ~GraphicCache() { Shutdown(); }
    GraphicCache(const GraphicCache&) = delete;
    GraphicCache& operator=(const GraphicCache&) = delete;

    // Which pixel class a layer draws: Base the colour pixels, Player the
    // player-coloured ones, Art both, Shadow and Outline their own; None
    // nothing, for an atlas's geometry alone.
    enum class Layer { Base, Player, Art, Shadow, Outline, None };

    // Packs a graphic of any archive into the given cache: its geometry, and
    // with `texture` its Base layer as a texture of its own.
    template <typename Key>
    const GraphicAtlas* AtlasFrom(const DrsArchive& src,
                                  std::unordered_map<Key, GraphicAtlas>& cache,
                                  Key key, uint32_t slp_id,
                                  const std::vector<PaletteColor>& colours, bool texture);
    // One layer of a graphic laid out as its atlas lays out its frames, with
    // `border` clear pixels all round; null when it has no such pixels.
    // `opaque` receives the atlas's coverage bits, and `coloured` whether
    // any pixel is player-coloured.
    SDL_Surface* DrawLayer(const DrsArchive& src, uint32_t slp_id, const GraphicAtlas& geometry,
                           Layer layer, int player, const std::vector<PaletteColor>& colours,
                           int border, std::vector<uint8_t>* opaque = nullptr,
                           bool* coloured = nullptr);
    // A surface as a texture of its own; frees the surface. Null for none.
    SDL_Texture* TextureOf(SDL_Surface* surface);
    // A layer as a texture of its own.
    SDL_Texture* BuildLayer(const DrsArchive& src, uint32_t slp_id, const GraphicAtlas& geometry,
                            Layer layer, int player, const std::vector<PaletteColor>& colours);
    size_t PaletteIndexForPlayer(int player, uint8_t shade) const;

    // The page kinds: art, shadows and outlines each draw with mods of their
    // own, so they never share a texture.
    enum class Pool { Art, Shadow, Outline, Count };
    // A stretch of a page's skyline: from x, w wide, filled down to y.
    struct Span {
        int x, y, w;
    };
    struct Page {
        SDL_Texture* texture = nullptr;
        int w = 0;
        int h = 0;
        std::vector<Span> skyline;
    };
    // Room for a w x h block on the page, its place in `at`; the skyline
    // raised over it.
    static bool Fit(Page& page, int w, int h, SDL_Rect& at);
    // A page grown to twice its width, or once wider than high to twice its
    // height, no side past `most`: what it held copied to its top left and
    // everything placed on it moved to the new texture. False when it cannot
    // grow.
    bool Grow(Page& page, int most);
    // A block's pixels put on a page of the pool with room for it, or on a
    // new one; a block bigger than a page on a texture of its own. Frees the
    // surface.
    ArtPlace Place(Pool pool, SDL_Surface* block, int border);
    // A block taller than the pool's pages may be put on them in bands of
    // whole shelves of `geometry`, each no taller than `most`. Frees the
    // surface.
    ArtPlace PlaceBands(Pool pool, SDL_Surface* block, const GraphicAtlas& geometry, int most);
    ArtPlace LayerPlace(Pool pool, uint32_t slp_id, Layer layer, int player);
    // The page sides for a renderer allowing textures `most` on a side.
    void Sides(int most);

    SDL_Renderer* sdl_renderer = nullptr;
    DrsArchive drs;
    std::vector<PaletteColor> palette;
    std::vector<int> player_ramps;
    std::array<int, 16> colour_slots{-1, -1, -1, -1, -1, -1, -1, -1,
                                     -1, -1, -1, -1, -1, -1, -1, -1};
    bool ready = false;

    std::unordered_map<uint32_t, GraphicAtlas> atlases;
    // Keyed by slp_id << 8 | the colour slot worn + 1, or 0 for a graphic
    // with no player-coloured pixels, so each colour's art is built once.
    std::unordered_map<uint64_t, ArtPlace> arts;
    std::unordered_map<uint32_t, ArtPlace> shadows;
    std::unordered_map<uint32_t, ArtPlace> outlines;
    std::array<std::vector<Page>, static_cast<size_t>(Pool::Count)> pages;
    // Blocks too big for a page, each on a texture of its own.
    std::vector<SDL_Texture*> lone;
    // The bands of graphics placed in bands, which their places point into.
    std::deque<std::vector<ArtBand>> banded;
    // Pages grown out of, for EndFrame.
    std::vector<SDL_Texture*> retired;
    int first_side = 0;     // a new page's
    int page_side = 0;      // a shadow or outline page's, grown
    int largest_side = 0;   // an art page's, grown
    int most_side = 0;      // any texture's: the renderer's limit
    bool grows = false;

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
