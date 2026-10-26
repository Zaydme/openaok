#include "core/LandGenerator.h"

#include <algorithm>

#include "core/ConfigManager.h"

namespace Core::LandGenerator {

namespace {

// Defaults for the figures a script leaves out.
constexpr int kDefaultBaseSize = 3;
constexpr int kDefaultClumping = 8;
constexpr int kDefaultFuzziness = 20;
// How far inside a border fuzziness reaches: the band in which a tile is
// taken only by chance, likelier the further in it is.
constexpr int kFuzzBand = 4;
// Zones the script does not name are numbered from here, clear of any it does.
constexpr int kFirstPrivateZone = 1000;

struct Land {
    TerrainType terrain = TerrainType::Grass1;
    int budget = 0;
    int base_size = kDefaultBaseSize;
    int clumping = kDefaultClumping;
    int fuzziness = kDefaultFuzziness;
    int avoidance = 0;
    int zone = 0;
    int id = -1;
    PlayerId player = 0;
    // Borders as tile limits, inclusive.
    int min_x = 0, min_y = 0, max_x = 0, max_y = 0;
    // Which of the four sides the script set: fuzziness acts on those only.
    bool bordered[4] = {false, false, false, false};
    int origin_x = 0, origin_y = 0;
    int size = 0;
    std::vector<int> frontier;   // tile indexes next to the land, not yet taken
};

void Borders(const RMSCommand& command, int width, int height, Land& land) {
    const int left = command.Attribute("left_border", 0);
    const int right = command.Attribute("right_border", 0);
    const int top = command.Attribute("top_border", 0);
    const int bottom = command.Attribute("bottom_border", 0);
    land.min_x = width * left / 100;
    land.max_x = width - 1 - width * right / 100;
    land.min_y = height * top / 100;
    land.max_y = height - 1 - height * bottom / 100;
    land.bordered[0] = left > 0;
    land.bordered[1] = right > 0;
    land.bordered[2] = top > 0;
    land.bordered[3] = bottom > 0;
}

void Shape(const RMSCommand& command, int total_tiles, Land& land, int share_between) {
    ConfigManager::Get().TerrainForRmsId(command.Attribute("terrain_type", -1), land.terrain);
    if (command.HasAttribute("number_of_tiles")) {
        land.budget = command.Attribute("number_of_tiles", 0);
    } else {
        land.budget = total_tiles * command.Attribute("land_percent", 0) / 100 /
                      std::max(1, share_between);
    }
    land.base_size = command.Attribute("base_size", kDefaultBaseSize);
    land.clumping = command.Attribute("clumping_factor", kDefaultClumping);
    land.fuzziness = std::clamp(command.Attribute("border_fuzziness", kDefaultFuzziness), 0, 100);
    land.avoidance = command.Attribute("other_zone_avoidance_distance", 0);
    land.id = command.Attribute("land_id", -1);
}

// Random placement: the players round an ellipse inside the player lands'
// borders, at an even spacing from a random starting bearing.
std::vector<std::pair<int, int>> PlaceSeats(const Land& area, size_t count, Random& rng) {
    static constexpr int kCos[16] = {1024, 946, 724, 392, 0, -392, -724, -946,
                                     -1024, -946, -724, -392, 0, 392, 724, 946};
    static constexpr int kSin[16] = {0, 392, 724, 946, 1024, 946, 724, 392,
                                     0, -392, -724, -946, -1024, -946, -724, -392};
    const int cx = (area.min_x + area.max_x) / 2;
    const int cy = (area.min_y + area.max_y) / 2;
    // Three quarters of the way out, short of the borders by a base.
    const int rx = std::max(4, ((area.max_x - area.min_x) / 2 - area.base_size) * 3 / 4);
    const int ry = std::max(4, ((area.max_y - area.min_y) / 2 - area.base_size) * 3 / 4);
    const int first = static_cast<int>(rng.NextInt(16));
    std::vector<std::pair<int, int>> starts;
    for (size_t p = 0; p < count; ++p) {
        const int step = (first + static_cast<int>(p * 16 / std::max<size_t>(1, count))) % 16;
        starts.emplace_back(cx + rx * kCos[step] / 1024, cy + ry * kSin[step] / 1024);
    }
    return starts;
}

class Grower {
public:
    Grower(int width, int height, std::vector<Land>& lands)
        : width_(width), height_(height), lands_(lands),
          owner_(static_cast<size_t>(width) * static_cast<size_t>(height), -1) {}

    // Takes the base square about each origin, then grows every land in turn.
    void Run(Random& rng) {
        for (size_t l = 0; l < lands_.size(); ++l) Seed(static_cast<int>(l));
        bool growing = true;
        while (growing) {
            growing = false;
            for (size_t l = 0; l < lands_.size(); ++l) {
                if (Step(static_cast<int>(l), rng)) growing = true;
            }
        }
    }

    const std::vector<int16_t>& Owners() const { return owner_; }

private:
    int Index(int x, int y) const { return y * width_ + x; }
    bool InBounds(int x, int y) const { return x >= 0 && y >= 0 && x < width_ && y < height_; }

    bool InsideBorders(const Land& land, int x, int y) const {
        return x >= land.min_x && x <= land.max_x && y >= land.min_y && y <= land.max_y;
    }

    // Kept from every land of another zone by the land's avoidance distance.
    bool ClearOfOtherZones(const Land& land, int x, int y) const {
        const int d = land.avoidance;
        if (d <= 0) return true;
        for (int oy = -d; oy <= d; ++oy) {
            for (int ox = -d; ox <= d; ++ox) {
                const int nx = x + ox;
                const int ny = y + oy;
                if (!InBounds(nx, ny)) continue;
                const int other = owner_[static_cast<size_t>(Index(nx, ny))];
                if (other >= 0 && lands_[static_cast<size_t>(other)].zone != land.zone) return false;
            }
        }
        return true;
    }

    void Take(int l, int x, int y) {
        Land& land = lands_[static_cast<size_t>(l)];
        owner_[static_cast<size_t>(Index(x, y))] = static_cast<int16_t>(l);
        ++land.size;
        static constexpr int kDx[4] = {1, -1, 0, 0};
        static constexpr int kDy[4] = {0, 0, 1, -1};
        for (int k = 0; k < 4; ++k) {
            const int nx = x + kDx[k];
            const int ny = y + kDy[k];
            if (InBounds(nx, ny) && owner_[static_cast<size_t>(Index(nx, ny))] < 0) {
                land.frontier.push_back(Index(nx, ny));
            }
        }
    }

    void Seed(int l) {
        Land& land = lands_[static_cast<size_t>(l)];
        const int r = land.base_size;
        for (int dy = -r; dy <= r; ++dy) {
            for (int dx = -r; dx <= r; ++dx) {
                const int x = land.origin_x + dx;
                const int y = land.origin_y + dy;
                if (!InBounds(x, y) || owner_[static_cast<size_t>(Index(x, y))] >= 0) continue;
                Take(l, x, y);
            }
        }
    }

    int OwnNeighbours(int l, int x, int y) const {
        int n = 0;
        for (int oy = -1; oy <= 1; ++oy) {
            for (int ox = -1; ox <= 1; ++ox) {
                if ((ox | oy) == 0 || !InBounds(x + ox, y + oy)) continue;
                if (owner_[static_cast<size_t>(Index(x + ox, y + oy))] == l) ++n;
            }
        }
        return n;
    }

    // Whether a tile at the edge of the land's room is taken this time:
    // always well inside the borders, by chance near one -- likelier the more
    // fuzzily the script asks, and the further in.
    bool PassesFuzz(const Land& land, int x, int y, Random& rng) const {
        int nearest = kFuzzBand;
        if (land.bordered[0]) nearest = std::min(nearest, x - land.min_x);
        if (land.bordered[1]) nearest = std::min(nearest, land.max_x - x);
        if (land.bordered[2]) nearest = std::min(nearest, y - land.min_y);
        if (land.bordered[3]) nearest = std::min(nearest, land.max_y - y);
        if (nearest >= kFuzzBand) return true;
        const int chance = land.fuzziness + (100 - land.fuzziness) * nearest / kFuzzBand;
        return static_cast<int>(rng.NextInt(100)) < chance;
    }

    // One tile more for land `l`, if it wants one and has anywhere to put it.
    bool Step(int l, Random& rng) {
        Land& land = lands_[static_cast<size_t>(l)];
        if (land.size >= land.budget) return false;
        // A few candidates from the frontier, the one most hemmed in by the
        // land's own tiles winning: the clumping factor is how many.
        const int samples = 1 + land.clumping / 4;
        int best = -1;
        int best_slot = -1;
        int best_score = -1;
        for (int s = 0; s < samples && !land.frontier.empty(); ++s) {
            const int slot = static_cast<int>(rng.NextInt(static_cast<uint32_t>(land.frontier.size())));
            const int tile = land.frontier[static_cast<size_t>(slot)];
            const int x = tile % width_;
            const int y = tile / width_;
            const bool usable = owner_[static_cast<size_t>(tile)] < 0 && InsideBorders(land, x, y) &&
                                ClearOfOtherZones(land, x, y);
            if (!usable) {
                // Dead for good to this land: taken, or out of its room. The
                // last entry moves into its slot -- the best so far with it,
                // if that was the last.
                const int last = static_cast<int>(land.frontier.size()) - 1;
                land.frontier[static_cast<size_t>(slot)] = land.frontier.back();
                land.frontier.pop_back();
                if (best_slot == last) best_slot = slot;
                continue;
            }
            const int score = OwnNeighbours(l, x, y);
            if (score > best_score) {
                best = tile;
                best_slot = slot;
                best_score = score;
            }
        }
        if (best < 0) return !land.frontier.empty();
        const int x = best % width_;
        const int y = best / width_;
        // Remove it from the frontier either way: refused by the fuzz, it may
        // come back through another neighbour.
        land.frontier[static_cast<size_t>(best_slot)] = land.frontier.back();
        land.frontier.pop_back();
        if (PassesFuzz(land, x, y, rng)) Take(l, x, y);
        return true;
    }

    int width_;
    int height_;
    std::vector<Land>& lands_;
    std::vector<int16_t> owner_;
};

} // namespace

Layout Generate(Physics::Grid& grid, const RMSSection& section, const std::vector<Seat>& seats,
                Random& rng, Paint paint) {
    auto& cfg = ConfigManager::Get();
    const int width = grid.GetWidth();
    const int height = grid.GetHeight();
    const int total = width * height;
    Layout layout;

    for (const RMSCommand& command : section.commands) {
        if (command.name == "base_terrain") cfg.TerrainForRmsId(command.Value(), layout.base_terrain);
    }

    std::vector<Land> lands;
    int private_zone = kFirstPrivateZone;
    bool placed_players = false;
    for (const RMSCommand& command : section.commands) {
        if (command.name == "create_player_lands" && !placed_players) {
            placed_players = true;
            Land area;
            Borders(command, width, height, area);
            Shape(command, total, area, static_cast<int>(seats.size()));
            layout.starts = PlaceSeats(area, seats.size(), rng);
            const bool shared = command.HasAttribute("zone");
            const bool by_team = command.HasAttribute("set_zone_by_team");
            const bool random_zone = command.HasAttribute("set_zone_randomly");
            for (size_t s = 0; s < seats.size(); ++s) {
                Land land = area;
                land.player = seats[s].player;
                land.id = seats[s].player;
                if (shared) {
                    land.zone = command.Attribute("zone", 0);
                } else if (by_team && seats[s].team != 0) {
                    land.zone = 100 + seats[s].team;
                } else if (random_zone) {
                    land.zone = 200 + static_cast<int>(rng.NextInt(static_cast<uint32_t>(
                                    std::max<size_t>(1, seats.size()))));
                } else {
                    land.zone = private_zone++;
                }
                land.origin_x = std::clamp(layout.starts[s].first, 0, width - 1);
                land.origin_y = std::clamp(layout.starts[s].second, 0, height - 1);
                lands.push_back(land);
            }
        } else if (command.name == "create_land") {
            Land land;
            Borders(command, width, height, land);
            Shape(command, total, land, 1);
            land.zone = command.HasAttribute("zone") ? command.Attribute("zone", 0) : private_zone++;
            land.player = static_cast<PlayerId>(std::max(0, command.Attribute("assign_to_player", 0)));
            // Where the script says, in percent of the map; else anywhere
            // inside its borders.
            bool positioned = false;
            for (const RMSCommand& attribute : command.attributes) {
                if (attribute.name == "land_position" && attribute.args.size() >= 2) {
                    land.origin_x = width * attribute.Value(0) / 100;
                    land.origin_y = height * attribute.Value(1) / 100;
                    positioned = true;
                }
            }
            if (!positioned) {
                const int span_x = std::max(1, land.max_x - land.min_x + 1);
                const int span_y = std::max(1, land.max_y - land.min_y + 1);
                land.origin_x = land.min_x + static_cast<int>(rng.NextInt(static_cast<uint32_t>(span_x)));
                land.origin_y = land.min_y + static_cast<int>(rng.NextInt(static_cast<uint32_t>(span_y)));
            }
            land.origin_x = std::clamp(land.origin_x, 0, width - 1);
            land.origin_y = std::clamp(land.origin_y, 0, height - 1);
            lands.push_back(land);
        }
    }
    // A script with no player lands still needs starts.
    if (!placed_players) {
        Land area;
        area.max_x = width - 1;
        area.max_y = height - 1;
        area.base_size = 9;
        layout.starts = PlaceSeats(area, seats.size(), rng);
    }

    Grower grower(width, height, lands);
    grower.Run(rng);
    layout.land_of = grower.Owners();
    for (const Land& land : lands) {
        layout.land_ids.push_back(land.id);
        layout.land_player.push_back(land.player);
    }

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const int owner = layout.land_of[static_cast<size_t>(y * width + x)];
            const TerrainType terrain = owner >= 0 ? lands[static_cast<size_t>(owner)].terrain
                                                   : layout.base_terrain;
            paint(grid, x, y, terrain);
        }
    }
    return layout;
}

} // namespace Core::LandGenerator
