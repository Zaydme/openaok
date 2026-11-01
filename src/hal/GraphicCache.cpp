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
// A texture page starts kFirstPageSide square and grows, a side at a time,
// as it fills: to kLargestPageSide for art, so the art on screen together
// mostly shares one page, and to kPageSide for shadows and outlines, which
// are few or seldom drawn. Unless the renderer allows less.
constexpr int kFirstPageSide = 1024;
constexpr int kPageSide = 2048;
constexpr int kLargestPageSide = 4096;
// How wide a graphic's frames are shelved: a block of them, its border all
// round, is no wider than kPageSide.
constexpr int kAtlasWidth = kPageSide - 2 * kPadding;

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
    SDL_RendererInfo info{};
    first_side = kFirstPageSide;
    page_side = kPageSide;
    largest_side = kLargestPageSide;
    if (SDL_GetRendererInfo(renderer, &info) == 0 && info.max_texture_width > 0 && info.max_texture_height > 0) {
        const int most = std::min(info.max_texture_width, info.max_texture_height);
        first_side = std::min(kFirstPageSide, most);
        page_side = std::min(kPageSide, most);
        largest_side = std::min(kLargestPageSide, most);
    }
    // Growing copies a page on the GPU, through a render target; without
    // them a page is made as big as it may grow from the first.
    grows = SDL_RenderTargetSupported(renderer) == SDL_TRUE;
    if (!DrsOpen(drs_path, drs)) return false;
    if (!LoadJascPalette(palette_path, palette)) return false;

    ready = true;
    std::cout << "[HAL] GraphicCache ready: " << drs.entries.size()
              << " SLPs, " << palette.size() << " palette entries." << std::endl;
    return true;
}

void GraphicCache::Shutdown() {
    for (auto& pool : pages) {
        for (Page& page : pool) SDL_DestroyTexture(page.texture);
        pool.clear();
    }
    for (SDL_Texture* texture : lone) SDL_DestroyTexture(texture);
    lone.clear();
    EndFrame();
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
    arts.clear();
    shadows.clear();
    outlines.clear();
    interface_atlases.clear();
    interface_player_layers.clear();
    terrain_atlases.clear();
    interface_ready = false;
    terrain_ready = false;
    gamedata_ready = false;
    ready = false;
}

const GraphicAtlas* GraphicCache::GetAtlas(uint32_t slp_id) {
    return AtlasFrom(drs, atlases, slp_id, slp_id, palette, false);
}

template <typename Key>
const GraphicAtlas* GraphicCache::AtlasFrom(
        const DrsArchive& src,
        std::unordered_map<Key, GraphicAtlas>& cache,
        Key key, uint32_t slp_id,
        const std::vector<PaletteColor>& colours, bool texture) {
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

    // The coverage bits, whether there are player-coloured pixels, and for a
    // graphic drawn from a texture of its own, the texture.
    atlas.base = TextureOf(DrawLayer(src, slp_id, atlas, texture ? Layer::Base : Layer::None, 0, colours, 0,
                                     &atlas.opaque, &atlas.coloured));
    // Geometry, not pixels, decides validity: a shadow graphic decodes to no
    // colour pixels at all, but its frame rects and hotspots are still needed.
    atlas.valid = !atlas.frames.empty();

    auto inserted = cache.emplace(key, std::move(atlas));
    return inserted.first->second.valid ? &inserted.first->second : nullptr;
}

void GraphicCache::Count(int& textures, int64_t& bytes) const {
    textures = 0;
    bytes = 0;
    int widest = 0, tallest = 0;
    auto add = [&](SDL_Texture* texture) {
        int w = 0, h = 0;
        if (texture == nullptr || SDL_QueryTexture(texture, nullptr, nullptr, &w, &h) != 0) return;
        ++textures;
        bytes += static_cast<int64_t>(w) * h * 4;
        if (static_cast<int64_t>(w) * h > static_cast<int64_t>(widest) * tallest) {
            widest = w;
            tallest = h;
        }
    };
    for (const auto& pool : pages) {
        for (const Page& page : pool) add(page.texture);
    }
    for (SDL_Texture* texture : lone) add(texture);
    for (const auto& [id, atlas] : interface_atlases) add(atlas.base);
    for (const auto& [id, texture] : interface_player_layers) add(texture);
    for (const auto& [id, atlas] : terrain_atlases) add(atlas.base);
    std::cout << "[HAL] Largest texture " << widest << "x" << tallest << std::endl;
}

ArtPlace GraphicCache::Art(uint32_t slp_id, int player) {
    const GraphicAtlas* geometry = GetAtlas(slp_id);
    if (geometry == nullptr) return {};
    // Keyed by the colour the player wears, which the lobby may change from
    // game to game.
    const uint64_t key = (static_cast<uint64_t>(slp_id) << 8) |
                         (geometry->coloured ? static_cast<uint8_t>(ColourSlot(player) + 1) : 0);
    auto it = arts.find(key);
    if (it != arts.end()) return it->second;
    const ArtPlace place = LayerPlace(Pool::Art, slp_id, Layer::Art, player);
    arts.emplace(key, place);
    return place;
}

ArtPlace GraphicCache::Shadow(uint32_t slp_id) {
    auto it = shadows.find(slp_id);
    if (it != shadows.end()) return it->second;
    const ArtPlace place = LayerPlace(Pool::Shadow, slp_id, Layer::Shadow, 0);
    shadows.emplace(slp_id, place);
    return place;
}

ArtPlace GraphicCache::Outline(uint32_t slp_id) {
    auto it = outlines.find(slp_id);
    if (it != outlines.end()) return it->second;
    const ArtPlace place = LayerPlace(Pool::Outline, slp_id, Layer::Outline, 0);
    outlines.emplace(slp_id, place);
    return place;
}

ArtPlace GraphicCache::LayerPlace(Pool pool, uint32_t slp_id, Layer layer, int player) {
    const GraphicAtlas* geometry = GetAtlas(slp_id);
    if (geometry == nullptr) return {};
    SDL_Surface* block = DrawLayer(drs, slp_id, *geometry, layer, player, palette, kPadding);
    return block != nullptr ? Place(pool, block, kPadding) : ArtPlace{};
}

bool GraphicCache::Fit(Page& page, int w, int h, SDL_Rect& at) {
    // Along the skyline, the lowest place the block's width fits.
    auto& sky = page.skyline;
    int best = -1, best_y = 0;
    for (size_t i = 0; i < sky.size(); ++i) {
        const int x = sky[i].x;
        if (x + w > page.w) break;
        int y = 0;
        for (size_t j = i; j < sky.size() && sky[j].x < x + w; ++j) y = std::max(y, sky[j].y);
        if (y + h > page.h) continue;
        if (best < 0 || y < best_y) {
            best = static_cast<int>(i);
            best_y = y;
        }
    }
    if (best < 0) return false;
    at = {sky[static_cast<size_t>(best)].x, best_y, w, h};
    // The block's top edge replaces the skyline under it.
    const int end = at.x + w;
    size_t i = static_cast<size_t>(best);
    while (i < sky.size() && sky[i].x + sky[i].w <= end) sky.erase(sky.begin() + static_cast<long>(i));
    if (i < sky.size() && sky[i].x < end) {
        sky[i].w -= end - sky[i].x;
        sky[i].x = end;
    }
    sky.insert(sky.begin() + static_cast<long>(i), Span{at.x, at.y + h, w});
    // Neighbours at one height are one stretch.
    for (size_t k = 0; k + 1 < sky.size();) {
        if (sky[k].y == sky[k + 1].y) {
            sky[k].w += sky[k + 1].w;
            sky.erase(sky.begin() + static_cast<long>(k) + 1);
        } else {
            ++k;
        }
    }
    return true;
}

bool GraphicCache::Grow(Page& page, int most) {
    const bool wider = page.w <= page.h;
    const int w = wider ? page.w * 2 : page.w;
    const int h = wider ? page.h : page.h * 2;
    if (!grows || w > most || h > most) return false;
    SDL_Texture* grown = SDL_CreateTexture(sdl_renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, w, h);
    if (grown == nullptr) return false;
    SDL_SetTextureBlendMode(grown, SDL_BLENDMODE_BLEND);
    SDL_Texture* target = SDL_GetRenderTarget(sdl_renderer);
    Uint8 r = 0, g = 0, b = 0, a = 0;
    SDL_GetRenderDrawColor(sdl_renderer, &r, &g, &b, &a);
    SDL_SetRenderTarget(sdl_renderer, grown);
    SDL_SetRenderDrawColor(sdl_renderer, 0, 0, 0, 0);
    SDL_RenderClear(sdl_renderer);
    SDL_SetRenderDrawColor(sdl_renderer, r, g, b, a);
    SDL_SetTextureBlendMode(page.texture, SDL_BLENDMODE_NONE);
    const SDL_Rect held{0, 0, page.w, page.h};
    SDL_RenderCopy(sdl_renderer, page.texture, nullptr, &held);
    SDL_SetTextureBlendMode(page.texture, SDL_BLENDMODE_BLEND);
    SDL_SetRenderTarget(sdl_renderer, target);
    for (auto& [key, place] : arts) {
        if (place.page == page.texture) place.page = grown;
    }
    for (auto* places : {&shadows, &outlines}) {
        for (auto& [key, place] : *places) {
            if (place.page == page.texture) place.page = grown;
        }
    }
    retired.push_back(page.texture);
    page.texture = grown;
    // A new right half is empty from the top; into a new bottom half the
    // skyline goes on down.
    if (wider) page.skyline.push_back({page.w, 0, page.w});
    page.w = w;
    page.h = h;
    return true;
}

void GraphicCache::EndFrame() {
    for (SDL_Texture* texture : retired) SDL_DestroyTexture(texture);
    retired.clear();
}

ArtPlace GraphicCache::Place(Pool pool, SDL_Surface* block, int border) {
    const int w = block->w;
    const int h = block->h;
    SDL_Texture* texture = nullptr;
    SDL_Rect at{0, 0, w, h};
    const int most = pool == Pool::Art ? largest_side : page_side;
    auto& list = pages[static_cast<size_t>(pool)];
    for (Page& page : list) {
        if (Fit(page, w, h, at)) {
            texture = page.texture;
            break;
        }
    }
    // A pool fills its last page, growing it, before it starts another: the
    // more on one page, the more sprites draw in one batch.
    while (texture == nullptr && !list.empty() && Grow(list.back(), most)) {
        if (Fit(list.back(), w, h, at)) texture = list.back().texture;
    }
    if (texture == nullptr && w <= most && h <= most) {
        // Big enough for the block, as small as it may be.
        int side = grows ? first_side : most;
        while (side < w || side < h) side *= 2;
        Page page;
        page.texture = SDL_CreateTexture(sdl_renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, side, side);
        if (page.texture != nullptr) {
            SDL_SetTextureBlendMode(page.texture, SDL_BLENDMODE_BLEND);
            page.w = side;
            page.h = side;
            page.skyline.push_back({0, 0, side});
            list.push_back(std::move(page));
            if (Fit(list.back(), w, h, at)) texture = list.back().texture;
        }
    }
    if (texture == nullptr) {
        texture = SDL_CreateTexture(sdl_renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, w, h);
        at = {0, 0, w, h};
        if (texture != nullptr) {
            SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
            lone.push_back(texture);
        }
    }
    if (texture == nullptr) {
        SDL_FreeSurface(block);
        return {};
    }
    SDL_UpdateTexture(texture, &at, block->pixels, block->pitch);
    SDL_FreeSurface(block);
    return {texture, at.x + border, at.y + border};
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
                     InterfacePalette(palette_id), true);
}

SDL_Texture* GraphicCache::GetInterfacePlayerLayer(uint32_t slp_id, int player) {
    if (!interface_ready) return nullptr;
    const uint64_t key = (static_cast<uint64_t>(slp_id) << 8) | static_cast<uint8_t>(ColourSlot(player) + 1);
    auto it = interface_player_layers.find(key);
    if (it != interface_player_layers.end()) return it->second;

    const GraphicAtlas* geometry = GetInterfaceAtlas(slp_id);
    SDL_Texture* tex = geometry != nullptr
                           ? BuildLayer(interface_drs, slp_id, *geometry, Layer::Player, player, palette)
                           : nullptr;
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
    return AtlasFrom(terrain_drs, terrain_atlases, slp_id, slp_id, palette, true);
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

SDL_Surface* GraphicCache::DrawLayer(const DrsArchive& src, uint32_t slp_id,
                                     const GraphicAtlas& geometry, Layer layer, int player,
                                     const std::vector<PaletteColor>& colours, int border,
                                     std::vector<uint8_t>* opaque, bool* coloured) {
    if (geometry.height <= 0 || geometry.frames.empty()) return nullptr;
    // A texture of its own spans the atlas's width; a block for a page only
    // what its frames cover.
    int used_w = 0;
    for (const GraphicFrame& f : geometry.frames) used_w = std::max(used_w, f.rect.x + f.rect.w);
    const int surface_w = border == 0 ? geometry.width : used_w + 2 * border;
    const int surface_h = geometry.height + 2 * border;
    SDL_Surface* surface = nullptr;
    if (layer != Layer::None) {
        surface = SDL_CreateRGBSurfaceWithFormat(0, surface_w, surface_h, 32, SDL_PIXELFORMAT_RGBA32);
        if (!surface) return nullptr;
        SDL_memset(surface->pixels, 0, static_cast<size_t>(surface->h) * surface->pitch);
    }
    if (opaque != nullptr) {
        opaque->assign((static_cast<size_t>(geometry.width) * static_cast<size_t>(geometry.height) + 7) / 8, 0);
    }
    if (coloured != nullptr) *coloured = false;

    bool wrote_any = false;
    SlpFrame frame;
    for (size_t i = 0; i < geometry.frames.size(); ++i) {
        const SDL_Rect& r = geometry.frames[i].rect;
        if (r.w == 0) continue;
        if (!SlpDecodeFrame(src, slp_id, static_cast<uint32_t>(i), frame)) continue;
        for (uint32_t y = 0; y < frame.height; ++y) {
            uint8_t* dst = surface == nullptr
                               ? nullptr
                               : static_cast<uint8_t*>(surface->pixels) +
                                     static_cast<size_t>(r.y + border + static_cast<int>(y)) * surface->pitch +
                                     static_cast<size_t>(r.x + border) * 4;
            for (uint32_t x = 0; x < frame.width; ++x) {
                const size_t p = static_cast<size_t>(y) * frame.width + x;
                if (opaque != nullptr && frame.alpha[p]) {
                    const size_t bit = static_cast<size_t>(r.y + static_cast<int>(y)) * geometry.width +
                                       static_cast<size_t>(r.x + static_cast<int>(x));
                    (*opaque)[bit >> 3] |= static_cast<uint8_t>(1u << (bit & 7));
                }
                if (coloured != nullptr && frame.alpha[p] && frame.player[p]) *coloured = true;
                if (dst == nullptr) continue;
                uint8_t* out = dst + static_cast<size_t>(x) * 4;
                const PaletteColor* c = nullptr;
                switch (layer) {
                case Layer::Base:
                    // Player-coloured pixels are left out; an interface icon
                    // gets them from its player layer.
                    if (frame.alpha[p] && !frame.player[p]) c = &colours[frame.indexes[p] % colours.size()];
                    break;
                case Layer::Player:
                    if (frame.player[p]) c = &palette[PaletteIndexForPlayer(player, frame.player[p]) % palette.size()];
                    break;
                case Layer::Art:
                    if (frame.player[p]) {
                        c = &palette[PaletteIndexForPlayer(player, frame.player[p]) % palette.size()];
                    } else if (frame.alpha[p]) {
                        c = &colours[frame.indexes[p] % colours.size()];
                    }
                    break;
                case Layer::Shadow:
                    if (frame.shadow[p]) {
                        out[0] = 0; out[1] = 0; out[2] = 0; out[3] = 255;
                        wrote_any = true;
                    }
                    break;
                case Layer::Outline:
                    // White, so a colour mod can tint it per player.
                    if (frame.outline[p]) {
                        out[0] = 255; out[1] = 255; out[2] = 255; out[3] = 255;
                        wrote_any = true;
                    }
                    break;
                case Layer::None:
                    break;
                }
                if (c != nullptr) {
                    out[0] = c->r; out[1] = c->g; out[2] = c->b; out[3] = 255;
                    wrote_any = true;
                }
            }
        }
    }
    if (!wrote_any) {
        if (surface != nullptr) SDL_FreeSurface(surface);
        return nullptr;
    }
    return surface;
}

SDL_Texture* GraphicCache::BuildLayer(const DrsArchive& src, uint32_t slp_id, const GraphicAtlas& geometry,
                                      Layer layer, int player, const std::vector<PaletteColor>& colours) {
    return TextureOf(DrawLayer(src, slp_id, geometry, layer, player, colours, 0));
}

SDL_Texture* GraphicCache::TextureOf(SDL_Surface* surface) {
    if (surface == nullptr) return nullptr;
    SDL_Texture* texture = SDL_CreateTextureFromSurface(sdl_renderer, surface);
    SDL_FreeSurface(surface);
    if (texture) SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
    return texture;
}

} // namespace HAL
