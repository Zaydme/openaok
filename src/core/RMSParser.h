#pragma once
#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Core {

// One statement of a random map script: a command name and its arguments,
// already resolved to numbers where the script wrote a `#const` name. A
// statement is a line: its first word the command, the rest its arguments.
struct RMSCommand {
    std::string name;
    std::vector<std::string> args;
    // Attributes from the `{ ... }` block that follows a create_* command.
    std::vector<RMSCommand> attributes;

    // An argument as a number. Names were resolved against the script's
    // constants during parsing, so `land_percent 25` and `base_terrain GRASS3`
    // both arrive here as integers.
    int Value(size_t index = 0, int fallback = 0) const {
        if (index >= args.size()) return fallback;
        const std::string& text = args[index];
        if (text.empty()) return fallback;
        char* end = nullptr;
        const long parsed = std::strtol(text.c_str(), &end, 10);
        return (end && *end == '\0') ? static_cast<int>(parsed) : fallback;
    }

    // An attribute's value, or `fallback` when the block does not set it.
    int Attribute(const std::string& attribute_name, int fallback = 0) const {
        for (const RMSCommand& attribute : attributes) {
            if (attribute.name == attribute_name) {
                return attribute.args.empty() ? 1 : attribute.Value(0, fallback);
            }
        }
        return fallback;
    }

    bool HasAttribute(const std::string& attribute_name) const {
        for (const RMSCommand& attribute : attributes) {
            if (attribute.name == attribute_name) return true;
        }
        return false;
    }
};

struct RMSSection {
    std::string name;
    std::vector<RMSCommand> commands;
};

struct RMSScript {
    std::string name;
    std::vector<RMSSection> sections;

    const RMSSection* GetSection(const std::string& section_name) const {
        for (const RMSSection& section : sections) {
            if (section.name == section_name) return &section;
        }
        return nullptr;
    }
};

// Parses random map scripts.
//
// The scripts live in gamedata.drs as plain text, so the parser works on
// content rather than on a path: `resolve_include` pulls in whatever a script
// `#include_drs`es (every script includes random_map.def's constants).
class RMSParser {
public:
    // Returns the text of an included archive entry, or false when absent.
    using Include = bool (*)(uint32_t file_id, std::string& out);

    // `seed` drives the `start_random` blocks, so the same seed always chooses
    // the same variant of a map.
    // `defines` are the labels the game sets before the script runs -- the
    // map's size (TINY_MAP ... GIGANTIC_MAP), which scripts test with `if`.
    static RMSScript Parse(const std::string& content, uint64_t seed,
                           Include resolve_include = nullptr,
                           const std::string& name = std::string(),
                           const std::vector<std::string>& defines = {});

private:
    struct Preprocessor {
        std::unordered_map<std::string, int> constants;
        std::unordered_set<std::string> defines;
        uint64_t seed = 0;
        Include resolve_include = nullptr;
        int include_depth = 0;
    };

    static void Expand(const std::string& content, Preprocessor& pre,
                       std::vector<std::vector<std::string>>& out_lines);
};

} // namespace Core
