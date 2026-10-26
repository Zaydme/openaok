// Prints the tables the data extractors need from EMPIRES2.DAT as JSON on
// stdout: terrains, units, rules, technologies, effects, graphics, sounds and
// the tech tree.
//
// genieutils parses the file; this walks the parsed structures and prints
// JSON. Built and run by datfile.py.
//
// Usage: dump_dat <empires2.dat>
#include <genie/dat/DatFile.h>

#include <algorithm>
#include <set>
#include <vector>
#include <cstdio>
#include <string>

namespace {

// Prints `text` as a quoted, escaped JSON string.
void PrintJsonString(const std::string& text) {
    std::putchar('"');
    for (const char c : text) {
        if (c == '"' || c == '\\') std::printf("\\%c", c);
        else if (static_cast<unsigned char>(c) < 0x20) std::printf("\\u%04x", c);
        else std::putchar(c);
    }
    std::putchar('"');
}

// Stand-in for a unit whose id falls outside the task table.
const std::vector<genie::Task> kNoTasks;

void PrintClassAmounts(const std::vector<genie::unit::AttackOrArmor>& list) {
    std::putchar('[');
    bool first = true;
    for (const genie::unit::AttackOrArmor& entry : list) {
        if (!first) std::printf(", ");
        first = false;
        std::printf("[%d, %d]", entry.Class, entry.Amount);
    }
    std::putchar(']');
}

// One unit's rules: every number the simulation plays by and a technology can
// change. Floats are printed to thousandths, the precision the engine rounds
// them to on load.
void PrintUnitRules(const genie::DatFile& dat, const genie::Unit& unit) {
    std::printf("{\"name\": ");
    PrintJsonString(unit.Name);
    std::printf(", \"type\": %d, \"class\": %d",
                static_cast<int>(unit.Type), static_cast<int>(unit.Class));
    std::printf(", \"hp\": %d, \"los\": %.3f, \"garrison\": %d",
                unit.HitPoints, unit.LineOfSight,
                static_cast<int>(unit.GarrisonCapacity));
    std::printf(", \"size\": [%.3f, %.3f]", unit.Size.x, unit.Size.y);
    std::printf(", \"speed\": %.3f, \"rotation\": %.3f",
                unit.Speed, unit.Moving.RotationSpeed);
    std::printf(", \"dead_unit\": %d, \"dying_graphic\": %d",
                unit.DeadUnitID, unit.DyingGraphic);
    std::printf(", \"undead_graphic\": %d", unit.UndeadGraphic);
    std::printf(", \"standing\": [%d, %d], \"walking\": %d",
                unit.StandingGraphic.first, unit.StandingGraphic.second,
                unit.Moving.WalkingGraphic);
    std::printf(", \"enabled\": %d, \"hide_in_editor\": %d",
                static_cast<int>(unit.Enabled),
                static_cast<int>(unit.HideInEditor));
    // The scenario editor's Heroes list: creatables marked as heroes.
    std::printf(", \"hero\": %d", static_cast<int>(unit.Creatable.HeroMode));
    std::printf(", \"terrain_restriction\": %d", unit.TerrainRestriction);
    // Placement beyond the restriction: a ground one of the footprint's
    // neighbours must be (a dock's shore), and a ground it must stand on.
    std::printf(", \"placement_side_terrain\": [%d, %d]",
                unit.PlacementSideTerrain.first, unit.PlacementSideTerrain.second);
    std::printf(", \"placement_terrain\": [%d, %d]",
                unit.PlacementTerrain.first, unit.PlacementTerrain.second);
    std::printf(", \"hill_mode\": %d", static_cast<int>(unit.HillMode));
    std::printf(", \"fog_visibility\": %d",
                static_cast<int>(unit.FogVisibility));
    std::printf(", \"interaction\": %d, \"combat_level\": %d",
                static_cast<int>(unit.InteractionMode),
                static_cast<int>(unit.CombatLevel));
    std::printf(", \"minimap_mode\": %d, \"minimap_colour\": %u",
                static_cast<int>(unit.MinimapMode), unit.MinimapColor);
    std::printf(", \"carry\": %d, \"decay\": %.3f",
                unit.ResourceCapacity, unit.ResourceDecay);
    std::printf(", \"storage\": [");
    bool first_store = true;
    for (const genie::Unit::ResourceStorage& store : unit.ResourceStorages) {
        if (store.Type < 0) continue;
        if (!first_store) std::printf(", ");
        first_store = false;
        std::printf("[%d, %.3f, %d]", store.Type, store.Amount,
                    static_cast<int>(store.Paid));
    }
    std::printf("]");
    std::printf(", \"damage_graphics\": [");
    bool first_damage = true;
    for (const genie::unit::DamageGraphic& damage : unit.DamageGraphics) {
        if (!first_damage) std::printf(", ");
        first_damage = false;
        std::printf("[%d, %d, %d]", damage.GraphicID,
                    static_cast<int>(damage.DamagePercent),
                    static_cast<int>(damage.ApplyMode));
    }
    std::printf("]");
    // Sounds, by the dat's sound table id.
    std::printf(", \"sounds\": {\"select\": %d, \"dying\": %d, \"train\": %d"
                ", \"damage\": %d, \"attack\": %d, \"move\": %d"
                ", \"construction\": %d}",
                unit.SelectionSound, unit.DyingSound, unit.TrainSound,
                unit.DamageSound, unit.Action.AttackSound,
                unit.Action.MoveSound, unit.Building.ConstructionSound);
    // Task-capable units.
    std::printf(", \"search_radius\": %.3f, \"work_rate\": %.3f",
                unit.Action.SearchRadius, unit.Action.WorkRate);
    // Combat.
    std::printf(", \"attacks\": ");
    PrintClassAmounts(unit.Combat.Attacks);
    std::printf(", \"armours\": ");
    PrintClassAmounts(unit.Combat.Armours);
    std::printf(", \"base_armour\": %d, \"terrain_defense\": %d",
                unit.Combat.BaseArmor, unit.Combat.DefenseTerrainBonus);
    std::printf(", \"max_range\": %.3f, \"min_range\": %.3f",
                unit.Combat.MaxRange, unit.Combat.MinRange);
    std::printf(", \"blast_width\": %.3f, \"blast_level\": %d",
                unit.Combat.BlastWidth,
                static_cast<int>(unit.Combat.BlastAttackLevel));
    std::printf(", \"reload\": %.3f, \"projectile\": %d, \"accuracy\": %d",
                unit.Combat.ReloadTime, unit.Combat.ProjectileUnitID,
                unit.Combat.AccuracyPercent);
    std::printf(", \"dispersion\": %.3f", unit.Combat.AccuracyDispersion);
    std::printf(", \"frame_delay\": %d", unit.Combat.FrameDelay);
    std::printf(", \"displacement\": [%.3f, %.3f, %.3f]",
                unit.Combat.GraphicDisplacement.x,
                unit.Combat.GraphicDisplacement.y,
                unit.Combat.GraphicDisplacement.z);
    std::printf(", \"attack_graphic\": %d", unit.Combat.AttackGraphic);
    std::printf(", \"break_off_combat\": %d",
                static_cast<int>(unit.Combat.BreakOffCombat));
    // Missiles.
    std::printf(", \"missile\": {\"type\": %d, \"smart\": %d, \"hit\": %d"
                ", \"vanish\": %d, \"area\": %d, \"arc\": %.3f}",
                static_cast<int>(unit.Missile.ProjectileType),
                static_cast<int>(unit.Missile.SmartMode),
                static_cast<int>(unit.Missile.HitMode),
                static_cast<int>(unit.Missile.VanishMode),
                static_cast<int>(unit.Missile.AreaEffectSpecials),
                unit.Missile.ProjectileArc);
    // Trainable units.
    std::printf(", \"costs\": [");
    bool first_cost = true;
    // [resource, amount, paid]: a slot with paid 0 is a requirement that is
    // checked without being deducted, such as population.
    for (const auto& cost : unit.Creatable.ResourceCosts) {
        if (cost.Type < 0) continue;
        if (!first_cost) std::printf(", ");
        first_cost = false;
        std::printf("[%d, %d, %d]", cost.Type, cost.Amount,
                    static_cast<int>(cost.Paid));
    }
    std::printf("]");
    std::printf(", \"train_time\": %d, \"train_loc\": %d, \"button\": %d",
                unit.Creatable.TrainTime, unit.Creatable.TrainLocationID,
                static_cast<int>(unit.Creatable.ButtonID));
    std::printf(", \"missiles\": %.3f, \"max_missiles\": %d",
                unit.Creatable.TotalProjectiles,
                static_cast<int>(unit.Creatable.MaxTotalProjectiles));
    std::printf(", \"missile_area\": [%.3f, %.3f, %.3f]",
                unit.Creatable.ProjectileSpawningArea[0],
                unit.Creatable.ProjectileSpawningArea[1],
                unit.Creatable.ProjectileSpawningArea[2]);
    std::printf(", \"secondary_projectile\": %d",
                unit.Creatable.SecondaryProjectileUnit);
    std::printf(", \"garrison_graphic\": %d", unit.Creatable.GarrisonGraphic);
    // Buildings.
    std::printf(", \"construction_graphic\": %d",
                unit.Building.ConstructionGraphicID);
    std::printf(", \"tech\": %d, \"head_unit\": %d, \"stack_unit\": %d"
                ", \"transform_unit\": %d",
                unit.Building.TechID, unit.Building.HeadUnit,
                unit.Building.StackUnitID, unit.Building.TransformUnit);
    std::printf(", \"foundation_terrain\": %d",
                unit.Building.FoundationTerrainID);
    std::printf(", \"garrison_type\": %d, \"garrison_heal\": %.3f",
                static_cast<int>(unit.Building.GarrisonType),
                unit.Building.GarrisonHealRate);
    std::printf(", \"adjacent_mode\": %d",
                static_cast<int>(unit.Building.AdjacentMode));
    std::printf(", \"name_string\": %d, \"help_string\": %d, \"icon\": %d",
                unit.LanguageDLLName, unit.LanguageDLLHelp, unit.IconID);
    // What the unit can do, and to what: a forager gathers forage bushes, a
    // hunter kills deer. In this dat version the task list lives in a table
    // parallel to the units, indexed by id.
    const std::vector<genie::Task>& tasks =
        (unit.ID >= 0 && unit.ID < static_cast<int>(dat.UnitHeaders.size()))
            ? dat.UnitHeaders[unit.ID].TaskList : kNoTasks;
    std::printf(", \"tasks\": [");
    bool first_task = true;
    for (const genie::Task& task : tasks) {
        if (!first_task) std::printf(", ");
        first_task = false;
        // [action, target class, target unit, resource in, work range,
        // work value, resource out, carrying graphic, auto search]: what a
        // gather task takes from its target and what it hands over (a fishing
        // ship takes fish, 17, and its load is food, 0), what the unit wears
        // coming back with the load (a trade cart's trade task names the full
        // cart's walk, 1145, TCARTF), and whether the unit looks for another
        // target when this one is gone (every villager job's gather task does).
        std::printf("[%d, %d, %d, %d, %.3f, %.3f, %d, %d, %d]", static_cast<int>(task.ActionType),
                    task.ClassID, task.UnitID, task.ResourceIn, task.WorkRange,
                    task.WorkValue1, task.ResourceOut, task.CarryingGraphicID,
                    static_cast<int>(task.AutoSearchTargets));
    }
    std::printf("]");
    std::printf("}");
}

// Art and sound the rules refer to by id: each graphic's deltas -- the
// sub-graphics a composite graphic is drawn from, a burning building's flames
// among them -- and the sound it plays, and the sound table itself, whose
// entries each pick one of several WAVs in sounds.drs.
void DumpMedia(const genie::DatFile& dat) {
    std::printf(",\n  \"graphic_media\": {\n");
    bool first = true;
    for (size_t id = 0; id < dat.Graphics.size(); ++id) {
        const genie::Graphic& graphic = dat.Graphics[id];
        // A graphic's sounds can also be tied to frames, one set per facing:
        // a sword's ring on the frame the blow lands, an axe's chop on the
        // frame it bites.
        bool angle_sounds = false;
        if (graphic.AngleSoundsUsed) {
            for (const genie::GraphicAngleSound& a : graphic.AngleSounds) {
                if (a.SoundID >= 0 || a.SoundID2 >= 0 || a.SoundID3 >= 0) angle_sounds = true;
            }
        }
        if (graphic.Deltas.empty() && graphic.SoundID < 0 && !angle_sounds) continue;
        if (!first) std::printf(",\n");
        first = false;
        std::printf("    \"%zu\": {\"sound\": %d, \"frames\": %d, \"angle_sounds\": [",
                    id, graphic.SoundID, graphic.FrameCount);
        if (angle_sounds) {
            bool first_angle = true;
            for (const genie::GraphicAngleSound& a : graphic.AngleSounds) {
                if (!first_angle) std::printf(", ");
                first_angle = false;
                std::printf("[[%d, %d], [%d, %d], [%d, %d]]", a.FrameNum, a.SoundID,
                            a.FrameNum2, a.SoundID2, a.FrameNum3, a.SoundID3);
            }
        }
        std::printf("], \"deltas\": [");
        bool first_delta = true;
        for (const genie::GraphicDelta& delta : graphic.Deltas) {
            if (!first_delta) std::printf(", ");
            first_delta = false;
            std::printf("[%d, %d, %d, %d]", delta.GraphicID, delta.OffsetX,
                        delta.OffsetY, delta.DisplayAngle);
        }
        std::printf("]}");
    }
    std::printf("\n  },\n  \"sounds\": {\n");
    first = true;
    for (size_t id = 0; id < dat.Sounds.size(); ++id) {
        const genie::Sound& sound = dat.Sounds[id];
        if (sound.Items.empty()) continue;
        if (!first) std::printf(",\n");
        first = false;
        std::printf("    \"%zu\": {\"delay\": %d, \"total\": %d, \"items\": [",
                    id, sound.PlayDelay, sound.TotalProbability);
        bool first_item = true;
        for (const genie::SoundItem& item : sound.Items) {
            if (!first_item) std::printf(", ");
            first_item = false;
            std::printf("{\"resource\": %d, \"probability\": %d, \"civ\": %d, \"file\": ",
                        item.ResourceID, item.Probability, item.Civilization);
            PrintJsonString(item.FileName);
            std::printf("}");
        }
        std::printf("]}");
    }
    std::printf("\n  }");
}

// The rules of the game as the dat states them: every unit's numbers, every
// technology and every effect a technology applies, and each civilisation's
// tech tree and starting resources. Each civ has its own copy of the unit
// table; the one dumped is the first playable civ's, with Gaia's for the units
// only Gaia owns.
void DumpRules(const genie::DatFile& dat) {
    std::printf(",\n  \"rules_units\": {\n");
    const genie::Civ* gaia = dat.Civs.empty() ? nullptr : &dat.Civs[0];
    const genie::Civ* first_civ = dat.Civs.size() > 1 ? &dat.Civs[1] : nullptr;
    const size_t unit_count = std::max(gaia ? gaia->Units.size() : 0,
                                       first_civ ? first_civ->Units.size() : 0);
    bool first = true;
    for (size_t id = 0; id < unit_count; ++id) {
        const genie::Unit* unit = nullptr;
        if (first_civ && id < first_civ->Units.size() &&
            first_civ->Units[id].ID >= 0 && !first_civ->Units[id].Name.empty()) {
            unit = &first_civ->Units[id];
        } else if (gaia && id < gaia->Units.size() && gaia->Units[id].ID >= 0 &&
                   !gaia->Units[id].Name.empty()) {
            unit = &gaia->Units[id];
        }
        if (!unit) continue;
        if (!first) std::printf(",\n");
        first = false;
        std::printf("    \"%zu\": ", id);
        PrintUnitRules(dat, *unit);
    }

    // Which units each civ starts with enabled. The flag is per civ: Gaia has
    // its wolves and deer, a player has its villagers and Town Center.
    std::printf("\n  },\n  \"civ_rules\": [");
    for (size_t i = 0; i < dat.Civs.size(); ++i) {
        const genie::Civ& civ = dat.Civs[i];
        if (i) std::printf(",");
        std::printf("\n    {\"id\": %zu, \"name\": ", i);
        PrintJsonString(civ.Name);
        std::printf(", \"tech_tree\": %d, \"team_bonus\": %d",
                    civ.TechTreeID, civ.TeamBonusID);
        std::printf(", \"icon_set\": %d", static_cast<int>(civ.IconSet));
        std::printf(", \"unique_techs\": [%d, %d, %d, %d]",
                    civ.UniqueUnitsTechs[0], civ.UniqueUnitsTechs[1],
                    civ.UniqueUnitsTechs[2], civ.UniqueUnitsTechs[3]);
        std::printf(", \"enabled\": [");
        bool first_enabled = true;
        for (const genie::Unit& unit : civ.Units) {
            if (unit.ID < 0 || !unit.Enabled) continue;
            if (!first_enabled) std::printf(", ");
            first_enabled = false;
            std::printf("%d", unit.ID);
        }
        std::printf("], \"resources\": [");
        for (size_t r = 0; r < civ.Resources.size(); ++r) {
            if (r) std::printf(", ");
            std::printf("%.3f", civ.Resources[r]);
        }
        std::printf("]}");
    }

    std::printf("\n  ],\n  \"techs\": {\n");
    bool first_tech = true;
    for (size_t id = 0; id < dat.Techs.size(); ++id) {
        const genie::Tech& tech = dat.Techs[id];
        if (tech.Name.empty() && tech.EffectID < 0) continue;
        if (!first_tech) std::printf(",\n");
        first_tech = false;
        std::printf("    \"%zu\": {\"name\": ", id);
        PrintJsonString(tech.Name);
        std::printf(", \"required\": [");
        bool first_required = true;
        for (const int16_t required : tech.RequiredTechs) {
            if (required < 0) continue;
            if (!first_required) std::printf(", ");
            first_required = false;
            std::printf("%d", required);
        }
        std::printf("], \"required_count\": %d", tech.RequiredTechCount);
        std::printf(", \"costs\": [");
        bool first_cost = true;
        for (const auto& cost : tech.ResourceCosts) {
            if (cost.Type < 0) continue;
            if (!first_cost) std::printf(", ");
            first_cost = false;
            std::printf("[%d, %d, %d]", cost.Type, cost.Amount,
                        static_cast<int>(cost.Paid));
        }
        std::printf("], \"civ\": %d, \"full_tech_mode\": %d", tech.Civ,
                    tech.FullTechMode);
        std::printf(", \"location\": %d, \"time\": %d, \"effect\": %d",
                    tech.ResearchLocation, tech.ResearchTime, tech.EffectID);
        std::printf(", \"type\": %d, \"icon\": %d, \"button\": %d",
                    tech.Type, tech.IconID, static_cast<int>(tech.ButtonID));
        std::printf(", \"name_string\": %u, \"help_string\": %d",
                    tech.LanguageDLLName, tech.LanguageDLLHelp);
        std::printf(", \"description_string\": %u", tech.LanguageDLLDescription);
        std::printf("}");
    }

    std::printf("\n  },\n  \"effects\": {\n");
    bool first_effect = true;
    for (size_t id = 0; id < dat.Effects.size(); ++id) {
        const genie::Effect& effect = dat.Effects[id];
        if (effect.Name.empty() && effect.EffectCommands.empty()) continue;
        if (!first_effect) std::printf(",\n");
        first_effect = false;
        std::printf("    \"%zu\": {\"name\": ", id);
        PrintJsonString(effect.Name);
        std::printf(", \"commands\": [");
        bool first_command = true;
        for (const genie::EffectCommand& command : effect.EffectCommands) {
            if (!first_command) std::printf(", ");
            first_command = false;
            std::printf("[%d, %d, %d, %d, %.3f]",
                        static_cast<int>(command.Type), command.TargetUnit,
                        command.UnitClassID,
                        static_cast<int>(command.AttributeID), command.Amount);
        }
        std::printf("]}");
    }
    std::printf("\n  }");
}

void PrintIntList(const std::vector<int32_t>& list) {
    std::putchar('[');
    for (size_t i = 0; i < list.size(); ++i) {
        if (i) std::printf(", ");
        std::printf("%d", list[i]);
    }
    std::putchar(']');
}

void PrintByteList(const std::vector<int8_t>& list) {
    std::putchar('[');
    for (size_t i = 0; i < list.size(); ++i) {
        if (i) std::printf(", ");
        std::printf("%d", static_cast<int>(list[i]));
    }
    std::putchar(']');
}

// The "connection" slots every tech tree record carries: up to ten other
// records, each with its mode (0 age, 1 building, 2 unit, 3 research).
void PrintCommon(const genie::techtree::Common& common) {
    std::printf("\"slots\": %d, \"slot_ids\": ", common.SlotsUsed);
    PrintIntList(common.UnitResearch);
    std::printf(", \"slot_modes\": ");
    PrintIntList(common.Mode);
}

// The technology tree screen's own table, record by record, exactly as the dat
// stores it: the ages, the buildings with what each holds per age, the units
// and the researches with the building above them and their place in the age.
// Nothing here is interpreted.
void DumpTechTree(const genie::DatFile& dat) {
    const genie::TechTree& tree = dat.TechTree;
    std::printf(",\n  \"tech_tree\": {\n    \"total_groups\": %d,\n    \"ages\": [",
                tree.TotalUnitTechGroups);
    for (size_t i = 0; i < tree.TechTreeAges.size(); ++i) {
        const genie::TechTreeAge& age = tree.TechTreeAges[i];
        std::printf("%s\n      {\"id\": %d, \"status\": %d, \"buildings\": ", i ? "," : "",
                    age.ID, static_cast<int>(age.Status));
        PrintIntList(age.Buildings);
        std::printf(", \"units\": ");
        PrintIntList(age.Units);
        std::printf(", \"techs\": ");
        PrintIntList(age.Techs);
        std::printf(", ");
        PrintCommon(age.Common);
        std::printf(", \"building_levels\": %d, \"buildings_per_zone\": ",
                    static_cast<int>(age.NumBuildingLevels));
        PrintByteList(age.BuildingsPerZone);
        std::printf(", \"group_length_per_zone\": ");
        PrintByteList(age.GroupLengthPerZone);
        std::printf(", \"max_age_length\": %d, \"line_mode\": %d}",
                    static_cast<int>(age.MaxAgeLength), age.LineMode);
    }
    std::printf("\n    ],\n    \"buildings\": [");
    for (size_t i = 0; i < tree.BuildingConnections.size(); ++i) {
        const genie::BuildingConnection& b = tree.BuildingConnections[i];
        std::printf("%s\n      {\"id\": %d, \"status\": %d, \"buildings\": ", i ? "," : "",
                    b.ID, static_cast<int>(b.Status));
        PrintIntList(b.Buildings);
        std::printf(", \"units\": ");
        PrintIntList(b.Units);
        std::printf(", \"techs\": ");
        PrintIntList(b.Techs);
        std::printf(", ");
        PrintCommon(b.Common);
        std::printf(", \"location_in_age\": %d, \"units_techs_total\": ",
                    static_cast<int>(b.LocationInAge));
        PrintByteList(b.UnitsTechsTotal);
        std::printf(", \"units_techs_first\": ");
        PrintByteList(b.UnitsTechsFirst);
        std::printf(", \"line_mode\": %d, \"enabling_research\": %d}", b.LineMode,
                    b.EnablingResearch);
    }
    std::printf("\n    ],\n    \"units\": [");
    for (size_t i = 0; i < tree.UnitConnections.size(); ++i) {
        const genie::UnitConnection& u = tree.UnitConnections[i];
        std::printf("%s\n      {\"id\": %d, \"status\": %d, \"upper_building\": %d, ",
                    i ? "," : "", u.ID, static_cast<int>(u.Status), u.UpperBuilding);
        PrintCommon(u.Common);
        std::printf(", \"vertical_line\": %d, \"units\": ", u.VerticalLine);
        PrintIntList(u.Units);
        std::printf(", \"location_in_age\": %d, \"required_research\": %d"
                    ", \"line_mode\": %d, \"enabling_research\": %d}",
                    u.LocationInAge, u.RequiredResearch, u.LineMode, u.EnablingResearch);
    }
    std::printf("\n    ],\n    \"researches\": [");
    for (size_t i = 0; i < tree.ResearchConnections.size(); ++i) {
        const genie::ResearchConnection& r = tree.ResearchConnections[i];
        std::printf("%s\n      {\"id\": %d, \"status\": %d, \"upper_building\": %d, \"buildings\": ",
                    i ? "," : "", r.ID, static_cast<int>(r.Status), r.UpperBuilding);
        PrintIntList(r.Buildings);
        std::printf(", \"units\": ");
        PrintIntList(r.Units);
        std::printf(", \"techs\": ");
        PrintIntList(r.Techs);
        std::printf(", ");
        PrintCommon(r.Common);
        std::printf(", \"vertical_line\": %d, \"location_in_age\": %d, \"line_mode\": %d}",
                    r.VerticalLine, r.LocationInAge, r.LineMode);
    }
    std::printf("\n    ]\n  }");
}

} // namespace

// The graphic table (every graphic the dat defines) and Gaia's unit table
// (every unit slot it fills), with the fields the engine's lookups use.
void DumpTables(const genie::DatFile& dat) {
    std::printf(",\n  \"graphic_table\": {");
    bool first = true;
    for (size_t id = 0; id < dat.Graphics.size(); ++id) {
        if (id < dat.GraphicPointers.size() && dat.GraphicPointers[id] == 0) continue;
        const genie::Graphic& graphic = dat.Graphics[id];
        std::printf("%s\n    \"%zu\": {\"name\": ", first ? "" : ",", id);
        first = false;
        PrintJsonString(graphic.Name);
        std::printf(", \"filename\": ");
        PrintJsonString(graphic.FileName);
        std::printf(", \"slp_id\": %d, \"frame_count\": %u, \"angle_count\": %u, \"layer\": %d}",
                    graphic.SLP, graphic.FrameCount, graphic.AngleCount, graphic.Layer);
    }
    std::printf("\n  },\n  \"unit_table\": {");
    first = true;
    if (!dat.Civs.empty()) {
        const genie::Civ& gaia = dat.Civs[0];
        for (size_t id = 0; id < gaia.Units.size(); ++id) {
            if (id < gaia.UnitPointers.size() && gaia.UnitPointers[id] == 0) continue;
            const genie::Unit& unit = gaia.Units[id];
            std::printf("%s\n    \"%zu\": {\"name\": ", first ? "" : ",", id);
            first = false;
            PrintJsonString(unit.Name);
            std::printf(", \"id\": %d, \"type\": %d, \"class\": %d, \"hp\": %d"
                        ", \"idle_graphic0\": %d, \"idle_graphic1\": %d, \"dying_graphic\": %d"
                        ", \"walking_graphic0\": %d, \"construction_graphic\": %d}",
                        static_cast<int>(id), static_cast<int>(unit.Type), static_cast<int>(unit.Class),
                        unit.HitPoints, unit.StandingGraphic.first, unit.StandingGraphic.second,
                        unit.DyingGraphic, unit.Moving.WalkingGraphic,
                        unit.Building.ConstructionGraphicID);
        }
    }
    std::printf("\n  }");
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: dump_dat <empires2.dat>\n");
        return 2;
    }

    genie::DatFile dat;
    dat.setGameVersion(genie::GV_AoK);
    try {
        dat.load(argv[1]);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "failed to read %s: %s\n", argv[1], error.what());
        return 1;
    }

    std::printf("{\n");
    std::printf("  \"version\": ");
    PrintJsonString(dat.FileVersion);
    std::printf(",\n  \"terrains\": {\n");

    bool first_terrain = true;
    for (size_t id = 0; id < dat.TerrainBlock.Terrains.size(); ++id) {
        const genie::Terrain& terrain = dat.TerrainBlock.Terrains[id];
        // Unused slots carry no art and no name.
        if (terrain.SLP < 0 && terrain.Name.empty()) continue;

        if (!first_terrain) std::printf(",\n");
        first_terrain = false;

        std::printf("    \"%zu\": {", id);
        std::printf("\"name\": ");
        PrintJsonString(terrain.Name);
        std::printf(", \"slp\": %d", terrain.SLP);
        std::printf(", \"sound\": %d", terrain.SoundID);
        std::printf(", \"minimap\": [%u, %u, %u]",
                    terrain.Colors[0], terrain.Colors[1], terrain.Colors[2]);
        std::printf(", \"is_water\": %d", terrain.IsWater);
        std::printf(", \"passable\": %d", terrain.PassableTerrain);
        std::printf(", \"impassable\": %d", terrain.ImpassableTerrain);
        std::printf(", \"draw_as\": %d", terrain.TerrainToDraw);
        // Animated ground (water): its frames and the pause between loops.
        // Then how it blends with its neighbours.
        std::printf(", \"animated\": %d, \"anim_frames\": %d, \"pause_frames\": %d"
                    ", \"interval\": %.4f, \"pause\": %.4f",
                    terrain.IsAnimated, terrain.AnimationFrames, terrain.PauseFames,
                    terrain.Interval, terrain.PauseBetweenLoops);
        std::printf(", \"blend_priority\": %d, \"blend_type\": %d",
                    terrain.BlendPriority, terrain.BlendType);

        // What grows on it: forest ground carries a tree on every tile, and
        // this is the table that says which tree and how densely.
        std::printf(", \"objects\": [");
        bool first_object = true;
        const int used = terrain.NumberOfTerrainUnitsUsed;
        for (int i = 0; i < used && i < static_cast<int>(terrain.TerrainUnitID.size()); ++i) {
            if (terrain.TerrainUnitID[i] <= 0) continue;
            if (!first_object) std::printf(", ");
            first_object = false;
            std::printf("{\"unit\": %d, \"density\": %d, \"centering\": %d}",
                        terrain.TerrainUnitID[i], terrain.TerrainUnitDensity[i],
                        terrain.TerrainUnitCentering[i]);
        }
        std::printf("]}");
    }

    // Where each kind of unit may go and build: per terrain restriction, a
    // figure per terrain -- 0 impassable, above 0 passable, above 0.05
    // buildable.
    std::printf("\n  },\n  \"terrain_restrictions\": [");
    for (size_t r = 0; r < dat.TerrainRestrictions.size(); ++r) {
        if (r > 0) std::printf(", ");
        std::printf("[");
        const auto& values = dat.TerrainRestrictions[r].PassableBuildableDmgMultiplier;
        for (size_t t = 0; t < values.size(); ++t) {
            if (t > 0) std::printf(", ");
            std::printf("%.3f", values[t]);
        }
        std::printf("]");
    }
    std::printf("]");

    // The SLP of every graphic a named unit can reach. The dat carries dead
    // table entries ("OLD-ACADEMY") whose graphics are not shipped; those are
    // left out.
    std::printf(",\n  \"reachable_graphics\": [");
    {
        std::set<int> reachable;
        for (const genie::Civ& civ : dat.Civs) {
            for (const genie::Unit& unit : civ.Units) {
                if (unit.ID < 0 || unit.Name.empty()) continue;
                // Dead entries are named but disabled and unbuildable.
                if (unit.Name.rfind("OLD-", 0) == 0) continue;
                reachable.insert(unit.StandingGraphic.first);
                reachable.insert(unit.StandingGraphic.second);
                reachable.insert(unit.DyingGraphic);
                reachable.insert(unit.Combat.AttackGraphic);
                reachable.insert(unit.Moving.WalkingGraphic);
                reachable.insert(unit.Moving.RunningGraphic);
                reachable.insert(unit.Building.ConstructionGraphicID);
                for (const genie::unit::DamageGraphic& damage : unit.DamageGraphics) {
                    reachable.insert(damage.GraphicID);
                }
            }
        }
        bool first = true;
        for (int graphic_id : reachable) {
            if (graphic_id < 0 ||
                graphic_id >= static_cast<int>(dat.Graphics.size())) {
                continue;
            }
            const int slp = dat.Graphics[graphic_id].SLP;
            if (slp < 0) continue;
            if (!first) std::printf(", ");
            first = false;
            std::printf("%d", slp);
        }
        std::printf("],\n  \"missing_detail\": [");
        // Which graphic each reachable SLP belongs to, so an absent one can be
        // named rather than just counted.
        first = true;
        for (int graphic_id : reachable) {
            if (graphic_id < 0 ||
                graphic_id >= static_cast<int>(dat.Graphics.size())) {
                continue;
            }
            const genie::Graphic& graphic = dat.Graphics[graphic_id];
            if (graphic.SLP < 0) continue;
            if (!first) std::printf(", ");
            first = false;
            std::printf("{\"slp\": %d, \"name\": ", graphic.SLP);
            PrintJsonString(graphic.Name);
            std::printf("}");
        }
    }
    std::printf("],\n  \"graphic_slps\": [");
    {
        bool first = true;
        for (const genie::Graphic& graphic : dat.Graphics) {
            if (graphic.SLP < 0) continue;
            if (!first) std::printf(", ");
            first = false;
            std::printf("%d", graphic.SLP);
        }
    }
    std::printf("],\n  \"units\": {\n");
    // Everything the interface shows about a unit. The playable civ only; map
    // objects live on Gaia and are dumped separately below.
    if (dat.Civs.size() > 1) {
        bool first_unit = true;
        for (const genie::Unit& unit : dat.Civs[1].Units) {
            if (unit.ID < 0 || unit.Name.empty()) continue;
            if (!first_unit) std::printf(",\n");
            first_unit = false;

            int pierce = 0;
            for (const genie::unit::AttackOrArmor& armour : unit.Combat.Armours) {
                if (armour.Class == 3) pierce = armour.Amount;   // pierce class
            }

            std::printf("    \"%d\": {\"name\": ", unit.ID);
            PrintJsonString(unit.Name);
            std::printf(", \"icon\": %d", unit.IconID);
            std::printf(", \"button\": %d", unit.Creatable.ButtonID);
            std::printf(", \"train_loc\": %d", unit.Creatable.TrainLocationID);
            std::printf(", \"train_time\": %d", unit.Creatable.TrainTime);
            std::printf(", \"interface_kind\": %d",
                        static_cast<int>(unit.InterfaceKind));
            std::printf(", \"hp\": %d", unit.HitPoints);
            // The dat's unit type: 70 a living unit, 80 a building, 10 a
            // static object. A sheep's hit points are shown; a tree's are for
            // siege damage only.
            std::printf(", \"unit_type\": %d", static_cast<int>(unit.Type));
            // Its gameplay class: 4 is a villager, the class that does economic
            // work.
            std::printf(", \"unit_class\": %d", static_cast<int>(unit.Class));
            std::printf(", \"attack\": %d", unit.Combat.DisplayedAttack);
            std::printf(", \"melee_armour\": %d", unit.Combat.DisplayedMeleeArmour);
            std::printf(", \"pierce_armour\": %d", pierce);
            std::printf(", \"range\": %d",
                        static_cast<int>(unit.Combat.DisplayedRange));
            std::printf(", \"line_of_sight\": %d",
                        static_cast<int>(unit.LineOfSight));
            std::printf(", \"attack_graphic\": %d", unit.Combat.AttackGraphic);
            // What it leaves behind when it dies: for a building, the rubble.
            std::printf(", \"dead_unit\": %d", unit.DeadUnitID);
            // The art a building wears while it is going up, and how much
            // ground it stands on. Clearance is a half-extent in tiles, so a
            // Town Center's 2.0 covers four tiles across.
            std::printf(", \"construction_graphic\": %d",
                        unit.Building.ConstructionGraphicID);
            std::printf(", \"clearance\": [%.3f, %.3f]",
                        unit.ClearanceSize.x, unit.ClearanceSize.y);
            // How it is shown selected: the outline's radii in tiles, the hit
            // point bar's height, whether either is shown at all, and the
            // obstruction type that makes the outline round (5) or square.
            std::printf(", \"outline\": [%.3f, %.3f], \"hp_bar_height\": %.3f"
                        ", \"selection_effect\": %d, \"obstruction\": %d",
                        unit.OutlineSize.x, unit.OutlineSize.y, unit.HPBarHeight,
                        static_cast<int>(unit.SelectionEffect),
                        static_cast<int>(unit.ObstructionType));
            std::printf(", \"name_string\": %d", unit.LanguageDLLName);
            std::printf(", \"help_string\": %d", unit.LanguageDLLHelp);

            // Multi-part buildings: the annex units and where they sit,
            // relative to the parent's tile.
            std::printf(", \"annexes\": [");
            bool first_annex = true;
            for (const genie::unit::BuildingAnnex& annex : unit.Building.Annexes) {
                if (annex.UnitID < 0) continue;
                if (!first_annex) std::printf(", ");
                first_annex = false;
                std::printf("{\"unit\": %d, \"dx\": %.4f, \"dy\": %.4f}",
                            annex.UnitID, annex.Misplacement.first,
                            annex.Misplacement.second);
            }
            std::printf("]");

            std::printf(", \"costs\": [");
            bool first_cost = true;
            for (const genie::unit::Creatable::ResourceCost& cost : unit.Creatable.ResourceCosts) {
                if (cost.Type < 0 || cost.Amount <= 0) continue;
                if (!first_cost) std::printf(", ");
                first_cost = false;
                std::printf("[%d, %d]", cost.Type, cost.Amount);
            }
            std::printf("]");

            // How fast this unit works: a villager's gather rate is its
            // action's work rate.
            std::printf(", \"work_rate\": %.4f", unit.Action.WorkRate);
            // How much this unit can carry before it has to walk a load home:
            // ten for a villager.
            std::printf(", \"carry_capacity\": %d", unit.ResourceCapacity);
            std::printf(", \"storage\": [");
            bool first_store = true;
            for (const genie::Unit::ResourceStorage& store : unit.ResourceStorages) {
                if (store.Type < 0) continue;
                if (!first_store) std::printf(", ");
                first_store = false;
                std::printf("{\"type\": %d, \"amount\": %.3f, \"mode\": %d}",
                            store.Type, store.Amount,
                            static_cast<int>(store.Paid));
            }
            std::printf("]");

            // Which buildings this unit may hand a load to: a lumberjack lists
            // the Town Centre and the Lumber Camp, a gold miner the Town Centre
            // and the Mining Camp.
            std::printf(", \"drop_sites\": [");
            bool first_site = true;
            for (const int16_t site : unit.Action.DropSites) {
                if (site < 0) continue;
                if (!first_site) std::printf(", ");
                first_site = false;
                std::printf("%d", site);
            }
            std::printf("]");

            // The unit's task list, which holds its work animations (the
            // hammering, the chopping, the mining). Each task names an action
            // (101 is Build, 5 is gather/rebuild) and up to four graphics for
            // its phases: walking there with the tool out, closing in, doing
            // the work, and carrying the result home. In AoK the lists sit in a
            // table parallel to the units, indexed by unit id;
            // `Unit::Action::TaskList` is the AoE1/RoR layout and is empty here.
            std::printf(", \"tasks\": [");
            bool first_task = true;
            const std::vector<genie::Task>& tasks =
                (unit.ID >= 0 && unit.ID < static_cast<int>(dat.UnitHeaders.size()))
                    ? dat.UnitHeaders[unit.ID].TaskList
                    : kNoTasks;
            for (const genie::Task& task : tasks) {
                if (!first_task) std::printf(", ");
                first_task = false;
                std::printf("{\"action\": %d", static_cast<int>(task.ActionType));
                std::printf(", \"class\": %d", task.ClassID);
                std::printf(", \"unit\": %d", task.UnitID);
                std::printf(", \"resource_in\": %d", task.ResourceIn);
                std::printf(", \"moving\": %d", task.MovingGraphicID);
                std::printf(", \"proceeding\": %d", task.ProceedingGraphicID);
                std::printf(", \"working\": %d", task.WorkingGraphicID);
                std::printf(", \"carrying\": %d", task.CarryingGraphicID);
                std::printf(", \"work_range\": %.4f", task.WorkRange);
                std::printf(", \"work_rate\": %.4f", task.WorkValue1);
                // For a gather task this is the carry capacity: how much of
                // the resource the worker can carry before it must walk a load
                // home. (The field is labelled for conversion timing, which is
                // what a monk uses it for.)
                std::printf(", \"work_value2\": %.4f", task.WorkValue2);
                std::printf("}");
            }
            std::printf("]}");
        }
    }

    // How long one frame of each graphic lasts, in milliseconds.
    std::printf("\n  },\n  \"graphic_ms\": {");
    {
        bool first_timing = true;
        for (size_t id = 0; id < dat.Graphics.size(); ++id) {
            const genie::Graphic& graphic = dat.Graphics[id];
            if (graphic.SLP < 0 || graphic.FrameDuration <= 0.0f) continue;
            const int ms = static_cast<int>(graphic.FrameDuration * 1000.0f + 0.5f);
            if (ms <= 0) continue;
            if (!first_timing) std::printf(", ");
            first_timing = false;
            std::printf("\"%zu\": %d", id, ms);
        }
    }

    // Each civ's building graphics: a civ's unit table names its own
    // architecture set.
    std::printf("},\n  \"civ_buildings\": {");
    {
        bool first_civ = true;
        for (size_t civ = 1; civ < dat.Civs.size(); ++civ) {
            if (!first_civ) std::printf(", ");
            first_civ = false;
            std::printf("\"%zu\": {", civ);
            bool first_unit = true;
            for (const genie::Unit& unit : dat.Civs[civ].Units) {
                if (unit.ID < 0 || unit.Name.empty()) continue;
                if (unit.Type != genie::Unit::BuildingType) continue;
                const int16_t graphic_id = unit.StandingGraphic.first;
                if (graphic_id < 0 ||
                    graphic_id >= static_cast<int16_t>(dat.Graphics.size())) {
                    continue;
                }
                if (!first_unit) std::printf(", ");
                first_unit = false;
                std::printf("\"%d\": %d", unit.ID, graphic_id);
            }
            std::printf("}");
        }
    }

    std::printf("},\n  \"civs\": [");
    for (size_t i = 1; i < dat.Civs.size(); ++i) {
        if (i > 1) std::printf(", ");
        PrintJsonString(dat.Civs[i].Name);
    }

    // Each civ's starting resource table: the stockpile it opens with, plus the
    // bookkeeping slots the engine runs on -- population headroom, the hard
    // population limit, and so on. Indexed by the dat's resource id, so slot 4
    // is headroom exactly as it is in a building's storage or a unit's cost.
    std::printf("],\n  \"civ_resources\": [");
    for (size_t i = 1; i < dat.Civs.size(); ++i) {
        if (i > 1) std::printf(", ");
        std::printf("[");
        const std::vector<float>& start = dat.Civs[i].Resources;
        for (size_t r = 0; r < start.size(); ++r) {
            if (r) std::printf(", ");
            std::printf("%g", start[r]);
        }
        std::printf("]");
    }

    std::printf("],\n  \"player_colours\": [");
    for (size_t i = 0; i < dat.PlayerColours.size(); ++i) {
        const genie::PlayerColour& colour = dat.PlayerColours[i];
        if (i) std::printf(", ");
        std::printf("{\"id\": %d, \"base\": %d, \"outline\": %d, "
                    "\"select1\": %d, \"select2\": %d, "
                    "\"minimap2\": %d, \"minimap3\": %d}",
                    colour.ID, colour.PlayerColorBase, colour.UnitOutlineColor,
                    colour.UnitSelectionColor1, colour.UnitSelectionColor2,
                    colour.MinimapColor2, colour.MinimapColor3);
    }
    std::printf("],\n  \"gaia_art\": {\n");

    // Map objects (Gaia's units) and the art each stands as.
    bool first_art = true;
    if (!dat.Civs.empty()) {
        const genie::Civ& gaia = dat.Civs[0];
        for (const genie::Unit& unit : gaia.Units) {
            if (unit.ID < 0) continue;
            const int16_t graphic_id = unit.StandingGraphic.first;
            if (graphic_id < 0 ||
                graphic_id >= static_cast<int16_t>(dat.Graphics.size())) {
                continue;
            }
            const genie::Graphic& graphic = dat.Graphics[graphic_id];
            if (graphic.SLP < 0) continue;

            if (!first_art) std::printf(",\n");
            first_art = false;
            const int frames = std::max<int>(1, graphic.FrameCount) *
                               std::max<int>(1, graphic.AngleCount);
            std::printf("    \"%d\": {\"name\": ", unit.ID);
            PrintJsonString(unit.Name);
            std::printf(", \"graphic\": %d, \"slp\": %d, \"frames\": %d",
                        graphic_id, graphic.SLP, frames);
            // What the interface calls it, and its icon.
            std::printf(", \"name_string\": %d", unit.LanguageDLLName);
            std::printf(", \"help_string\": %d", unit.LanguageDLLHelp);
            std::printf(", \"icon\": %d", unit.IconID);
            std::printf(", \"hp\": %d", unit.HitPoints);
            std::printf(", \"outline\": [%.3f, %.3f], \"hp_bar_height\": %.3f"
                        ", \"selection_effect\": %d, \"obstruction\": %d",
                        unit.OutlineSize.x, unit.OutlineSize.y, unit.HPBarHeight,
                        static_cast<int>(unit.SelectionEffect),
                        static_cast<int>(unit.ObstructionType));
            // Its fighting numbers, read the same way the playable units' are
            // (a boar fights back).
            {
                int pierce = 0;
                for (const genie::unit::AttackOrArmor& armour : unit.Combat.Armours) {
                    if (armour.Class == 3) pierce = armour.Amount;
                }
                std::printf(", \"attack\": %d", unit.Combat.DisplayedAttack);
                std::printf(", \"melee_armour\": %d", unit.Combat.DisplayedMeleeArmour);
                std::printf(", \"pierce_armour\": %d", pierce);
                std::printf(", \"line_of_sight\": %d",
                            static_cast<int>(unit.LineOfSight));
            }
            // Frames and angles are kept apart: a graphic with one frame per
            // angle is a set of shapes to choose from -- fourteen trees -- and
            // one with several is an animation the unit plays.
            std::printf(", \"frame_count\": %u, \"angle_count\": %u",
                        graphic.FrameCount, graphic.AngleCount);
            std::printf(", \"frame_duration\": %.4f", graphic.FrameDuration);
            std::printf(", \"minimap\": %u", unit.MinimapColor);
            std::printf(", \"unit_type\": %u",
                        static_cast<unsigned>(unit.Type));
            // What a map object is worth, which lives on Gaia only.
            std::printf(", \"storage\": [");
            bool first_storage = true;
            for (const genie::Unit::ResourceStorage& store : unit.ResourceStorages) {
                if (store.Type < 0 || store.Amount <= 0) continue;
                if (!first_storage) std::printf(", ");
                first_storage = false;
                std::printf("[%d, %d]", store.Type,
                            static_cast<int>(store.Amount));
            }
            std::printf("]");
            std::printf("}");
        }
    }

    std::printf("\n  }");
    DumpRules(dat);
    DumpMedia(dat);
    DumpTechTree(dat);
    DumpTables(dat);
    std::printf(",\n  \"terrain_count\": %zu",
                dat.TerrainBlock.Terrains.size());
    std::printf(",\n  \"graphic_count\": %zu", dat.Graphics.size());
    std::printf(",\n  \"sound_count\": %zu", dat.Sounds.size());
    std::printf(",\n  \"tech_count\": %zu", dat.Techs.size());
    std::printf(",\n  \"effect_count\": %zu", dat.Effects.size());
    std::printf(",\n  \"civ_count\": %zu", dat.Civs.size());
    std::printf("\n}\n");
    return 0;
}
