#include "ai/Interpreter.h"

#include "ai/Actions.h"
#include "ai/Facts.h"

namespace AI {

namespace {

// The value of the fact or logic node at `at`.
bool Evaluate(Turn& turn, uint32_t at) {
    const Script::Expr& node = turn.program->exprs[at];
    if (node.fact) return Facts::Test(turn, node);
    const auto logic = static_cast<Lang::Logic>(node.logic);
    uint32_t child = at + 1;
    auto next = [&]() {
        const bool v = Evaluate(turn, child);
        child += turn.program->exprs[child].size;
        return v;
    };
    switch (logic) {
    case Lang::Logic::Not:
        return !next();
    case Lang::Logic::And:
    case Lang::Logic::Nand: {
        bool all = true;
        for (int i = 0; i < node.operands; ++i) {
            if (!next()) {
                all = false;
                break;
            }
        }
        return logic == Lang::Logic::And ? all : !all;
    }
    case Lang::Logic::Or:
    case Lang::Logic::Nor: {
        bool any = false;
        for (int i = 0; i < node.operands; ++i) {
            if (next()) {
                any = true;
                break;
            }
        }
        return logic == Lang::Logic::Or ? any : !any;
    }
    case Lang::Logic::Xor: {
        int count = 0;
        for (int i = 0; i < node.operands; ++i) count += next() ? 1 : 0;
        return count == 1;
    }
    }
    return false;
}

void ResetVariables(Turn& turn) {
    for (int& v : turn.vars) v = -1;
}

} // namespace

bool Holds(Turn& turn, size_t rule) {
    const Script::Rule& r = turn.program->rules[rule];
    ResetVariables(turn);
    uint32_t at = r.first_expr;
    for (int f = 0; f < r.expr_count; ++f) {
        if (!Evaluate(turn, at)) return false;
        at += turn.program->exprs[at].size;
    }
    return true;
}

void Pass(Turn& turn) {
    const Script::Program& program = *turn.program;
    Mind& mind = *turn.mind;
    for (size_t r = 0; r < program.rules.size(); ++r) {
        if (!mind.enabled[r]) continue;
        if (!Holds(turn, r)) continue;
        const Script::Rule& rule = program.rules[r];
        bool disable = false;
        for (uint32_t a = 0; a < rule.action_count; ++a) {
            Actions::Run(turn, program.actions[rule.first_action + a], disable);
        }
        if (disable) mind.enabled[r] = 0;
        ++turn.fired;
    }
}

} // namespace AI
