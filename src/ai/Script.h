#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "ai/Language.h"

// Reading computer player scripts.
//
// A script is a `.per` file of rules in the language CPSB.DOC documents:
// `(defrule facts => actions)`, `(defconst name value)`, `(load "file")`,
// `(load-random 20 "a" 10 "b" "c")`, and the conditional loading directives
// `#load-if-defined`, `#load-if-not-defined`, `#else`, `#end-if` over the
// symbols the game defines for each player (DIFFICULTY-HARD, BRITON-CIV,
// ARABIA-MAP, POPULATION-CAP-75 ...). Loading happens once, at the start of a
// game, per computer player: which rules are loaded depends on that player's
// civilisation and on the dice load-random throws.
//
// What comes out is flat data -- every rule's facts and actions compiled to
// numbers, nothing left to look up by name while the game runs:
//
//   - `exprs`: each rule's facts in prefix order. A logic node (and, or, not,
//     nand, nor, xor) is followed by its operands; `size` is how many nodes
//     its subtree takes, so an evaluator can skip one without walking it.
//   - `actions`: each rule's actions in order.
//   - `rules`: where each rule's facts and actions are, in load order -- the
//     order the rules are tried in.
//
// Load nests ten deep, conditional loading fifty; constants cannot be
// redefined with a different value; rules not loaded are not compiled (nor
// syntax-checked). Errors are collected with the language's codes ("ERR2003:
// Invalid keyword") without stopping the game, and a rule with an error is
// dropped.
namespace AI::Script {

struct Expr {
    // A logic node, or a fact.
    bool fact = false;
    uint8_t logic = 0;      // Lang::Logic
    uint8_t operands = 0;   // a logic node's direct operands
    uint16_t id = 0;        // Lang::Fact
    uint16_t size = 1;      // nodes in this subtree, itself included
    int32_t args[Lang::kMaxArgs] = {0, 0, 0, 0};
};

struct Act {
    uint16_t id = 0;        // Lang::Action
    int32_t args[Lang::kMaxArgs] = {0, 0, 0, 0};
};

struct Rule {
    uint32_t first_expr = 0;
    uint16_t expr_count = 0;     // top-level facts, all of which must hold
    uint32_t first_action = 0;
    uint16_t action_count = 0;
    uint16_t file = 0;           // index into Program::files
    uint32_t line = 0;
};

struct Program {
    std::vector<Expr> exprs;
    std::vector<Act> actions;
    std::vector<Rule> rules;
    std::vector<std::string> strings;   // <string> arguments, by index
    std::vector<std::string> files;     // every file loaded, in order
    std::vector<std::string> errors;    // "file:line: ERRnnnn: ..."
    std::vector<std::pair<std::string, int>> constants;
};

// What a script is loaded under.
struct Environment {
    // The system-defined symbols in effect for this player.
    std::vector<std::string> symbols;
    // The system-defined constants (my-player-number, my-civ ...).
    std::vector<std::pair<std::string, int>> constants;
    // A file's text by the name `load` gives it; false if there is none.
    std::function<bool(const std::string& name, std::string& text)> read;
    // A die for load-random: 0..99.
    std::function<int()> percent;
};

// Loads `file` and everything it loads.
Program Compile(const std::string& file, const Environment& env);

// The constant's value, if the program defined it.
bool Constant(const Program& program, const std::string& name, int& out);

} // namespace AI::Script
