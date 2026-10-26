#include "ai/Script.h"

#include <algorithm>
#include <charconv>
#include <deque>
#include <string_view>
#include <unordered_map>

namespace AI::Script {

namespace {

using namespace Lang;

// Nesting limits.
constexpr int kMaxLoadDepth = 10;
constexpr int kMaxConditionalDepth = 50;

enum class Tok : uint8_t { Open, Close, String, Symbol, Directive };

struct Token {
    Tok kind = Tok::Symbol;
    std::string_view text;   // a string's text without its quotes
    uint32_t line = 0;
};

// Delimiters: space, tab, carriage return, line feed, the parentheses, and
// the semicolon that starts a comment.
bool Delimiter(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '(' || c == ')' || c == ';';
}

void Lex(std::string_view src, std::vector<Token>& out) {
    uint32_t line = 1;
    size_t i = 0;
    while (i < src.size()) {
        const char c = src[i];
        if (c == '\n') {
            ++line;
            ++i;
        } else if (c == ' ' || c == '\t' || c == '\r' || c == '\f' || c == '\v') {
            ++i;
        } else if (c == ';') {
            while (i < src.size() && src[i] != '\n') ++i;
        } else if (c == '(') {
            out.push_back({Tok::Open, src.substr(i, 1), line});
            ++i;
        } else if (c == ')') {
            out.push_back({Tok::Close, src.substr(i, 1), line});
            ++i;
        } else if (c == '"') {
            const size_t start = ++i;
            const uint32_t at = line;
            while (i < src.size() && src[i] != '"') {
                if (src[i] == '\n') ++line;
                ++i;
            }
            out.push_back({Tok::String, src.substr(start, i - start), at});
            if (i < src.size()) ++i;
        } else {
            const size_t start = i;
            while (i < src.size() && !Delimiter(src[i])) ++i;
            const std::string_view word = src.substr(start, i - start);
            out.push_back({word.front() == '#' ? Tok::Directive : Tok::Symbol, word, line});
        }
    }
}

bool Integer(std::string_view text, int& out) {
    if (text.empty()) return false;
    const char* first = text.data();
    const char* last = text.data() + text.size();
    if (*first == '+') ++first;
    // A percentage sign after the number is ignored: the campaigns' scripts
    // write "(set-strategic-number sn-percent-attack-boats 100%)".
    if (last - first > 1 && *(last - 1) == '%') --last;
    const auto result = std::from_chars(first, last, out);
    return result.ec == std::errc() && result.ptr == last;
}

struct Compiler {
    const Environment& env;
    Program& program;
    std::unordered_map<std::string, int> constants;
    std::deque<std::string> sources;    // texts kept alive (and in place) while their tokens are used
    std::string file;                    // the file being read
    uint16_t file_index = 0;

    void Error(uint32_t line, std::string_view code, std::string_view what, std::string_view word = {}) {
        std::string msg = file + ":" + std::to_string(line) + ": " + std::string(code) + ": " +
                          std::string(what);
        if (!word.empty()) msg += ": " + std::string(word);
        program.errors.push_back(std::move(msg));
    }

    bool Defined(std::string_view symbol) const {
        return std::find(env.symbols.begin(), env.symbols.end(), symbol) != env.symbols.end();
    }

    bool ConstantValue(std::string_view name, int& out) const {
        const auto it = constants.find(std::string(name));
        if (it == constants.end()) return false;
        out = it->second;
        return true;
    }

    // One argument of `type`, from a token. False (with an error) if it is
    // not one.
    bool Argument(Param type, const Token& t, bool in_action, int32_t& out) {
        int v = 0;
        if (type == Param::String) {
            if (t.kind != Tok::String) {
                Error(t.line, "ERR2004", "Missing identifier", t.text);
                return false;
            }
            out = static_cast<int32_t>(program.strings.size());
            program.strings.emplace_back(t.text);
            return true;
        }
        if (t.kind != Tok::Symbol) {
            Error(t.line, "ERR2004", "Missing identifier", t.text);
            return false;
        }
        const std::string_view w = t.text;
        auto value = [&]() {
            if (Integer(w, v) || ConstantValue(w, v)) {
                out = v;
                return true;
            }
            return false;
        };
        switch (type) {
        case Param::Value:
            if (value()) return true;
            break;
        case Param::RelOp: {
            RelOp op{};
            if (FindRelOp(w, op)) {
                out = static_cast<int32_t>(op);
                return true;
            }
            break;
        }
        case Param::Player: {
            Who who{};
            if (FindWildcard(w, who)) {
                out = kWildcardBase + static_cast<int32_t>(who);
                return true;
            }
            if (in_action && FindVariable(w, who)) {
                out = kVariableBase + static_cast<int32_t>(who);
                return true;
            }
            if (value()) return true;
            break;
        }
        case Param::Unit:
        case Param::Building:
        case Param::Research: {
            const int thing = FindThing(w);
            if (thing >= 0) {
                const ThingKind kind = Things()[thing].kind;
                const bool fits =
                    type == Param::Unit
                        ? (kind == ThingKind::Unit || kind == ThingKind::UniqueUnit ||
                           kind == ThingKind::EliteUniqueUnit || kind == ThingKind::UniqueUnitLine)
                    : type == Param::Building
                        ? kind == ThingKind::Building
                        : (kind == ThingKind::Research || kind == ThingKind::Age ||
                           kind == ThingKind::UniqueUnitUpgrade);
                if (fits) {
                    out = thing;
                    return true;
                }
            }
            break;
        }
        case Param::Age:
            if (FindAge(w, v) || value()) {
                out = v;
                return true;
            }
            break;
        case Param::Civ:
            if (FindCiv(w, v)) { out = v; return true; }
            break;
        case Param::Difficulty:
            if (FindDifficulty(w, v)) { out = v; return true; }
            break;
        case Param::MapType:
            if (FindMapType(w, v)) { out = v; return true; }
            break;
        case Param::MapSize:
            if (FindMapSize(w, v)) { out = v; return true; }
            break;
        case Param::Resource:
            if (FindResource(w, v)) { out = v; return true; }
            break;
        case Param::Commodity:
            if (w != "gold" && FindResource(w, v)) { out = v; return true; }
            break;
        case Param::Stance:
            if (FindStance(w, v)) { out = v; return true; }
            break;
        case Param::StartingResources:
            if (FindStartingResources(w, v)) { out = v; return true; }
            break;
        case Param::StrategicNumber: {
            SN sn{};
            if (FindStrategicNumber(w, sn)) {
                out = static_cast<int32_t>(sn);
                return true;
            }
            break;
        }
        case Param::WallType:
            if (FindWallType(w, v)) { out = v; return true; }
            break;
        case Param::DifficultyParameter:
            if (FindDifficultyParameter(w, v)) { out = v; return true; }
            break;
        case Param::VictoryCondition:
            if (FindVictoryCondition(w, v)) { out = v; return true; }
            break;
        case Param::EventType:
            if (FindEventType(w, v)) { out = v; return true; }
            break;
        case Param::String:
        case Param::None:
            break;
        }
        // A word of another of the enumerated kinds, where one kind is
        // expected, is taken by its value: "petersen dip feeder" tributes
        // "this-any-human ally 100" -- a stance where the resource goes --
        // and ally's value, 0, is food.
        if (Enumerated(type) && OtherWord(w, v)) {
            out = v;
            return true;
        }
        Error(t.line, "ERR2005", "Invalid identifier", w);
        return false;
    }

    static bool Enumerated(Param type) {
        switch (type) {
        case Param::Resource:
        case Param::Commodity:
        case Param::Stance:
        case Param::StartingResources:
        case Param::DifficultyParameter:
        case Param::VictoryCondition:
            return true;
        default:
            return false;
        }
    }

    static bool OtherWord(std::string_view w, int& v) {
        return FindStance(w, v) || FindResource(w, v) || FindStartingResources(w, v) ||
               FindDifficultyParameter(w, v) || FindVictoryCondition(w, v);
    }

    // A form's arguments, by `sig`, up to its closing parenthesis.
    bool Arguments(const std::vector<Token>& toks, size_t& i, const Signature& sig, bool in_action,
                   int32_t* args) {
        for (int a = 0; a < sig.count; ++a) {
            if (i >= toks.size() || toks[i].kind == Tok::Close || toks[i].kind == Tok::Open) {
                Error(i < toks.size() ? toks[i].line : 0, "ERR2004", "Missing identifier", sig.name);
                return false;
            }
            if (!Argument(sig.params[a], toks[i], in_action, args[a])) return false;
            ++i;
        }
        if (i >= toks.size() || toks[i].kind != Tok::Close) {
            Error(i < toks.size() ? toks[i].line : 0, "ERR2011", "Missing closing parenthesis", sig.name);
            return false;
        }
        ++i;
        return true;
    }

    // Skips a form whose opening parenthesis is at toks[i], to just past its
    // closing one.
    static void Skip(const std::vector<Token>& toks, size_t& i) {
        int depth = 0;
        do {
            if (toks[i].kind == Tok::Open) ++depth;
            else if (toks[i].kind == Tok::Close) --depth;
            ++i;
        } while (i < toks.size() && depth > 0);
    }

    // One fact, or a logic node and its operands, into `out`.
    bool Fact(const std::vector<Token>& toks, size_t& i, std::vector<Expr>& out) {
        if (i >= toks.size() || toks[i].kind != Tok::Open) {
            Error(i < toks.size() ? toks[i].line : 0, "ERR2001", "Missing opening parenthesis");
            return false;
        }
        ++i;
        if (i >= toks.size() || toks[i].kind != Tok::Symbol) {
            Error(i < toks.size() ? toks[i].line : 0, "ERR2002", "Missing keyword");
            return false;
        }
        const Token& head = toks[i++];
        Logic logic{};
        if (FindLogic(head.text, logic)) {
            const size_t at = out.size();
            out.push_back({});
            int operands = 0;
            while (i < toks.size() && toks[i].kind == Tok::Open) {
                if (!Fact(toks, i, out)) return false;
                ++operands;
            }
            if (i >= toks.size() || toks[i].kind != Tok::Close) {
                Error(head.line, "ERR2011", "Missing closing parenthesis", head.text);
                return false;
            }
            ++i;
            if (logic == Logic::Not ? operands != 1 : operands < 2) {
                Error(head.line, "ERR2004", "Missing identifier", head.text);
                return false;
            }
            Expr& node = out[at];
            node.fact = false;
            node.logic = static_cast<uint8_t>(logic);
            node.operands = static_cast<uint8_t>(operands);
            node.size = static_cast<uint16_t>(out.size() - at);
            return true;
        }
        Lang::Fact fact{};
        if (!FindFact(head.text, fact)) {
            Error(head.line, "ERR2003", "Invalid keyword", head.text);
            return false;
        }
        Expr node;
        node.fact = true;
        node.id = static_cast<uint16_t>(fact);
        if (!Arguments(toks, i, Describe(fact), false, node.args)) return false;
        out.push_back(node);
        return true;
    }

    bool ActionForm(const std::vector<Token>& toks, size_t& i, std::vector<Act>& out) {
        if (i >= toks.size() || toks[i].kind != Tok::Open) {
            Error(i < toks.size() ? toks[i].line : 0, "ERR2001", "Missing opening parenthesis");
            return false;
        }
        ++i;
        if (i >= toks.size() || toks[i].kind != Tok::Symbol) {
            Error(i < toks.size() ? toks[i].line : 0, "ERR2002", "Missing keyword");
            return false;
        }
        const Token& head = toks[i++];
        Lang::Action action{};
        if (!FindAction(head.text, action)) {
            Error(head.line, "ERR2003", "Invalid keyword", head.text);
            return false;
        }
        Act act;
        act.id = static_cast<uint16_t>(action);
        if (!Arguments(toks, i, Describe(action), true, act.args)) return false;
        out.push_back(act);
        return true;
    }

    // (defrule facts => actions), its opening parenthesis and keyword read.
    void Defrule(const std::vector<Token>& toks, size_t& i, size_t open, uint32_t line) {
        std::vector<Expr> facts;
        std::vector<Act> acts;
        uint16_t top = 0;
        bool ok = true;
        while (ok && i < toks.size() && toks[i].kind == Tok::Open) {
            ok = Fact(toks, i, facts);
            ++top;
        }
        if (ok && top == 0) {
            Error(line, "ERR2007", "Missing left-hand side (LHS) of the rule");
            ok = false;
        }
        if (ok && (i >= toks.size() || toks[i].kind != Tok::Symbol || toks[i].text != "=>")) {
            Error(line, "ERR2008", "Missing arrow");
            ok = false;
        }
        if (ok) ++i;
        while (ok && i < toks.size() && toks[i].kind == Tok::Open) ok = ActionForm(toks, i, acts);
        if (ok && acts.empty()) {
            Error(line, "ERR2009", "Missing right-hand side (RHS) of the rule");
            ok = false;
        }
        if (ok && (i >= toks.size() || toks[i].kind != Tok::Close)) {
            Error(line, "ERR2011", "Missing closing parenthesis", "defrule");
            ok = false;
        }
        if (!ok) {
            // Drop the rule: skip to the end of the whole form.
            i = open;
            Skip(toks, i);
            return;
        }
        ++i;
        Rule rule;
        rule.first_expr = static_cast<uint32_t>(program.exprs.size());
        rule.expr_count = top;
        rule.first_action = static_cast<uint32_t>(program.actions.size());
        rule.action_count = static_cast<uint16_t>(acts.size());
        rule.file = file_index;
        rule.line = line;
        program.exprs.insert(program.exprs.end(), facts.begin(), facts.end());
        program.actions.insert(program.actions.end(), acts.begin(), acts.end());
        program.rules.push_back(rule);
    }

    void Defconst(const std::vector<Token>& toks, size_t& i, size_t open, uint32_t line) {
        int value = 0;
        if (i + 2 >= toks.size() || toks[i].kind != Tok::Symbol || toks[i + 1].kind != Tok::Symbol ||
            toks[i + 2].kind != Tok::Close ||
            !(Integer(toks[i + 1].text, value) || ConstantValue(toks[i + 1].text, value))) {
            Error(line, "ERR2004", "Missing identifier", "defconst");
            i = open;
            Skip(toks, i);
            return;
        }
        const std::string name(toks[i].text);
        int existing = 0;
        if (ConstantValue(name, existing)) {
            if (existing != value) Error(line, "ERR2012", "Constant Already Defined", name);
        } else {
            constants.emplace(name, value);
            program.constants.emplace_back(name, value);
        }
        i += 3;
    }

    void Load(const std::string& name, uint32_t line, int depth) {
        if (depth >= kMaxLoadDepth) {
            Error(line, "ERR9001", "Unexpected error", "load nested more than ten deep");
            return;
        }
        std::string text;
        if (!env.read || !env.read(name, text)) {
            Error(line, "ERR5001", "File open failed", name);
            return;
        }
        const std::string outer = file;
        const uint16_t outer_index = file_index;
        File(name, std::move(text), depth + 1);
        file = outer;
        file_index = outer_index;
    }

    void LoadRandom(const std::vector<Token>& toks, size_t& i, size_t open, uint32_t line, int depth) {
        // Pairs of percent and file, then perhaps a default file alone.
        std::vector<std::pair<int, std::string>> choices;
        std::string fallback;
        bool ok = true;
        while (ok && i < toks.size() && toks[i].kind != Tok::Close) {
            int chance = 0;
            if (toks[i].kind == Tok::String) {
                fallback = std::string(toks[i].text);
                ++i;
                break;
            }
            if (toks[i].kind != Tok::Symbol || !Integer(toks[i].text, chance) || i + 1 >= toks.size() ||
                toks[i + 1].kind != Tok::String) {
                ok = false;
                break;
            }
            choices.emplace_back(chance, std::string(toks[i + 1].text));
            i += 2;
        }
        if (!ok || i >= toks.size() || toks[i].kind != Tok::Close) {
            Error(line, "ERR2006", "Missing file name", "load-random");
            i = open;
            Skip(toks, i);
            return;
        }
        ++i;
        // One roll for all the files; a load-random loads at most one.
        const int roll = env.percent ? env.percent() : 0;
        int sum = 0;
        for (const auto& [chance, name] : choices) {
            sum += chance;
            if (roll < sum) {
                Load(name, line, depth);
                return;
            }
        }
        if (!fallback.empty()) Load(fallback, line, depth);
    }

    void File(const std::string& name, std::string text, int depth) {
        sources.push_back(std::move(text));
        const std::string_view src = sources.back();
        file = name;
        file_index = static_cast<uint16_t>(program.files.size());
        program.files.push_back(name);
        std::vector<Token> toks;
        Lex(src, toks);

        struct Branch {
            bool outer = true;
            bool taken = true;
            bool seen_else = false;
        };
        std::vector<Branch> branches;
        auto active = [&]() { return branches.empty() || (branches.back().outer && branches.back().taken); };

        size_t i = 0;
        while (i < toks.size()) {
            const Token& t = toks[i];
            if (t.kind == Tok::Directive) {
                if (t.text == "#load-if-defined" || t.text == "#load-if-not-defined") {
                    if (i + 1 >= toks.size() || toks[i + 1].kind != Tok::Symbol) {
                        Error(t.line, "ERR3002", "Missing preprocessor symbol");
                        ++i;
                        continue;
                    }
                    if (branches.size() >= kMaxConditionalDepth) {
                        Error(t.line, "ERR3003", "Preprocessor nesting too deep");
                    }
                    const bool defined = Defined(toks[i + 1].text);
                    branches.push_back({active(), t.text == "#load-if-defined" ? defined : !defined, false});
                    i += 2;
                } else if (t.text == "#else") {
                    if (branches.empty() || branches.back().seen_else) {
                        Error(t.line, "ERR3004", "Unexpected #else");
                    } else {
                        branches.back().taken = !branches.back().taken;
                        branches.back().seen_else = true;
                    }
                    ++i;
                } else if (t.text == "#end-if") {
                    if (branches.empty()) Error(t.line, "ERR3005", "Unexpected #end-if");
                    else branches.pop_back();
                    ++i;
                } else {
                    Error(t.line, "ERR3001", "Invalid preprocessor directive", t.text);
                    ++i;
                }
                continue;
            }
            if (t.kind != Tok::Open) {
                Error(t.line, "ERR2001", "Missing opening parenthesis", t.text);
                ++i;
                continue;
            }
            const size_t open = i;
            if (!active()) {
                Skip(toks, i);
                continue;
            }
            ++i;
            if (i >= toks.size() || toks[i].kind != Tok::Symbol) {
                Error(t.line, "ERR2002", "Missing keyword");
                i = open;
                Skip(toks, i);
                continue;
            }
            const std::string_view head = toks[i].text;
            const uint32_t line = toks[i].line;
            ++i;
            if (head == "defrule") {
                Defrule(toks, i, open, line);
            } else if (head == "defconst") {
                Defconst(toks, i, open, line);
            } else if (head == "load") {
                if (i + 1 >= toks.size() || toks[i].kind != Tok::String || toks[i + 1].kind != Tok::Close) {
                    Error(line, "ERR2006", "Missing file name", "load");
                    i = open;
                    Skip(toks, i);
                    continue;
                }
                const std::string target(toks[i].text);
                i += 2;
                Load(target, line, depth);
            } else if (head == "load-random") {
                LoadRandom(toks, i, open, line, depth);
            } else {
                Error(line, "ERR2003", "Invalid keyword", head);
                i = open;
                Skip(toks, i);
            }
        }
        if (!branches.empty()) Error(toks.empty() ? 0 : toks.back().line, "ERR3006", "Missing #end-if");
    }
};

} // namespace

Program Compile(const std::string& file, const Environment& env) {
    Program program;
    Compiler compiler{env, program, {}, {}, {}, 0};
    for (const auto& [name, value] : env.constants) {
        compiler.constants.emplace(name, value);
        program.constants.emplace_back(name, value);
    }
    compiler.Load(file, 0, -1);
    if (program.rules.empty()) program.errors.push_back(file + ": ERR8001: No rules");
    return program;
}

bool Constant(const Program& program, const std::string& name, int& out) {
    for (const auto& [n, v] : program.constants) {
        if (n == name) {
            out = v;
            return true;
        }
    }
    return false;
}

} // namespace AI::Script
