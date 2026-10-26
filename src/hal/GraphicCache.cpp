#include "GraphicCache.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <iostream>

namespace HAL {

namespace {

// Padding between packed frames so bilinear sampling cannot bleed one frame
// into its neighbour.
constexpr int kPadding = 1;
constexpr int kAtlasWidth = 2048;

// Player-coloured pixels are stored as a shade, not an absolute palette entry.
// Each player's ramp begins at palette index 16 * player and runs eight
// entries, dark to light: 16-23 is player 1's blue, 32-39 player 2's red,
// 48-55 player 3's green. The eight indexes after each ramp are unrelated
// browns, so a shade is clamped to the ramp: units use shades 8 and 9 for
// their brightest highlights.
constexpr int kPlayerRampSize = 8;
// Used only until the dat's ramp table has been supplied.
constexpr int kPlayerRampStride = 16;

} // namespace

bool GraphicCache::Init(SDL_Renderer* renderer,
                        const std::string& drs_path,
                        const std::string& palette_path) {
    sdl_renderer = renderer;
    if (!DrsOpen(drs_path, drs)) return false;
    if (!LoadJascPalette(palette_path, palette)) return false;

    ready = true;
    std::cout << "[HAL] GraphicCache ready: " << drs.entries.size()
              << " SLPs, " << palette.size() << " palette entries." << std::endl;
    return true;
}

void GraphicCache::Shutdown() {
    for (auto& pair : atlases) {
        if (pair.second.base) SDL_DestroyTexture(pair.second.base);
    }
    for (auto& pair : player_layers) {
        if (pair.second) SDL_DestroyTexture(pair.second);
    }
    for (auto& pair : shadow_layers) {
        if (pair.second) SDL_DestroyTexture(pair.second);
    }
    for (auto& pair : outline_layers) {
        if (pair.second) SDL_DestroyTexture(pair.second);
    }
    for (auto& pair : interface_atlases) {
        if (pair.second.base) SDL_DestroyTexture(pair.second.base);
    }
    for (auto& pair : interface_player_layers) {
        if (pair.second) SDL_DestroyTexture(pair.second);
    }
    for (auto& pair : terrain_atlases) {
        if (pair.second.base) SDL_DestroyTexture(pair.second.base);
    }
    atlases.clear();
    player_layers.clear();
    shadow_layers.clear();
    outline_layers.clear();
    interface_atlases.clear();
    interface_player_layers.clear();
    terrain_atlases.clear();
    interface_ready = false;
    terrain_ready = false;
    gamedata_ready = false;
    ready = false;
}

const GraphicAtlas* GraphicCache::GetAtlas(uint32_t slp_id) {
    return AtlasFrom(drs, atlases, slp_id, slp_id, palette);
}

template <typename Key>
const GraphicAtlas* GraphicCache::AtlasFrom(
        const DrsArchive& src,
        std::unordered_map<Key, GraphicAtlas>& cache,
        Key key, uint32_t slp_id,
        const std::vector<PaletteColor>& colours) {
    auto it = cache.find(key);
    if (it != cache.end()) {
        return it->second.valid ? &it->second : nullptr;
    }

    GraphicAtlas atlas;
    // Insert even on failure, so a missing graphic is not retried every frame.
    const std::vector<SlpFrameHeader> headers = SlpReadHeaders(src, slp_id);
    if (headers.empty()) {
        cache.emplace(key, atlas);
        return nullptr;
    }

    // Shelf-pack the frames in order, so a frame index maps straight to a rect.
    int shelf_x = 0;
    int shelf_y = 0;
    int shelf_h = 0;
    atlas.frames.resize(headers.size());
    for (size_t i = 0; i < headers.size(); ++i) {
        const int fw = static_cast<int>(headers[i].width);
        const int fh = static_cast<int>(headers[i].height);
        if (fw <= 0 || fh <= 0) {
            atlas.frames[i].rect = {0, 0, 0, 0};
            continue;
        }
        if (shelf_x + fw > kAtlasWidth) {
            shelf_x = 0;
            shelf_y += shelf_h + kPadding;
            shelf_h = 0;
        }
        atlas.frames[i].rect = {shelf_x, shelf_y, fw, fh};
        atlas.frames[i].hotspot_x = headers[i].hotspot_x;
        atlas.frames[i].hotspot_y = headers[i].hotspot_y;
        shelf_x += fw + kPadding;
        shelf_h = std::max(shelf_h, fh);
    }
    atlas.width = kAtlasWidth;
    atlas.height = shelf_y + shelf_h;
    if (atlas.height <= 0) {
        cache.emplace(key, atlas);
        return nullptr;
    }

    // Coverage bits only for art tall enough to stand in front of a unit.
    constexpr int kOccluderHeight = 64;
    bool tall = false;
    for (const auto& h : headers) tall = tall || static_cast<int>(h.height) > kOccluderHeight;
    atlas.base = BuildLayer(src, slp_id, Layer::Base, 0, colours,
                            tall ? &atlas.opaque : nullptr);
    // Geometry, not pixels, decides validity: a shadow graphic decodes to no
    // colour pixels at all, but its frame rects and hotspots are still needed.
    atlas.valid = !atlas.frames.empty();

    auto inserted = cache.emplace(key, std::move(atlas));
    return inserted.first->second.valid ? &inserted.first->second : nullptr;
}

SDL_Texture* GraphicCache::GetPlayerLayer(uint32_t slp_id, int player) {
    // Keyed by the colour the player wears, which the lobby may change from
    // game to game.
    const uint64_t key = (static_cast<uint64_t>(slp_id) << 8) | static_cast<uint8_t>(ColourSlot(player) + 1);
    auto it = player_layers.find(key);
    if (it != player_layers.end()) return it->second;

    SDL_Texture* tex = BuildLayer(drs, slp_id, Layer::Player, player, palette);
    player_layers.emplace(key, tex);
    return tex;
}

SDL_Texture* GraphicCache::GetShadowLayer(uint32_t slp_id) {
    auto it = shadow_layers.find(slp_id);
    if (it != shadow_layers.end()) return it->second;

    SDL_Texture* tex = BuildLayer(drs, slp_id, Layer::Shadow, 0, palette);
    shadow_layers.emplace(slp_id, tex);
    return tex;
}

void GraphicCache::SetPlayerRamps(const std::vector<int>& bases) {
    player_ramps = bases;
}

void GraphicCache::SetPlayerColours(const std::array<int, 16>& slots) {
    colour_slots = slots;
}

int GraphicCache::ColourSlot(int player) const {
    // Gaia has no colour of its own in the dat; it is drawn in the grey ramp.
    constexpr int kGaiaRamp = 6;
    if (player == 0) return kGaiaRamp;
    if (player < 0 || player >= 16) return 0;
    return colour_slots[static_cast<size_t>(player)] >= 0 ? colour_slots[static_cast<size_t>(player)]
                                                          : player - 1;
}

size_t GraphicCache::PaletteIndexForPlayer(int player, uint8_t shade) const {
    const size_t step = std::min<size_t>(shade, kPlayerRampSize - 1);
    // The colour the player wears: its seat's own, or the lobby's choice.
    const int index = ColourSlot(player);
    if (index >= 0 && index < static_cast<int>(player_ramps.size())) {
        return static_cast<size_t>(player_ramps[index]) + step;
    }
    return static_cast<size_t>(player) * kPlayerRampStride + step;
}

PaletteColor GraphicCache::GetPlayerColor(int player) const {
    if (palette.empty()) return PaletteColor{255, 255, 255};
    // Four steps along the ramp: the pure colour, before it washes out into
    // the pale highlights the last few entries hold.
    const size_t idx = PaletteIndexForPlayer(player, 4) % palette.size();
    return palette[idx];
}

PaletteColor GraphicCache::GetPaletteColor(size_t index) const {
    if (palette.empty()) return PaletteColor{255, 255, 255};
    return palette[index % palette.size()];
}

bool GraphicCache::OpenInterface(const std::string& drs_path) {
    interface_ready = DrsOpen(drs_path, interface_drs);
    if (interface_ready) {
        std::cout << "[HAL] Interface archive: " << interface_drs.entries.size()
                  << " files." << std::endl;
    }
    return interface_ready;
}

const std::vector<PaletteColor>& GraphicCache::InterfacePalette(uint32_t palette_id) {
    if (palette_id == kGamePalette) return palette;
    auto it = interface_palettes.find(palette_id);
    if (it != interface_palettes.end()) return it->second;
    if (const uint32_t percent = palette_id >> 24; percent > 0) {
        const std::vector<PaletteColor> base = InterfacePalette(palette_id & 0xFFFFFFu);
        std::vector<PaletteColor> shaded(base.size());
        const int keep = 100 - static_cast<int>(percent);
        for (size_t i = 0; i < base.size(); ++i) {
            const int r = base[i].r * keep / 100;
            const int g = base[i].g * keep / 100;
            const int b = base[i].b * keep / 100;
            int best = 0;
            int best_d = 1 << 30;
            for (size_t j = 0; j < base.size(); ++j) {
                const int dr = base[j].r - r, dg = base[j].g - g, db = base[j].b - b;
                const int d = dr * dr + dg * dg + db * db;
                if (d < best_d) { best_d = d; best = static_cast<int>(j); }
            }
            shaded[i] = base[static_cast<size_t>(best)];
        }
        return interface_palettes.emplace(palette_id, std::move(shaded)).first->second;
    }
    std::vector<PaletteColor> colours;
    std::string text;
    if (interface_ready && DrsReadText(interface_drs, palette_id, text)) {
        // JASC-PAL: a header, a version, a count, then one "r g b" per line.
        size_t pos = 0;
        int line = 0;
        int count = 0;
        while (pos < text.size()) {
            size_t end = text.find('\n', pos);
            if (end == std::string::npos) end = text.size();
            const std::string row = text.substr(pos, end - pos);
            pos = end + 1;
            if (line == 2) count = std::atoi(row.c_str());
            if (line >= 3 && static_cast<int>(colours.size()) < count) {
                int r = 0, g = 0, b = 0;
                if (std::sscanf(row.c_str(), "%d %d %d", &r, &g, &b) == 3) {
                    colours.push_back({static_cast<uint8_t>(r), static_cast<uint8_t>(g),
                                       static_cast<uint8_t>(b)});
                }
            }
            ++line;
        }
    }
    if (colours.empty()) colours = palette;
    return interface_palettes.emplace(palette_id, std::move(colours)).first->second;
}

const GraphicAtlas* GraphicCache::GetInterfaceAtlas(uint32_t slp_id, uint32_t palette_id) {
    if (!interface_ready) return nullptr;
    const uint64_t key = (static_cast<uint64_t>(palette_id) << 32) | slp_id;
    return AtlasFrom(interface_drs, interface_atlases, key, slp_id,
                     InterfacePalette(palette_id));
}

SDL_Texture* GraphicCache::GetInterfacePlayerLayer(uint32_t slp_id, int player) {
    if (!interface_ready) return nullptr;
    const uint64_t key = (static_cast<uint64_t>(slp_id) << 8) | static_cast<uint8_t>(ColourSlot(player) + 1);
    auto it = interface_player_layers.find(key);
    if (it != interface_player_layers.end()) return it->second;

    SDL_Texture* tex = BuildLayer(interface_drs, slp_id, Layer::Player, player, palette);
    interface_player_layers.emplace(key, tex);
    return tex;
}

SDL_Texture* GraphicCache::GetInterfaceTexture(uint32_t slp_id, uint32_t palette_id) {
    const GraphicAtlas* atlas = GetInterfaceAtlas(slp_id, palette_id);
    return atlas ? atlas->base : nullptr;
}

bool GraphicCache::OpenTerrain(const std::string& drs_path) {
    terrain_ready = DrsOpen(drs_path, terrain_drs);
    if (terrain_ready) {
        std::cout << "[HAL] Terrain archive: " << terrain_drs.entries.size()
                  << " files." << std::endl;
    }
    return terrain_ready;
}

const GraphicAtlas* GraphicCache::GetTerrainAtlas(uint32_t slp_id) {
    if (!terrain_ready) return nullptr;
    return AtlasFrom(terrain_drs, terrain_atlases, slp_id, slp_id, palette);
}

bool GraphicCache::OpenGameData(const std::string& drs_path) {
    gamedata_ready = DrsOpen(drs_path, gamedata_drs);
    if (gamedata_ready) {
        std::cout << "[HAL] Game data archive: " << gamedata_drs.entries.size()
                  << " scripts." << std::endl;
    }
    return gamedata_ready;
}

bool GraphicCache::ReadGameDataText(uint32_t file_id, std::string& out) const {
    if (!gamedata_ready) return false;
    return DrsReadText(gamedata_drs, file_id, out);
}

SDL_Texture* GraphicCache::GetOutlineLayer(uint32_t slp_id) {
    auto it = outline_layers.find(slp_id);
    if (it != outline_layers.end()) return it->second;

    SDL_Texture* tex = BuildLayer(drs, slp_id, Layer::Outline, 0, palette);
    outline_layers.emplace(slp_id, tex);
    return tex;
}

SDL_Texture* GraphicCache::BuildLayer(const DrsArchive& src, uint32_t slp_id,
                                              Layer layer, int player,
                                     const std::vector<PaletteColor>& colours,
                                     std::vector<uint8_t>* opaque) {
    // The atlas geometry has to exist before a layer can be packed into it. For
    // the base layer this is the caller's half-built entry, so the rects come
    // from the header list, not from `atlases`.
    const std::vector<SlpFrameHeader> headers = SlpReadHeaders(src, slp_id);
    if (headers.empty()) return nullptr;

    int shelf_x = 0, shelf_y = 0, shelf_h = 0;
    std::vector<SDL_Rect> rects(headers.size());
    for (size_t i = 0; i < headers.size(); ++i) {
        const int fw = static_cast<int>(headers[i].width);
        const int fh = static_cast<int>(headers[i].height);
        if (fw <= 0 || fh <= 0) { rects[i] = {0, 0, 0, 0}; continue; }
        if (shelf_x + fw > kAtlasWidth) {
            shelf_x = 0;
            shelf_y += shelf_h + kPadding;
            shelf_h = 0;
        }
        rects[i] = {shelf_x, shelf_y, fw, fh};
        shelf_x += fw + kPadding;
        shelf_h = std::max(shelf_h, fh);
    }
    const int atlas_h = shelf_y + shelf_h;
    if (atlas_h <= 0) return nullptr;

    SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormat(
        0, kAtlasWidth, atlas_h, 32, SDL_PIXELFORMAT_RGBA32);
    if (!surface) return nullptr;
    SDL_memset(surface->pixels, 0, static_cast<size_t>(surface->h) * surface->pitch);

    if (opaque != nullptr) {
        opaque->assign((static_cast<size_t>(kAtlasWidth) * static_cast<size_t>(atlas_h) + 7) / 8, 0);
    }
    bool wrote_any = false;
    SlpFrame frame;
    for (size_t i = 0; i < headers.size(); ++i) {
        if (rects[i].w == 0) continue;
        if (!SlpDecodeFrame(src, slp_id, static_cast<uint32_t>(i), frame)) continue;

        uint8_t* pixels = static_cast<uint8_t*>(surface->pixels);
        for (uint32_t y = 0; y < frame.height; ++y) {
            uint8_t* dst = pixels
                         + static_cast<size_t>(rects[i].y + static_cast<int>(y)) * surface->pitch
                         + static_cast<size_t>(rects[i].x) * 4;
            for (uint32_t x = 0; x < frame.width; ++x, dst += 4) {
                const size_t p = static_cast<size_t>(y) * frame.width + x;
                if (opaque != nullptr && frame.alpha[p]) {
                    const size_t bit =
                        static_cast<size_t>(rects[i].y + static_cast<int>(y)) * kAtlasWidth +
                        static_cast<size_t>(rects[i].x + static_cast<int>(x));
                    (*opaque)[bit >> 3] |= static_cast<uint8_t>(1u << (bit & 7));
                }
                switch (layer) {
                case Layer::Base: {
                    // Player-coloured pixels are deliberately omitted; they are
                    // supplied by the per-player layer so a unit can be tinted
                    // without disturbing the rest of its art.
                    if (!frame.alpha[p] || frame.player[p]) continue;
                    const PaletteColor& c = colours[frame.indexes[p] % colours.size()];
                    dst[0] = c.r; dst[1] = c.g; dst[2] = c.b; dst[3] = 255;
                    wrote_any = true;
                    break;
                }
                case Layer::Player: {
                    if (!frame.player[p]) continue;
                    const size_t pi = PaletteIndexForPlayer(player, frame.player[p]);
                    const PaletteColor& c = palette[pi % palette.size()];
                    dst[0] = c.r; dst[1] = c.g; dst[2] = c.b; dst[3] = 255;
                    wrote_any = true;
                    break;
                }
                case Layer::Shadow: {
                    if (!frame.shadow[p]) continue;
                    dst[0] = 0; dst[1] = 0; dst[2] = 0; dst[3] = 255;
                    wrote_any = true;
                    break;
                }
                case Layer::Outline: {
                    if (!frame.outline[p]) continue;
                    // White, so a colour mod can tint it per player.
                    dst[0] = 255; dst[1] = 255; dst[2] = 255; dst[3] = 255;
                    wrote_any = true;
                    break;
                }
                }
            }
        }
    }

    if (!wrote_any) {
        SDL_FreeSurface(surface);
        return nullptr;
    }

    SDL_Texture* texture = SDL_CreateTextureFromSurface(sdl_renderer, surface);
    SDL_FreeSurface(surface);
    if (texture) SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
    return texture;
}

} // namespace HAL
