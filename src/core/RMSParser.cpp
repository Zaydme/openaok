#include "RMSParser.h"

#include "Random.h"

#include <cctype>
#include <cstdlib>
#include <iostream>

namespace Core {

namespace {

// Strips `/* ... */` comments (the scripts' only comment form) and splits what
// is left into lines of whitespace-separated words. A brace is a line of its
// own wherever it appears.
std::vector<std::vector<std::string>> SplitLines(const std::string& content) {
    std::vector<std::vector<std::string>> lines;
    std::vector<std::string> current;
    std::string word;
    bool in_comment = false;

    auto flush_word = [&]() {
        if (!word.empty()) {
            current.push_back(word);
            word.clear();
        }
    };
    auto flush_line = [&]() {
        flush_word();
        if (!current.empty()) {
            lines.push_back(current);
            current.clear();
        }
    };

    for (size_t i = 0; i < content.size(); ++i) {
        const char c = content[i];
        if (in_comment) {
            if (c == '*' && i + 1 < content.size() && content[i + 1] == '/') {
                in_comment = false;
                ++i;
            }
            continue;
        }
        if (c == '/' && i + 1 < content.size() && content[i + 1] == '*') {
            in_comment = true;
            ++i;
            continue;
        }
        if (c == '\n' || c == '\r') {
            flush_line();
        } else if (c == '{' || c == '}') {
            flush_word();
            current.push_back(std::string(1, c));
            flush_line();
        } else if (std::isspace(static_cast<unsigned char>(c))) {
            flush_word();
        } else {
            word += c;
        }
    }
    flush_line();
    return lines;
}

bool IsInteger(const std::string& text) {
    if (text.empty()) return false;
    size_t i = (text[0] == '-' || text[0] == '+') ? 1 : 0;
    if (i >= text.size()) return false;
    for (; i < text.size(); ++i) {
        if (!std::isdigit(static_cast<unsigned char>(text[i]))) return false;
    }
    return true;
}

} // namespace

// Walks the raw text once, resolving the preprocessor as it goes and emitting
// only the lines that survive it.
//
// Conditionals come in two kinds. `if NAME / elseif / else / endif` tests
// whether a `#define` is set, and `start_random / percent_chance N /
// end_random` picks exactly one branch by weight.
void RMSParser::Expand(const std::string& content, Preprocessor& pre,
                       std::vector<std::vector<std::string>>& out_lines) {
    const std::vector<std::vector<std::string>> lines = SplitLines(content);

    // One entry per open `if`/`start_random`: whether its branch is being kept,
    // and whether any earlier branch already was.
    struct Frame {
        bool emitting = false;
        bool taken = false;
        bool random = false;
        int roll = 0;        // random blocks only: the weight rolled for
        int cumulative = 0;  // weights of the branches seen so far
    };
    std::vector<Frame> stack;
    auto emitting = [&]() {
        for (const Frame& frame : stack) {
            if (!frame.emitting) return false;
        }
        return true;
    };

    for (const std::vector<std::string>& line : lines) {
        const std::string& head = line[0];

        if (head == "#const") {
            if (emitting() && line.size() >= 3 && IsInteger(line[2])) {
                pre.constants[line[1]] = std::atoi(line[2].c_str());
            }
            continue;
        }
        if (head == "#define") {
            if (emitting() && line.size() >= 2) pre.defines.insert(line[1]);
            continue;
        }
        if (head == "#include_drs") {
            // e.g. `#include_drs random_map.def 54000`. The id is what matters;
            // the filename beside it is commentary.
            if (emitting() && pre.resolve_include && pre.include_depth < 4) {
                for (size_t i = 1; i < line.size(); ++i) {
                    if (!IsInteger(line[i])) continue;
                    std::string included;
                    if (pre.resolve_include(static_cast<uint32_t>(std::atoi(line[i].c_str())),
                                            included)) {
                        ++pre.include_depth;
                        Expand(included, pre, out_lines);
                        --pre.include_depth;
                    }
                    break;
                }
            }
            continue;
        }

        if (head == "if" || head == "start_random") {
            Frame frame;
            frame.random = (head == "start_random");
            if (frame.random) {
                // One roll for the whole block, so exactly one branch is taken.
                Random rng(pre.seed ^ (0x9E3779B97F4A7C15ULL * (stack.size() + 1)));
                frame.roll = static_cast<int>(rng.NextInt(100));
                frame.emitting = false;   // nothing until the first branch
            } else {
                frame.emitting = line.size() >= 2 &&
                                 pre.defines.count(line[1]) != 0;
                frame.taken = frame.emitting;
            }
            stack.push_back(frame);
            continue;
        }
        if (head == "percent_chance") {
            if (!stack.empty() && stack.back().random) {
                Frame& frame = stack.back();
                const int weight = line.size() >= 2 ? std::atoi(line[1].c_str()) : 0;
                const int low = frame.cumulative;
                frame.cumulative += weight;
                frame.emitting = !frame.taken &&
                                 frame.roll >= low && frame.roll < frame.cumulative;
                if (frame.emitting) frame.taken = true;
            }
            continue;
        }
        if (head == "elseif") {
            if (!stack.empty()) {
                Frame& frame = stack.back();
                const bool matches = line.size() >= 2 &&
                                     pre.defines.count(line[1]) != 0;
                frame.emitting = !frame.taken && matches;
                if (frame.emitting) frame.taken = true;
            }
            continue;
        }
        if (head == "else") {
            if (!stack.empty()) {
                Frame& frame = stack.back();
                frame.emitting = !frame.taken;
                frame.taken = true;
            }
            continue;
        }
        if (head == "endif" || head == "end_random") {
            if (!stack.empty()) stack.pop_back();
            continue;
        }

        if (!emitting()) continue;

        // Resolve constant names to their values.
        std::vector<std::string> resolved = line;
        for (size_t i = 1; i < resolved.size(); ++i) {
            auto found = pre.constants.find(resolved[i]);
            if (found != pre.constants.end()) {
                resolved[i] = std::to_string(found->second);
            }
        }
        out_lines.push_back(std::move(resolved));
    }
}

RMSScript RMSParser::Parse(const std::string& content, uint64_t seed,
                           Include resolve_include, const std::string& name,
                           const std::vector<std::string>& defines) {
    Preprocessor pre;
    pre.seed = seed;
    pre.resolve_include = resolve_include;
    for (const std::string& label : defines) pre.defines.insert(label);

    std::vector<std::vector<std::string>> lines;
    Expand(content, pre, lines);

    RMSScript script;
    script.name = name;
    RMSSection section;

    for (size_t i = 0; i < lines.size(); ++i) {
        const std::vector<std::string>& line = lines[i];
        const std::string& head = line[0];

        if (head.size() >= 2 && head.front() == '<' && head.back() == '>') {
            if (!section.name.empty()) script.sections.push_back(std::move(section));
            section = RMSSection();
            section.name = head.substr(1, head.size() - 2);
            continue;
        }
        if (head == "{" || head == "}") continue;   // stray brace

        RMSCommand command;
        command.name = head;
        command.args.assign(line.begin() + 1, line.end());

        // An attribute block, if one follows.
        if (i + 1 < lines.size() && lines[i + 1][0] == "{") {
            i += 2;   // step past the command's line and the opening brace
            for (; i < lines.size() && lines[i][0] != "}"; ++i) {
                RMSCommand attribute;
                attribute.name = lines[i][0];
                attribute.args.assign(lines[i].begin() + 1, lines[i].end());
                command.attributes.push_back(std::move(attribute));
            }
        }

        section.commands.push_back(std::move(command));
    }
    if (!section.name.empty()) script.sections.push_back(std::move(section));

    return script;
}

} // namespace Core
