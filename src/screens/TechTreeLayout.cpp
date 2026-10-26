#include "TechTreeLayout.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>

namespace Screens::TechTreeLayout {

namespace {

Tree g_master;

// ---------------------------------------------------------------------------
// Loading techtree.json.

std::vector<int> Ints(const nlohmann::json& j, const char* key) {
    std::vector<int> out;
    if (!j.contains(key)) return out;
    for (const auto& v : j[key]) out.push_back(v.get<int>());
    return out;
}

std::vector<Slot> Slots(const nlohmann::json& j) {
    std::vector<Slot> out;
    if (!j.contains("slots")) return out;
    for (const auto& s : j["slots"]) {
        out.push_back(Slot{static_cast<SlotMode>(s[0].get<int>()), s[1].get<int>()});
    }
    return out;
}

// ---------------------------------------------------------------------------
// Looking records up: the first with the id, in the table's order.

AgeRecord* AgeById(Tree& t, int id) {
    for (auto& r : t.ages) if (r.id == id) return &r;
    return nullptr;
}
BuildingRecord* BuildingById(Tree& t, int id) {
    for (auto& r : t.buildings) if (r.id == id) return &r;
    return nullptr;
}
UnitRecord* UnitById(Tree& t, int id) {
    for (auto& r : t.units) if (r.id == id) return &r;
    return nullptr;
}
ResearchRecord* ResearchById(Tree& t, int id) {
    for (auto& r : t.researches) if (r.id == id) return &r;
    return nullptr;
}
const AgeRecord* AgeById(const Tree& t, int id) { return AgeById(const_cast<Tree&>(t), id); }

// The first connection of a mode: a record's age, a building's or
// research's building.
int FirstSlot(const std::vector<Slot>& slots, SlotMode mode) {
    for (const Slot& s : slots) if (s.mode == mode) return s.id;
    return -1;
}
int AgeOf(const std::vector<Slot>& slots) { return FirstSlot(slots, SlotMode::Age); }

// An age record's place among the first five.
int AgeIndex(const Tree& t, const AgeRecord* age) {
    if (age == nullptr) return -1;
    for (int i = 0; i < 5 && i < static_cast<int>(t.ages.size()); ++i) {
        if (t.ages[static_cast<size_t>(i)].id == age->id) return i;
    }
    return -1;
}
int AgeIndexOfId(const Tree& t, int age_id) { return AgeIndex(t, AgeById(t, age_id)); }

bool Contains(const std::vector<int>& v, int x) {
    return std::find(v.begin(), v.end(), x) != v.end();
}

// ---------------------------------------------------------------------------
// The civ's statuses.

void SetBuildingStatus(Tree& t, int id, int status) {
    if (BuildingRecord* r = BuildingById(t, id)) r->status = status;
}
void SetUnitStatus(Tree& t, int id, int status) {
    if (UnitRecord* r = UnitById(t, id)) r->status = status;
}

void ApplyTechTree(Tree& t, int civ, const CivRules& rules) {
    // The B of the last enable-or-disable-unit command, which the second loop
    // reads again.
    int last_mode = 0;
    for (const Command& c : rules.tech_tree) {
        if (c.type == 2) {
            last_mode = c.b;
            SetBuildingStatus(t, c.a, c.b == 1 ? kAvailable : kNotAvailable);
            SetUnitStatus(t, c.a, c.b == 1 ? kAvailable : kNotAvailable);
        } else if (c.type == 102) {
            const int tech = static_cast<int>(c.d);
            if (tech == -1) continue;
            if (ResearchRecord* r = ResearchById(t, tech)) {
                r->status = kNotAvailable;
                continue;
            }
            // A technology the screen does not show: what it would have
            // enabled or upgraded to is not available.
            if (tech < 0 || tech >= static_cast<int>(rules.tech_effect.size())) continue;
            for (const Command& e : rules.tech_effect[static_cast<size_t>(tech)]) {
                if (e.type == 2) SetUnitStatus(t, e.a, kNotAvailable);
                else if (e.type == 3 && UnitById(t, e.b) != nullptr) SetUnitStatus(t, e.b, kNotAvailable);
            }
        }
    }
    // Every other civ's own technologies.
    for (size_t tech = 0; tech < rules.tech_civ.size(); ++tech) {
        const int owner = rules.tech_civ[tech];
        if (owner <= 0 || owner == civ) continue;
        if (tech >= rules.tech_effect.size()) continue;
        for (const Command& e : rules.tech_effect[tech]) {
            if (e.type == 2) SetUnitStatus(t, e.a, last_mode == 1 ? kAvailable : kNotAvailable);
            else if (e.type == 3 && UnitById(t, e.b) != nullptr) SetUnitStatus(t, e.b, kNotAvailable);
        }
    }
}

// What leads from something not available is not available either: one pass,
// in the table's order.
template <typename Rec>
void Propagate(Tree& t, std::vector<Rec>& records) {
    for (Rec& r : records) {
        for (const Slot& s : r.slots) {
            int status = 0;
            switch (s.mode) {
            case SlotMode::Age: if (const AgeRecord* a = AgeById(t, s.id)) status = a->status; break;
            case SlotMode::Building: if (const BuildingRecord* b = BuildingById(t, s.id)) status = b->status; break;
            case SlotMode::Unit: if (const UnitRecord* u = UnitById(t, s.id)) status = u->status; break;
            case SlotMode::Research: if (const ResearchRecord* q = ResearchById(t, s.id)) status = q->status; break;
            }
            if (status == kNotAvailable) {
                r.status = kNotAvailable;
                break;
            }
        }
    }
}

// ---------------------------------------------------------------------------
// The layout hints, worked out again per age.

// A research's row.
void ResearchFollowers(Tree& t, ResearchRecord* q, int age_id, int location) {
    if (q == nullptr || q->location >= 1) return;
    if (AgeOf(q->slots) != age_id) return;
    q->location = location + 1;
    for (int id : q->techs) ResearchFollowers(t, ResearchById(t, id), age_id, location + 1);
}

void ResearchLocation(Tree& t, ResearchRecord& r, const AgeRecord& age) {
    if (r.location >= 1) return;
    for (const Slot& s : r.slots) {
        if (s.mode != SlotMode::Research) continue;
        const ResearchRecord* p = ResearchById(t, s.id);
        if (p != nullptr && AgeOf(p->slots) == age.id && p->upper_building == r.upper_building) return;
    }
    r.location = 1;
    const std::vector<int> techs = r.techs;
    const int upper = r.upper_building;
    for (int id : techs) {
        ResearchRecord* q = ResearchById(t, id);
        if (q != nullptr && q->upper_building == upper) ResearchFollowers(t, q, age.id, 1);
    }
}

// A unit's row.
void UnitFollowers(Tree& t, UnitRecord* q, int age_id, int location) {
    if (q == nullptr || q->location >= 1) return;
    if (AgeOf(q->slots) != age_id) return;
    q->location = location + 1;
    for (int id : q->units) UnitFollowers(t, UnitById(t, id), age_id, location + 1);
}

void UnitLocation(Tree& t, UnitRecord& u, const AgeRecord& age) {
    if (u.location >= 1) return;
    for (const Slot& s : u.slots) {
        if (s.mode != SlotMode::Unit) continue;
        const UnitRecord* p = UnitById(t, s.id);
        if (p != nullptr && AgeOf(p->slots) == age.id && p->upper_building == u.upper_building) return;
    }
    u.location = 1;
    const std::vector<int> units = u.units;
    const int upper = u.upper_building;
    for (int id : units) {
        UnitRecord* q = UnitById(t, id);
        if (q != nullptr && q->upper_building == upper) UnitFollowers(t, q, age.id, 1);
    }
}

// A building's row.
void BuildingFollowers(Tree& t, BuildingRecord* q, int age_id, int location) {
    if (q == nullptr || q->location >= 1) return;
    if (AgeOf(q->slots) != age_id) return;
    q->location = location + 1;
    const std::vector<int> buildings = q->buildings;
    for (int id : buildings) BuildingFollowers(t, BuildingById(t, id), age_id, location + 1);
}

void BuildingLocation(Tree& t, BuildingRecord& b, const AgeRecord& age) {
    if (b.location >= 1) return;
    for (const Slot& s : b.slots) {
        if (s.mode != SlotMode::Building) continue;
        const BuildingRecord* p = BuildingById(t, s.id);
        if (p != nullptr && AgeOf(p->slots) == age.id) return;
    }
    b.location = 1;
    const std::vector<int> buildings = b.buildings;
    for (int id : buildings) BuildingFollowers(t, BuildingById(t, id), age.id, 1);
}

// The columns a building needs in each age: the lines starting in
// it, and the lines passing through it from an earlier age to a later one.
void BuildingFirsts(Tree& t, BuildingRecord& b) {
    const int ages = static_cast<int>(t.ages.size());
    for (int i = 0; i < ages && i < 5; ++i) {
        const int age_id = t.ages[static_cast<size_t>(i)].id;
        int count = 0;
        for (int id : b.units) {
            const UnitRecord* u = UnitById(t, id);
            if (u == nullptr || AgeOf(u->slots) != age_id) continue;
            bool upgrade = false;
            for (const Slot& s : u->slots) {
                if (s.mode != SlotMode::Unit) continue;
                const UnitRecord* p = UnitById(t, s.id);
                if (p != nullptr && AgeOf(p->slots) == age_id && p->upper_building == b.id) {
                    upgrade = true;
                    break;
                }
            }
            if (!upgrade) ++count;
        }
        for (int id : b.techs) {
            const ResearchRecord* r = ResearchById(t, id);
            if (r == nullptr || AgeOf(r->slots) != age_id) continue;
            bool follows = false;
            for (const Slot& s : r->slots) {
                if (s.mode != SlotMode::Research) continue;
                const ResearchRecord* p = ResearchById(t, s.id);
                if (p != nullptr && AgeOf(p->slots) == age_id && p->upper_building == b.id) {
                    follows = true;
                    break;
                }
            }
            if (!follows) ++count;
        }
        b.first[i] = count;
    }
    for (int i = 0; i < ages && i < 5; ++i) {
        const int age_id = t.ages[static_cast<size_t>(i)].id;
        for (int id : b.units) {
            const UnitRecord* u = UnitById(t, id);
            if (u == nullptr) continue;
            const int from = AgeOf(u->slots);
            if (from != age_id) continue;
            const UnitRecord* next = u->units.empty() ? nullptr : UnitById(t, u->units.front());
            if (next == nullptr) continue;
            const int to = AgeOf(next->slots);
            for (int c = from + 1; c < to; ++c) {
                if (c - 1 >= 0 && c - 1 < 5) ++b.first[c - 1];
            }
        }
        for (int id : b.techs) {
            const ResearchRecord* r = ResearchById(t, id);
            if (r == nullptr) continue;
            const int from = AgeOf(r->slots);
            if (from != age_id) continue;
            const ResearchRecord* next = r->techs.empty() ? nullptr : ResearchById(t, r->techs.front());
            if (next == nullptr) continue;
            const int to = AgeOf(next->slots);
            for (int c = from + 1; c < to; ++c) {
                if (c - 1 >= 0 && c - 1 < 5) ++b.first[c - 1];
            }
        }
    }
}

void Recompute(Tree& t) {
    // The dat's hints are not used.
    for (auto& b : t.buildings) {
        b.location = 0;
        for (int& f : b.first) f = 0;
    }
    for (auto& u : t.units) u.location = 0;
    for (auto& r : t.researches) r.location = 0;
    for (size_t i = 0; i < t.ages.size(); ++i) {
        const AgeRecord age = t.ages[i];
        // Researches.
        for (int id : age.techs) if (ResearchRecord* r = ResearchById(t, id)) r->line_mode = 4;
        for (int id : age.techs) if (ResearchRecord* r = ResearchById(t, id)) ResearchLocation(t, *r, age);
        // Units.
        for (int id : age.units) {
            if (UnitRecord* u = UnitById(t, id)) u->line_mode = u->required_research >= 0 ? 3 : 2;
        }
        for (int id : age.units) if (UnitRecord* u = UnitById(t, id)) UnitLocation(t, *u, age);
        // Buildings.
        for (int id : age.buildings) {
            BuildingRecord* b = BuildingById(t, id);
            if (b == nullptr) continue;
            b->line_mode = (!b->units.empty() || !b->techs.empty() || !b->buildings.empty()) ? 5 : 6;
            BuildingLocation(t, *b, age);
            BuildingFirsts(t, *b);
        }
    }
}

// ---------------------------------------------------------------------------
// The columns and what hangs in them.

// A building's columns: the most lines it starts in any
// one age; the Castle six, the Dock six and a seventh for the Vikings'
// longboats.
int ColumnCount(const Tree& t, const BuildingRecord& b) {
    int n = 0;
    if (b.id == 82) n = 6;
    else if (b.id == 45) n = 6 + (t.civ == 11 ? 1 : 0);
    else for (int f : b.first) n = std::max(n, f);
    return std::max(1, n);
}

struct Cell {
    int id = -1;
    int status = 1;
    Kind kind = Kind::None;
    int required = -1;
    int line_mode = 0;
    int has_line = 0;
    int parent = -1;
    int parent_age = -1;
    int parent_row = -1;
    int first = 0;
};

// A column as it is filled: its building and its cells.
struct Work {
    int building = -1;
    int building_age = -1;
    int building_status = 0;
    int building_line_mode = 0;
    int building_location = 0;
    bool has_children = false;
    int children[5] = {-1, -1, -1, -1, -1};
    bool has_parent = false;
    int parent_building = -1;
    int parent_age = -1;
    bool box_column = false;
    int content = 0;           // 3 units, 4 researches, 5 both
    Cell cells[25];
};

struct Placer {
    Tree& tree;
    std::vector<Work>& work;
    std::vector<int> placed_units;
    std::vector<int> placed_techs;

    Work* At(int i) { return i >= 0 && i < static_cast<int>(work.size()) ? &work[static_cast<size_t>(i)] : nullptr; }

    // Whether the line from `unit` down would fit in this column. The answer
    // is whether its last box's cell is free.
    bool CanPlaceUnit(int row, int col, const BuildingRecord* b, const AgeRecord* age, const UnitRecord* u) {
        if (col < 0 || b == nullptr || age == nullptr || u == nullptr || row < 0 || row >= 5) return false;
        const int ai = AgeIndex(tree, age);
        if (ai < 0) return false;
        const Work& w = work[static_cast<size_t>(col)];
        if (w.building != b->id) return false;
        const bool result = w.cells[ai * 5 + row].kind == Kind::None;
        for (int id : u->units) {
            const UnitRecord* c = UnitById(tree, id);
            if (c == nullptr) continue;
            const BuildingRecord* cb = BuildingById(tree, c->upper_building);
            if (cb == nullptr || cb->id != b->id) continue;
            if (AgeOf(c->slots) == age->id) return CanPlaceUnit(row + 1, col, b, age, c);
            const AgeRecord* next = AgeById(tree, AgeOf(c->slots));
            if (next == nullptr) return result;
            return CanPlaceUnit(0, col, b, next, c);
        }
        return result;
    }

    bool CanPlaceResearch(int row, int col, const BuildingRecord* b, const AgeRecord* age, const ResearchRecord* r) {
        if (col < 0 || b == nullptr || age == nullptr || r == nullptr || row < 0 || row >= 5) return false;
        const int ai = AgeIndex(tree, age);
        if (ai < 0) return false;
        const Work& w = work[static_cast<size_t>(col)];
        if (w.building != b->id) return false;
        const bool result = w.cells[ai * 5 + row].kind == Kind::None;
        for (int id : r->techs) {
            const ResearchRecord* c = ResearchById(tree, id);
            if (c == nullptr) continue;
            const BuildingRecord* cb = BuildingById(tree, c->upper_building);
            if (cb == nullptr || cb->id != b->id) continue;
            if (AgeOf(c->slots) == age->id) return CanPlaceResearch(row + 1, col, b, age, c);
            const AgeRecord* next = AgeById(tree, AgeOf(c->slots));
            if (next == nullptr) return result;
            return CanPlaceResearch(0, col, b, next, c);
        }
        return result;
    }

    // Hangs a unit's box: in `fixed` if given, else the first
    // column from `start` to `end` with its cell free; then its upgrades,
    // the first in the same column and any others from the next column on.
    // `parent_age` and `parent_row` are the box above it in its line.
    bool PlaceUnit(int parent_age, int parent_row, int parent, int row, int start, int end, int fixed,
                   const BuildingRecord* b, const AgeRecord* age, const UnitRecord* u) {
        if (start < 0 || b == nullptr || age == nullptr || u == nullptr || row < 0 || row >= 5) return false;
        const int ai = AgeIndex(tree, age);
        if (ai < 0) return false;
        if (work[static_cast<size_t>(start)].building != b->id) return false;
        if (Contains(placed_units, u->id)) return false;
        bool result = false;
        const int k = row + ai * 5;
        int col = -1;
        if (fixed >= 0) {
            col = fixed;
            Work& w = work[static_cast<size_t>(col)];
            if (w.cells[k].id <= 0) {
                w.content = w.content == 0 ? 3 : (w.content == 3 ? 3 : 5);
                Cell& c = w.cells[k];
                c.id = u->id;
                c.status = u->status;
                c.line_mode = u->line_mode;
                c.kind = Kind::Unit;
                c.required = u->required_research;
                c.has_line = 1;
                c.parent = parent;
                c.parent_age = parent_age;
                c.parent_row = parent_row;
                placed_units.push_back(u->id);
                result = true;
                if (u->id == 83) c.line_mode = 2;
                // The ages the line passes through without a box of its own.
                for (int j = (parent_age + 1) * 5; j < ai * 5; ++j) {
                    if (j >= 0) w.cells[j].kind = Kind::Through;
                }
            }
        } else {
            col = start;
            while (true) {
                Work& w = work[static_cast<size_t>(col)];
                if (w.cells[k].id > 0 || w.cells[k].kind != Kind::None) {
                    if (col == end) break;
                    ++col;
                    if (col >= static_cast<int>(work.size())) { col = -1; break; }
                    continue;
                }
                w.content = w.content == 0 ? 3 : (w.content == 3 ? 3 : 5);
                Cell& c = w.cells[k];
                c.id = u->id;
                c.status = u->status;
                c.line_mode = u->line_mode;
                c.kind = Kind::Unit;
                c.required = u->required_research;
                c.has_line = 0;
                c.parent = c.parent_age = c.parent_row = -1;
                placed_units.push_back(u->id);
                result = true;
                if (u->id == 83) c.line_mode = 2;
                break;
            }
        }
        if (u->units.empty() || col < 0) return result;
        bool first = true;
        for (int id : u->units) {
            const UnitRecord* c = UnitById(tree, id);
            if (c == nullptr) continue;
            const BuildingRecord* cb = BuildingById(tree, c->upper_building);
            if (cb == nullptr || cb->id != b->id) continue;
            if (first) {
                work[static_cast<size_t>(col)].cells[k].has_line = 1;
                if (AgeOf(c->slots) == age->id) {
                    result = PlaceUnit(ai, row, u->id, row + 1, start, end, col, b, age, c);
                } else if (const AgeRecord* next = AgeById(tree, AgeOf(c->slots))) {
                    result = PlaceUnit(ai, 0, u->id, 0, start, end, col, b, next, c);
                }
                first = false;
            } else {
                if (start == end || end < 0) continue;
                if (AgeOf(c->slots) == age->id) {
                    result = PlaceUnit(ai, row, u->id, row + 1, start + 1, end, -1, b, age, c);
                } else if (const AgeRecord* next = AgeById(tree, AgeOf(c->slots))) {
                    result = PlaceUnit(ai, 0, u->id, 0, start + 1, end, -1, b, next, c);
                }
            }
        }
        return result;
    }

    // The same for a research. The age advances follow only one another.
    bool PlaceResearch(int parent_age, int parent_row, int parent, int row, int start, int end, int fixed,
                       const BuildingRecord* b, const AgeRecord* age, const ResearchRecord* r) {
        if (start < 0 || b == nullptr || age == nullptr || r == nullptr || row < 0 || row >= 5) return false;
        const int ai = AgeIndex(tree, age);
        if (ai < 0) return false;
        if (work[static_cast<size_t>(start)].building != b->id) return false;
        if (Contains(placed_techs, r->id)) return false;
        bool result = false;
        const int k = row + ai * 5;
        int col = -1;
        if (fixed >= 0) {
            col = fixed;
            Work& w = work[static_cast<size_t>(col)];
            if (w.cells[k].id <= 0) {
                w.content = w.content == 0 ? 4 : (w.content == 4 ? 4 : 5);
                Cell& c = w.cells[k];
                c.id = r->id;
                c.status = r->status;
                c.line_mode = r->line_mode;
                c.kind = Kind::Research;
                c.required = -1;
                c.has_line = 1;
                c.parent = parent;
                c.parent_age = parent_age;
                c.parent_row = parent_row;
                placed_techs.push_back(r->id);
                result = true;
                for (int j = (parent_age + 1) * 5; j < ai * 5; ++j) {
                    if (j >= 0) w.cells[j].kind = Kind::Through;
                }
            }
        } else {
            col = start;
            while (true) {
                Work& w = work[static_cast<size_t>(col)];
                if (w.cells[k].id > 0 || w.cells[k].kind != Kind::None) {
                    if (col == end) break;
                    ++col;
                    if (col >= static_cast<int>(work.size())) { col = -1; break; }
                    continue;
                }
                w.content = w.content == 0 ? 4 : (w.content == 4 ? 4 : 5);
                Cell& c = w.cells[k];
                c.id = r->id;
                c.status = r->status;
                c.line_mode = r->line_mode;
                c.kind = Kind::Research;
                c.required = -1;
                c.has_line = 0;
                c.parent = c.parent_age = c.parent_row = -1;
                placed_techs.push_back(r->id);
                result = true;
                break;
            }
        }
        if (r->techs.empty() || col < 0) return result;
        auto is_age = [](int id) { return id == 104 || id == 101 || id == 102 || id == 103; };
        bool first = true;
        for (int id : r->techs) {
            const ResearchRecord* c = ResearchById(tree, id);
            if (c == nullptr) continue;
            const BuildingRecord* cb = BuildingById(tree, c->upper_building);
            if (cb == nullptr) continue;
            if (is_age(r->id) && !is_age(id)) continue;
            if (cb->id != b->id) continue;
            if (first) {
                work[static_cast<size_t>(col)].cells[k].has_line = 1;
                if (AgeOf(c->slots) == age->id) {
                    result = PlaceResearch(ai, row, r->id, row + 1, start, end, col, b, age, c);
                } else if (const AgeRecord* next = AgeById(tree, AgeOf(c->slots))) {
                    result = PlaceResearch(ai, 0, r->id, 0, start, end, col, b, next, c);
                }
                first = false;
            } else {
                if (start == end || end < 0) continue;
                if (AgeOf(c->slots) == age->id) {
                    result = PlaceResearch(ai, row, r->id, row + 1, start + 1, end, -1, b, age, c);
                } else if (const AgeRecord* next = AgeById(tree, AgeOf(c->slots))) {
                    result = PlaceResearch(ai, 0, r->id, 0, start + 1, end, -1, b, next, c);
                }
            }
        }
        return result;
    }

    // One of the hand-placed boxes: a unit or research id in a column of the
    // building, counted from its first.
    void Head(bool unit, int id, int col, int last, const BuildingRecord* b) {
        if (unit) {
            const UnitRecord* u = UnitById(tree, id);
            const AgeRecord* age = u != nullptr ? AgeById(tree, AgeOf(u->slots)) : nullptr;
            if (CanPlaceUnit(0, col, b, age, u)) PlaceUnit(0, 0, -1, 0, col, last, -1, b, age, u);
        } else {
            const ResearchRecord* r = ResearchById(tree, id);
            const AgeRecord* age = r != nullptr ? AgeById(tree, AgeOf(r->slots)) : nullptr;
            if (CanPlaceResearch(0, col, b, age, r)) PlaceResearch(0, 0, -1, 0, col, last, -1, b, age, r);
        }
    }

    // Everything else the building holds, age by age, where it first fits.
    void Rest(int first_col, int last_col, const BuildingRecord* b) {
        for (int c = first_col;; ++c) {
            for (const AgeRecord& age : tree.ages) {
                for (int id : b->units) {
                    const UnitRecord* u = UnitById(tree, id);
                    if (u == nullptr || Contains(placed_units, id)) continue;
                    if (AgeOf(u->slots) != age.id) continue;
                    // The Castle shows the civ's own unique unit and the
                    // trebuchet; the Dock the longboats only for the Vikings.
                    if (b->id == 82 && u->status == kNotAvailable && u->id != 331) continue;
                    if (b->id == 45 && (u->id == 250 || u->id == 533) && tree.civ != 11) continue;
                    bool upgrade = false;
                    for (const Slot& s : u->slots) {
                        if (s.mode != SlotMode::Unit) continue;
                        const UnitRecord* p = UnitById(tree, s.id);
                        if (p != nullptr && p->upper_building == u->upper_building) { upgrade = true; break; }
                    }
                    if (upgrade) continue;
                    if (CanPlaceUnit(0, c, b, &age, u)) PlaceUnit(0, 0, -1, 0, c, last_col, -1, b, &age, u);
                }
                for (int id : b->techs) {
                    const ResearchRecord* r = ResearchById(tree, id);
                    if (r == nullptr || Contains(placed_techs, id)) continue;
                    if (id == 104 || id == 102 || id == 103) continue;
                    if (AgeOf(r->slots) != age.id) continue;
                    bool follows = false;
                    for (const Slot& s : r->slots) {
                        if (s.mode != SlotMode::Research) continue;
                        const ResearchRecord* p = ResearchById(tree, s.id);
                        if (p != nullptr && p->upper_building == r->upper_building) { follows = true; break; }
                    }
                    if (follows) continue;
                    if (CanPlaceResearch(0, c, b, &age, r)) PlaceResearch(0, 0, -1, 0, c, last_col, -1, b, &age, r);
                }
            }
            if (c == last_col) break;
        }
    }
};

// ---------------------------------------------------------------------------
// Heights and positions.

// What a column holds in one age band, in pixels: the building's own boxes if
// it stands in that age, and each box of the band.
int BandNeed(const Tree& t, const Column& col, const AgeRecord& age, int node) {
    int h = 0;
    if (col.building_age == age.id) h = (node + 8) * col.building_location;
    const int ai = AgeIndex(t, &age);
    if (ai < 0) return h;
    int boxes = 0;
    for (int r = 0; r < 5; ++r) {
        const Kind k = col.cells[ai * 5 + r].kind;
        if (k == Kind::Unit || k == Kind::Research || static_cast<int>(k) == 5) ++boxes;
    }
    return (h + (node + 8) * boxes) & 0xffff;
}

int Trunc(double v) { return static_cast<int>(v); }

} // namespace

// ---------------------------------------------------------------------------

bool Load(const std::string& path) {
    std::ifstream in(path);
    if (!in.is_open()) {
        std::cerr << "[TechTree] Cannot open " << path << std::endl;
        return false;
    }
    nlohmann::json j;
    try { in >> j; } catch (...) { return false; }
    Tree t;
    for (const auto& a : j["ages"]) {
        AgeRecord r;
        r.id = a.value("id", -1);
        r.status = a.value("status", 2);
        r.buildings = Ints(a, "buildings");
        r.units = Ints(a, "units");
        r.techs = Ints(a, "techs");
        r.slots = Slots(a);
        t.ages.push_back(std::move(r));
    }
    for (const auto& b : j["buildings"]) {
        BuildingRecord r;
        r.id = b.value("id", -1);
        r.status = b.value("status", 2);
        r.line_mode = b.value("line_mode", 0);
        r.buildings = Ints(b, "buildings");
        r.units = Ints(b, "units");
        r.techs = Ints(b, "techs");
        r.slots = Slots(b);
        r.location = b.value("location", 0);
        r.name_string = b.value("name_string", -1);
        r.icon = b.value("icon", -1);
        r.name = b.value("name", std::string{});
        t.buildings.push_back(std::move(r));
    }
    for (const auto& u : j["units"]) {
        UnitRecord r;
        r.id = u.value("id", -1);
        r.status = u.value("status", 2);
        r.line_mode = u.value("line_mode", 0);
        r.upper_building = u.value("upper_building", -1);
        r.required_research = u.value("required_research", -1);
        r.units = Ints(u, "units");
        r.slots = Slots(u);
        r.location = u.value("location", 0);
        r.name_string = u.value("name_string", -1);
        r.icon = u.value("icon", -1);
        r.name = u.value("name", std::string{});
        t.units.push_back(std::move(r));
    }
    for (const auto& q : j["researches"]) {
        ResearchRecord r;
        r.id = q.value("id", -1);
        r.status = q.value("status", 2);
        r.line_mode = q.value("line_mode", 0);
        r.upper_building = q.value("upper_building", -1);
        r.buildings = Ints(q, "buildings");
        r.units = Ints(q, "units");
        r.techs = Ints(q, "techs");
        r.slots = Slots(q);
        r.location = q.value("location", 0);
        r.name_string = q.value("name_string", -1);
        r.icon = q.value("icon", -1);
        r.name = q.value("name", std::string{});
        t.researches.push_back(std::move(r));
    }
    g_master = std::move(t);
    return true;
}

const Tree& Master() { return g_master; }

Tree ForCiv(int civ, const CivRules& rules) {
    Tree t = g_master;
    t.civ = civ;
    ApplyTechTree(t, civ, rules);
    Propagate(t, t.buildings);
    Propagate(t, t.units);
    Propagate(t, t.researches);
    // The Goths build no stone walls and no gates.
    if (civ == 3) {
        SetBuildingStatus(t, 117, kNotAvailable);
        SetBuildingStatus(t, 487, kNotAvailable);
    }
    Recompute(t);
    return t;
}

void PregameStatuses(Tree& t) {
    for (auto& r : t.ages) if (r.status != kNotAvailable) r.status = kResearched;
    for (auto& r : t.buildings) if (r.status != kNotAvailable) r.status = kResearched;
    for (auto& r : t.units) if (r.status != kNotAvailable) r.status = kResearched;
    for (auto& r : t.researches) if (r.status != kNotAvailable) r.status = kResearched;
}

void PlayerStatuses(Tree& t, const PlayerView& p) {
    auto enabled = [&](int unit) {
        return unit >= 0 && unit < static_cast<int>(p.enabled.size()) && p.enabled[static_cast<size_t>(unit)] != 0;
    };
    auto state = [&](int tech) {
        return tech >= 0 && tech < static_cast<int>(p.tech_state.size()) ? p.tech_state[static_cast<size_t>(tech)] : 0;
    };
    for (auto& b : t.buildings) {
        if (b.id < 0) continue;
        if (b.id == 109) { b.status = kResearched; continue; }
        if (enabled(b.id)) {
            b.status = b.status == kNotAvailable ? kNotAvailable : kResearched;
        } else {
            b.status = b.status == kNotAvailable ? kNotAvailable : kAvailable;
        }
    }
    for (auto& u : t.units) {
        if (u.id < 0) continue;
        if (u.required_research >= 0) {
            const int s = state(u.required_research);
            if (s == 2) u.status = kResearching;
            else if (s == 3) u.status = kResearched;
        } else if (enabled(u.id) && u.status == kAvailable) {
            const BuildingRecord* b = BuildingById(t, u.upper_building);
            if (b != nullptr && b->status == kResearched) u.status = kResearched;
        }
    }
    for (auto& r : t.researches) {
        if (r.id < 0) continue;
        const int s = state(r.id);
        if (s == 2) r.status = kResearching;
        else if (s == 3) r.status = kResearched;
    }
}

Metrics MetricsFor(int screen_w, int screen_h) {
    (void)screen_w;
    Metrics m;
    // The left panel's width at each size (the techages frames 2, 0 and 1),
    // and the screen's scale, height over 600, as a float.
    if (screen_h <= 600) {
        m.origin_x = 340 + 150;
        m.scale = 1.0f;
    } else if (screen_h <= 768) {
        m.origin_x = 350 + 150;
        m.scale = 1.28f;
    } else {
        m.origin_x = 450 + 150;
        m.scale = 1.70666671f;
    }
    return m;
}

Layout Build(const Tree& tree_in, const Metrics& m, const Viewer& viewer) {
    Tree tree = tree_in;
    Layout out;
    const int node = m.node;

    // --- The building columns, in a fixed order. The second chain is put in
    // front of the first.
    static const int kChainA[] = {109, 621, 49, 103, 209, 584, 562, 84, 68, 50};
    static const int kChainB[] = {87, 12, 101, 276, 45, 199, 598, 79, 234, 235, 236, 72, 487, 117, 155,
                                  104, 82, 70};
    // Towers and walls draw no lines down (their line mode is made 6); the
    // bombard tower stands in its band's second row.
    auto quiet = [](int id) {
        return id == 598 || id == 79 || id == 234 || id == 235 || id == 236 || id == 72 || id == 487 ||
               id == 117 || id == 155;
    };
    std::vector<Work> chain_a, chain_b;
    auto columns_for = [&](int id, std::vector<Work>& chain) {
        BuildingRecord* b = BuildingById(tree, id);
        if (b == nullptr) return;
        if (quiet(id)) b->line_mode = 6;
        if (id == 236) b->location = 2;
        const AgeRecord* age = AgeById(tree, AgeOf(b->slots));
        if (age == nullptr) return;
        const int n = ColumnCount(tree, *b);
        const int middle = std::max(1, n - n / 2);
        for (int i = 1; i <= n; ++i) {
            Work w;
            w.building = b->id;
            w.building_age = AgeOf(b->slots);
            w.building_status = b->status;
            w.building_line_mode = b->line_mode;
            w.building_location = b->location;
            w.has_children = !b->buildings.empty();
            for (size_t c = 0; c < 5 && c < b->buildings.size(); ++c) w.children[c] = b->buildings[c];
            const int parent = FirstSlot(b->slots, SlotMode::Building);
            if (parent >= 0) {
                w.has_parent = true;
                w.parent_building = parent;
                const BuildingRecord* p = BuildingById(tree, parent);
                w.parent_age = p != nullptr ? AgeOf(p->slots) : -1;
            }
            w.box_column = i == middle;
            chain.push_back(w);
        }
    };
    for (int id : kChainA) columns_for(id, chain_a);
    for (int id : kChainB) columns_for(id, chain_b);
    std::vector<Work> work = std::move(chain_b);
    work.insert(work.end(), chain_a.begin(), chain_a.end());

    // --- What hangs in them.
    Placer placer{tree, work, {}, {}};
    for (int first = 0; first < static_cast<int>(work.size());) {
        int last = first;
        while (last + 1 < static_cast<int>(work.size()) && work[static_cast<size_t>(last + 1)].building ==
                                                                work[static_cast<size_t>(first)].building) {
            ++last;
        }
        const BuildingRecord* b = BuildingById(tree, work[static_cast<size_t>(first)].building);
        if (b != nullptr) {
            const int c0 = first;
            switch (b->id) {
            case 84:   // Market
                placer.Head(false, 19, c0, last, b);
                placer.Head(false, 15, c0, last, b);
                placer.Head(false, 23, c0 + 1, last, b);
                placer.Head(true, 128, c0 + 2, last, b);
                break;
            case 49:   // Siege Workshop
                placer.Head(true, 280, c0, last, b);
                placer.Head(true, 35, c0 + 1, last, b);
                placer.Head(true, 279, c0 + 2, last, b);
                break;
            case 45:   // Dock
                placer.Head(true, 13, c0, last, b);
                placer.Head(true, 17, c0, last, b);
                placer.Head(true, 529, c0, last, b);
                placer.Head(true, 545, c0 + 1, last, b);
                placer.Head(true, 527, c0 + 1, last, b);
                placer.Head(true, 539, c0 + 2, last, b);
                if (tree.civ == 11) placer.Head(true, 250, c0 + 3, last, b);
                break;
            case 101:  // Stable
                placer.Head(true, 329, c0, last, b);
                placer.Head(true, 38, c0 + 1, last, b);
                placer.Head(true, 448, c0 + 2, last, b);
                break;
            case 584:  // Mining Camp
                placer.Head(false, 278, c0, last, b);
                placer.Head(false, 55, c0 + 1, last, b);
                break;
            case 104:  // Monastery
                placer.Head(false, 316, c0, last, b);
                placer.Head(false, 230, c0, last, b);
                placer.Head(false, 252, c0 + 1, last, b);
                placer.Head(false, 233, c0 + 1, last, b);
                placer.Head(false, 231, c0 + 2, last, b);
                placer.Head(false, 45, c0 + 2, last, b);
                placer.Head(false, 319, c0 + 3, last, b);
                placer.Head(true, 125, c0 + 4, last, b);
                break;
            default: break;
            }
            placer.Rest(first, last, b);
        }
        first = last + 1;
    }
    // Each column's topmost box.
    for (Work& w : work) {
        for (int k = 0; k < 25; ++k) {
            if (w.cells[k].id >= 0) {
                w.cells[k].first = 1;
                break;
            }
        }
    }

    // --- The screen's columns.
    out.age_count = static_cast<int>(tree.ages.size());
    out.columns.resize(work.size());
    for (size_t i = 0; i < work.size(); ++i) {
        const Work& w = work[i];
        Column& c = out.columns[i];
        c.building = w.building;
        c.building_age = w.building_age;
        c.building_location = w.building_location;
        c.box_column = w.box_column;
        c.has_children = w.has_children;
        for (int k = 0; k < 5; ++k) c.children[k] = w.children[k];
        c.box.id = w.building;
        c.box.kind = w.box_column ? Kind::Building : Kind::None;
        c.box.status = w.building_status;
        c.box.line_mode = w.building_line_mode;
        c.box.location = w.building_location;
        c.box.age = w.building_age;
        c.box.has_line = w.has_parent;
        c.box.parent = w.parent_building;
        // The towers' and walls' own boxes show their upgrade researched.
        if (viewer.player != nullptr && viewer.player_civ == viewer.shown_civ) {
            const int tech = w.building == 155 ? 194 : w.building == 234 ? 140
                           : w.building == 235 ? 63 : w.building == 236 ? 64 : -1;
            const auto& states = viewer.player->tech_state;
            if (tech >= 0 && tech < static_cast<int>(states.size()) && states[static_cast<size_t>(tech)] == 3) {
                c.box.status = kResearched;
            }
        }
        for (int k = 0; k < 25; ++k) {
            const Cell& cell = w.cells[k];
            Node& n = c.cells[k];
            n.id = cell.id;
            n.kind = cell.kind;
            if (cell.kind == Kind::Research) {
                if (const ResearchRecord* r = ResearchById(tree, cell.id)) {
                    n.location = r->location;
                    n.age = AgeOf(r->slots);
                }
            } else if (cell.kind == Kind::Unit) {
                if (const UnitRecord* u = UnitById(tree, cell.id)) {
                    n.location = u->location;
                    n.age = AgeOf(u->slots);
                }
            }
            n.status = cell.status;
            // The hand cannoneer, for the Teutons, is always theirs to have.
            if (cell.id == 5 && viewer.shown_civ == 4) {
                if (viewer.player_civ >= 0 && viewer.player_civ == 4) {
                    if (n.status != kResearched) n.status = kAvailable;
                } else {
                    n.status = kResearched;
                }
            }
            n.line_mode = cell.line_mode;
            n.has_line = cell.has_line == 1;
            n.parent = cell.parent;
            n.parent_age = cell.parent_age;
            n.parent_row = cell.parent_row;
            n.first = cell.first == 1;
        }
    }

    // --- Band heights. Some of these figures are rounded to single floats
    // between steps.
    const float s = m.scale;
    const float six_s = static_cast<float>(static_cast<double>(s) * 6.0);
    const float seven_s = static_cast<float>(static_cast<double>(six_s) + static_cast<double>(s));
    double y = 0.0;
    out.bands[0] = 0;
    for (int a = 0; a < out.age_count && a < 5; ++a) {
        const AgeRecord& age = tree.ages[static_cast<size_t>(a)];
        const float base = static_cast<float>(static_cast<double>(seven_s) + y);
        float most = 0.0f;
        for (const Column& c : out.columns) {
            const float h = static_cast<float>(BandNeed(tree, c, age, node));
            if (h > most) most = h;
        }
        y = static_cast<double>(most) * static_cast<double>(s) + static_cast<double>(base);
        out.bands[a + 1] = Trunc(y);
    }

    // --- Columns' x: three pixels between columns, twenty between buildings.
    int gap = 20;
    for (size_t i = 0; i < out.columns.size(); ++i) {
        if (i > 0 && out.columns[i].building != out.columns[i - 1].building) gap += 20;
        out.columns[i].x = (node + 3) * static_cast<int>(i) + gap + m.origin_x;
        for (Node& n : out.columns[i].cells) n.x = out.columns[i].x;
        out.columns[i].box.x = out.columns[i].x;
    }
    const int groups_gap = gap;

    // --- The building's own box: its row in its age band.
    for (Column& c : out.columns) {
        if (!c.box_column) continue;
        const int ai = AgeIndexOfId(tree, c.building_age);
        double v = ai < 0 ? 0.0 : static_cast<double>((c.box.location - 1) * (node + 8));
        v = (v + 6.0) * static_cast<double>(s) + (ai < 0 ? 0 : out.bands[ai]);
        c.box.y = Trunc(v);
    }
    // --- Every other box: its row, below its building's own rows when the
    // building stands in the same band.
    for (Column& c : out.columns) {
        for (int a = 0; a < 5; ++a) {
            const int offset = AgeIndexOfId(tree, c.building_age) == a ? c.building_location * (node + 8) : 0;
            for (int r = 0; r < 5; ++r) {
                Node& n = c.cells[a * 5 + r];
                const int k = static_cast<int>(n.kind);
                if (k != 3 && k != 4 && k != 5) continue;
                const int ai = AgeIndexOfId(tree, n.age);
                double v = ai < 0 ? 0.0 : static_cast<double>((n.location - 1) * (node + 8) + offset);
                v = (v + 6.0) * static_cast<double>(s) + (ai < 0 ? 0 : out.bands[ai]);
                n.y = Trunc(v);
            }
        }
        // The column's first box, for the lines.
        for (int k = 0; k < 25; ++k) {
            if (c.cells[k].kind != Kind::None) {
                c.first_age = k / 5;
                c.first_row = k % 5;
                break;
            }
        }
    }
    out.height = out.bands[std::min(out.age_count, 5)];
    out.width = (node + 3) * static_cast<int>(out.columns.size()) + m.age_column + m.origin_x + groups_gap + 3;

    // --- A building with an even number of columns stands between
    // its middle two.
    for (Column& c : out.columns) {
        if (!c.box_column || c.building < 0) continue;
        const BuildingRecord* b = BuildingById(tree, c.building);
        if (b == nullptr) continue;
        if (ColumnCount(tree, *b) % 2 == 0) c.box.x += (node + 3) / 2;
    }
    // Groups, counted from 1.
    {
        int group = 1;
        int current = out.columns.empty() ? -1 : out.columns.front().building;
        for (Column& c : out.columns) {
            if (c.building != current) {
                ++group;
                current = c.building;
            }
            c.group = group;
        }
    }
    // Each column's span: from its building's box (or the box's foot) to its
    // lowest box's foot.
    {
        int box_y = -1, box_foot = -1;
        int current = -1;
        // The first building's box, found ahead.
        if (!out.columns.empty()) {
            current = out.columns.front().building;
            for (const Column& c : out.columns) {
                if (c.building != current) break;
                if (c.box_column) { box_y = c.box.y; box_foot = c.box.y + node; }
            }
        }
        for (size_t i = 0; i < out.columns.size(); ++i) {
            Column& c = out.columns[i];
            if (c.building != current) {
                current = c.building;
                box_y = box_foot = -1;
                for (size_t j = i; j < out.columns.size() && out.columns[j].building == current; ++j) {
                    if (out.columns[j].box_column) {
                        if (out.columns[j].box.y >= 0) {
                            box_y = out.columns[j].box.y;
                            box_foot = box_y + node;
                        }
                        break;
                    }
                }
            }
            int bottom;
            if (c.box_column && c.box.line_mode == 6) {
                bottom = c.box.y + node;
            } else {
                bottom = -1;
                for (const Node& n : c.cells) bottom = std::max(bottom, n.y + node);
            }
            c.top = c.box_column ? box_y : box_foot;
            c.bottom = bottom;
        }
    }
    // The towers stack in one column, and so do the walls; everything after
    // each closes up.
    auto stack = [&](int first_id, int last_id) {
        int from = -1, to = -1;
        for (size_t i = 0; i < out.columns.size(); ++i) {
            if (out.columns[i].box.id == first_id) from = static_cast<int>(i);
            if (out.columns[i].box.id == last_id) to = static_cast<int>(i);
            if (from >= 0 && to >= 0) break;
        }
        if (from < 0 || to < 0) return;
        const int x0 = out.columns[static_cast<size_t>(from)].x;
        const int shift = out.columns[static_cast<size_t>(to)].x - x0;
        if (shift <= 0) return;
        for (int i = from; i <= to; ++i) {
            Column& c = out.columns[static_cast<size_t>(i)];
            c.x = x0;
            c.box.x = x0;
        }
        for (size_t i = static_cast<size_t>(to) + 1; i < out.columns.size(); ++i) {
            Column& c = out.columns[i];
            c.x -= shift;
            c.box.x -= shift;
            for (Node& n : c.cells) {
                const int k = static_cast<int>(n.kind);
                if (k == 3 || k == 4 || k == 5) n.x -= shift;
            }
        }
        out.width += 20 - shift;
    };
    stack(598, 236);
    stack(72, 155);

    // --- The connecting lines, in the tree's own coordinates.
    const int half = node / 2;
    auto fill = [&](int x1, int y1, int x2, int y2) { out.lines.push_back(LineRect{x1, y1, x2, y2}); };
    // A line down to each box from the box above it in its line.
    for (const Column& c : out.columns) {
        for (int k = 0; k < 25; ++k) {
            const Node& n = c.cells[k];
            if (!n.has_line || n.parent < 0) continue;
            const int pk = n.parent_row + n.parent_age * 5;
            if (pk < 0 || pk >= 25) continue;
            const Node& p = c.cells[pk];
            const int x = p.x + half;
            fill(x, p.y + node + 1, x + 1, n.y - 1);
        }
    }
    // From each building down to the top of each of its columns.
    auto first_node = [&](const Column& c) -> const Node& { return c.cells[c.first_age * 5 + c.first_row]; };
    auto feed = [&](int box_col, int from, int to) {
        const Column& box = out.columns[static_cast<size_t>(box_col)];
        if (box.box.line_mode != 5) return;
        const int x = box.box.x + half;
        const int foot = box.box.y + node;
        fill(x, foot + 1, x + 1, foot + 4);
        const int bus = foot + 4;
        fill(first_node(out.columns[static_cast<size_t>(from)]).x + half, bus,
             first_node(out.columns[static_cast<size_t>(to)]).x + half + 1, bus + 1);
        for (int i = from; i <= to; ++i) {
            const Node& n = first_node(out.columns[static_cast<size_t>(i)]);
            const int nx = n.x + 32;
            fill(nx, bus + 1, nx + 1, n.y - 1);
        }
    };
    for (int from = 0; from < static_cast<int>(out.columns.size());) {
        int to = from;
        while (to + 1 < static_cast<int>(out.columns.size()) &&
               out.columns[static_cast<size_t>(to + 1)].building == out.columns[static_cast<size_t>(from)].building) {
            ++to;
        }
        int box_col = -1;
        for (int i = from; i <= to; ++i) {
            if (out.columns[static_cast<size_t>(i)].box_column) { box_col = i; break; }
        }
        if (box_col >= 0) feed(box_col, from, to);
        from = to + 1;
    }
    // From each building to the buildings it leads to. Not from the palisade,
    // the outpost, the gate or the keep: those stand in a stack with their
    // successors.
    for (const Column& c : out.columns) {
        if (!c.box_column || !c.has_children) continue;
        if (c.building == 72 || c.building == 598 || c.building == 487 || c.building == 235) continue;
        for (int child : c.children) {
            if (child < 0) break;
            const Column* target = nullptr;
            for (const Column& t : out.columns) {
                if (t.building == child && t.box_column) { target = &t; break; }
            }
            if (target == nullptr) continue;
            const int x = c.box.x + half;
            const int foot = c.box.y + node;
            fill(x, foot + 1, x + 1, foot + 4);
            const int tx = target->box.x + half;
            fill(x, foot + 4, tx + 1, foot + 5);
            fill(tx, foot + 5, tx + 1, target->box.y - 1);
        }
    }
    return out;
}

} // namespace Screens::TechTreeLayout
