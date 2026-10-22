// =============================================================================
// src/codegen/src/emit/control.cpp - codegen control-flow lowering (STYLE.md 8).
//
// Lowering for while/do-while, classic for, for-of, switch, labeled statements
// and the break/continue statements. Every loop and switch carries generated
// end / continue labels, so a `break`/`continue` lowers to a `goto` that can
// reach the frame a JS label selects (a C++ `break` cannot). A plain switch
// becomes a test chain plus labels so fall-through and a `default` in any
// position keep JS semantics; a C++ switch cannot compare std::string and would
// need the case bodies ordered to match its own dispatch. A same-enum switch
// keeps its native lowering (emit/enum.cpp) behind the same frame.
// =============================================================================
#include "detail/generator.h"

#include <cstddef>
#include <string>
#include <vector>

#include "detail/cpp_writer.h"
#include "detail/mangle.h"
#include "detail/type_map.h"

namespace cp3 {

using namespace detail;

std::string Generator::freshLabel(const char* tag) {
    return "cp3_" + std::string(tag) + std::to_string(uniqueCounter_++);
}

void Generator::pushFlowFrame(bool isLoop, const std::string& breakLabel,
                              const std::string& continueLabel,
                              const std::string& jsLabel) {
    flowStack_.push_back(FlowFrame{isLoop, breakLabel, continueLabel});
    if (!jsLabel.empty()) {
        labelStack_.push_back(
            LabelFrame{jsLabel, isLoop, breakLabel, continueLabel});
    }
}

void Generator::popFlowFrame() {
    if (!flowStack_.empty()) flowStack_.pop_back();
}

// -----------------------------------------------------------------------------
// while / do-while / for
// -----------------------------------------------------------------------------
std::string Generator::emitWhile(const WhileStatement& w,
                                 const std::string& jsLabel, std::string& out,
                                 int indent) {
    // A counted push loop reserves its remaining pushes once (emit/push_reserve.cpp).
    emitPushReserveHint(w, out, indent);
    const std::string breakLabel = freshLabel("while_end_");
    const std::string continueLabel = freshLabel("while_next_");
    pushFlowFrame(true, breakLabel, continueLabel, jsLabel);
    // A proven entry-body counter: the increment is the body's last statement
    // (skipped by emitBranch), so the `for` header performs it. The proof
    // rejected a `continue` in this loop, so the header step is never skipped.
    const auto counter = int64Loops_.find(w.nodeId);
    if (counter != int64Loops_.end()) {
        const TopInt64Binding& b = *counter->second;
        const std::string op = b.inclusive ? " <= " : " < ";
        line(out, indent, "for (; " + b.alias + op + std::to_string(b.bound) +
                              "; " + b.alias + " += " +
                              std::to_string(b.step) + ") {");
        if (w.body) {
            line(out, indent + 1, "{");
            emitBranch(*w.body, out, indent + 2);
            line(out, indent + 1, "}");
        }
        line(out, indent + 1, continueLabel + ": ;");
        line(out, indent, "}");
        popFlowFrame();
        line(out, indent, breakLabel + ": ;");
        return continueLabel;
    }
    const std::string condition = w.condition ? cond(*w.condition) : "false";
    line(out, indent, "while (" + condition + ") {");
    if (w.body) {
        // The body is one scope deeper than the continue label: a `goto` to
        // that label must not jump over an initialized declaration in the
        // loop body's own block.
        line(out, indent + 1, "{");
        emitBranch(*w.body, out, indent + 2);
        line(out, indent + 1, "}");
    }
    line(out, indent + 1, continueLabel + ": ;");
    line(out, indent, "}");
    popFlowFrame();
    line(out, indent, breakLabel + ": ;");
    return continueLabel;
}

std::string Generator::emitDoWhile(const DoWhileStatement& d,
                                   const std::string& jsLabel, std::string& out,
                                   int indent) {
    const std::string breakLabel = freshLabel("do_end_");
    const std::string continueLabel = freshLabel("do_next_");
    pushFlowFrame(true, breakLabel, continueLabel, jsLabel);
    line(out, indent, "do {");
    if (d.body) {
        line(out, indent + 1, "{");
        emitBranch(*d.body, out, indent + 2);
        line(out, indent + 1, "}");
    }
    line(out, indent + 1, continueLabel + ": ;");
    const std::string condition = d.condition ? cond(*d.condition) : "false";
    line(out, indent, "} while (" + condition + ");");
    popFlowFrame();
    line(out, indent, breakLabel + ": ;");
    return continueLabel;
}

std::string Generator::emitFor(const ForStatement& f,
                               const std::string& jsLabel, std::string& out,
                               int indent) {
    const std::string breakLabel = freshLabel("for_end_");
    const std::string continueLabel = freshLabel("for_next_");
    pushFlowFrame(true, breakLabel, continueLabel, jsLabel);
    pushScope();
    std::string init;
    if (f.init) {
        if (f.init->kind == NodeKind::VariableDeclaration) {
            const auto& vd = static_cast<const VariableDeclaration&>(*f.init);
            init = forInitText(vd, out, indent);
        } else if (auto exp = dynamic_cast<const Expr*>(f.init.get())) {
            init = expr(*exp);
        }
    }
    const std::string condition = f.condition ? cond(*f.condition) : "";
    const std::string update = f.update ? expr(*f.update) : "";
    line(out, indent, "for (" + init + "; " + condition + "; " + update + ") {");
    if (f.body) {
        line(out, indent + 1, "{");
        emitBranch(*f.body, out, indent + 2);
        line(out, indent + 1, "}");
    }
    line(out, indent + 1, continueLabel + ": ;");
    line(out, indent, "}");
    popScope();
    popFlowFrame();
    line(out, indent, breakLabel + ": ;");
    return continueLabel;
}

// -----------------------------------------------------------------------------
// for-of / for-in
// -----------------------------------------------------------------------------
// The array lowering lives in its family unit (src/std/arrays/codegen_methods.cpp):
// it owns the element-type gate and the lifetime-safe source handle.
void Generator::emitForOf(const ForOfStatement& f, const std::string& jsLabel,
                          std::string& out, int indent) {
    ArrayEmitter::forOf(*this, f, jsLabel, out, indent);
}

void Generator::emitForIn(const ForInStatement& f, const std::string&,
                          std::string& out, int indent) {
    // `for...in` stays an open question (its index type and target set are
    // unspecified); the marker stays in _ambiguous.
    notImplemented(f, "for-in");
    line(out, indent, "{");
    line(out, indent + 1, "/* CP3002: for-in */");
    line(out, indent, "}");
}

// -----------------------------------------------------------------------------
// switch
// -----------------------------------------------------------------------------
namespace {

// The eq text of `disc == test` for the switch test chain. The caller has
// already checked that both sides lower to the same representable C++ type.
std::string switchCompare(const std::string& disc, const std::string& test) {
    return "(" + disc + " == " + test + ")";
}

}  // namespace

std::string Generator::emitSwitch(const SwitchStatement& s,
                                  const std::string& jsLabel, std::string& out,
                                  int indent) {
    if (intOn()) intFail();
    const std::string breakLabel = freshLabel("switch_end_");

    // A same-enum switch keeps the native C++ lowering; the frame still makes
    // `break`/`continue` obey the JS labels.
    if (s.discriminant &&
        effectiveType(*s.discriminant).kind == TypeKind::Enum) {
        pushFlowFrame(false, breakLabel, std::string(), jsLabel);
        const bool lowered = lowerEnumSwitch(s, out, indent);
        popFlowFrame();
        if (lowered) {
            line(out, indent, breakLabel + ": ;");
            return breakLabel;
        }
    }

    const std::string dispatchLabel = freshLabel("switch_dispatch_");
    const std::string sw = freshLabel("switch_value_");
    pushFlowFrame(false, breakLabel, std::string(), jsLabel);

    const std::string disc = s.discriminant ? expr(*s.discriminant) : "0.0";
    const CppInfo discInfo =
        s.discriminant ? info(*s.discriminant) : CppInfo{};
    line(out, indent, "auto " + sw + " = " + disc + ";");
    line(out, indent, "goto " + dispatchLabel + ";");

    std::vector<std::string> caseLabels;
    std::size_t defaultIndex = static_cast<std::size_t>(-1);
    for (std::size_t i = 0; i < s.cases.size(); ++i) {
        caseLabels.push_back(freshLabel("switch_case_"));
    }
    for (std::size_t i = 0; i < s.cases.size(); ++i) {
        const SwitchCase* c = s.cases[i].get();
        if (!c) continue;
        if (c->isDefault || !c->test) defaultIndex = i;
        line(out, indent, caseLabels[i] + ": {");
        for (const auto& stmtPtr : c->statements) {
            if (stmtPtr) stmt(*stmtPtr, out, indent + 1);
        }
        line(out, indent, "}");
    }
    // Falling out of the last case block ends the switch; without this the
    // fall-through would reach `dispatch:` and re-enter the chain forever.
    line(out, indent, "goto " + breakLabel + ";");

    line(out, indent, dispatchLabel + ": {");
    for (std::size_t i = 0; i < s.cases.size(); ++i) {
        const SwitchCase* c = s.cases[i].get();
        if (!c || c->isDefault || !c->test) continue;
        const CppInfo testInfo = info(*c->test);
        const bool comparable = discInfo.representable &&
                                testInfo.representable &&
                                discInfo.cpp == testInfo.cpp;
        std::string cond;
        if (comparable) {
            cond = switchCompare(sw, expr(*c->test));
        } else {
            notImplemented(*c->test, "this switch case test type");
            cond = "false";
        }
        line(out, indent + 1, "if (" + cond + ") goto " + caseLabels[i] + ";");
    }
    if (defaultIndex != static_cast<std::size_t>(-1)) {
        line(out, indent + 1, "goto " + caseLabels[defaultIndex] + ";");
    }
    line(out, indent + 1, "goto " + breakLabel + ";");
    line(out, indent, "}");
    popFlowFrame();
    line(out, indent, breakLabel + ": ;");
    return breakLabel;
}

// -----------------------------------------------------------------------------
// labels + break / continue
// -----------------------------------------------------------------------------
void Generator::emitLabeled(const LabeledStatement& l, std::string& out,
                            int indent) {
    if (!l.body) {
        line(out, indent, "/* empty labeled statement */");
        return;
    }
    switch (l.body->kind) {
        case NodeKind::WhileStatement:
            emitWhile(static_cast<const WhileStatement&>(*l.body), l.label, out,
                      indent);
            return;
        case NodeKind::DoWhileStatement:
            emitDoWhile(static_cast<const DoWhileStatement&>(*l.body), l.label,
                        out, indent);
            return;
        case NodeKind::ForStatement:
            emitFor(static_cast<const ForStatement&>(*l.body), l.label, out,
                    indent);
            return;
        case NodeKind::ForOfStatement:
            emitForOf(static_cast<const ForOfStatement&>(*l.body), l.label, out,
                      indent);
            return;
        case NodeKind::SwitchStatement:
            emitSwitch(static_cast<const SwitchStatement&>(*l.body), l.label,
                       out, indent);
            return;
        default: {
            const std::string endLabel = freshLabel("label_end_");
            labelStack_.push_back(
                LabelFrame{l.label, false, endLabel, std::string()});
            stmt(*l.body, out, indent);
            labelStack_.pop_back();
            line(out, indent, endLabel + ": ;");
            return;
        }
    }
}

void Generator::emitBreak(const BreakStatement& b, std::string& out,
                          int indent) {
    if (b.hasLabel) {
        for (auto it = labelStack_.rbegin(); it != labelStack_.rend(); ++it) {
            if (it->name == b.label) {
                line(out, indent, "goto " + it->breakLabel + ";");
                return;
            }
        }
        notImplemented(b, "break to this label");
        line(out, indent, "break;");
        return;
    }
    if (!flowStack_.empty()) {
        line(out, indent, "goto " + flowStack_.back().breakLabel + ";");
        return;
    }
    notImplemented(b, "break outside a loop or switch");
    line(out, indent, "break;");
}

void Generator::emitContinue(const ContinueStatement& c, std::string& out,
                             int indent) {
    if (c.hasLabel) {
        for (auto it = labelStack_.rbegin(); it != labelStack_.rend(); ++it) {
            if (it->name == c.label && it->isLoop) {
                line(out, indent, "goto " + it->continueLabel + ";");
                return;
            }
        }
        notImplemented(c, "continue to this label");
        line(out, indent, "continue;");
        return;
    }
    for (auto it = flowStack_.rbegin(); it != flowStack_.rend(); ++it) {
        if (it->isLoop) {
            line(out, indent, "goto " + it->continueLabel + ";");
            return;
        }
    }
    notImplemented(c, "continue outside a loop");
    line(out, indent, "continue;");
}

// -----------------------------------------------------------------------------
// the stmt() hook
// -----------------------------------------------------------------------------
// The statements this unit owns, reached before the ordinary arm table in
// stmt() (which keeps the if/block/declaration arms). Returns false for
// everything else so that table still runs.
bool Generator::emitControlDispatch(const Stmt& s, std::string& out,
                                    int indent) {
    switch (s.kind) {
        case NodeKind::WhileStatement:
            emitWhile(static_cast<const WhileStatement&>(s), std::string(), out,
                      indent);
            return true;
        case NodeKind::DoWhileStatement:
            emitDoWhile(static_cast<const DoWhileStatement&>(s), std::string(),
                        out, indent);
            return true;
        case NodeKind::ForStatement:
            emitFor(static_cast<const ForStatement&>(s), std::string(), out,
                    indent);
            return true;
        case NodeKind::ForOfStatement:
            emitForOf(static_cast<const ForOfStatement&>(s), std::string(), out,
                      indent);
            return true;
        case NodeKind::ForInStatement:
            emitForIn(static_cast<const ForInStatement&>(s), std::string(), out,
                      indent);
            return true;
        case NodeKind::SwitchStatement:
            emitSwitch(static_cast<const SwitchStatement&>(s), std::string(),
                       out, indent);
            return true;
        case NodeKind::LabeledStatement:
            emitLabeled(static_cast<const LabeledStatement&>(s), out, indent);
            return true;
        case NodeKind::BreakStatement:
            emitBreak(static_cast<const BreakStatement&>(s), out, indent);
            return true;
        case NodeKind::ContinueStatement:
            emitContinue(static_cast<const ContinueStatement&>(s), out, indent);
            return true;
        default:
            return false;
    }
}

}  // namespace cp3
