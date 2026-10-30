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
#include "detail/generator/generator.h"

#include <cstddef>
#include <string>
#include <vector>

#include "detail/generator/cpp_writer.h"
#include "detail/generator/mangle.h"
#include "detail/expr/pattern_emitter.h"
#include "detail/generator/type_map.h"
#include "cp3/diag/diagnostic_codes.hpp"

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
    const auto fast = fastDataViewLoops_.find(w.nodeId);
    const bool fastLoop = fast != fastDataViewLoops_.end();
    const auto fastWrites = fastTypedArrayLoops_.find(w.nodeId);
    const bool fastWriteLoop = fastWrites != fastTypedArrayLoops_.end() &&
                               !fastWrites->second.writes.empty();
    const bool fastScope = fastLoop || fastWriteLoop;
    std::string fastRawName;
    std::string fastReceiverText;
    std::optional<std::string> previousRaw;
    std::vector<std::pair<std::uint32_t, std::optional<std::string>>>
        previousTypedPointers;
    if (fastScope) {
        line(out, indent, "{");
        ++indent;
    }
    if (fastLoop) {
        fastRawName = "cp3_fast_bytes_" + std::to_string(w.nodeId);
        fastReceiverText = mapIdent(fast->second.receiver);
        const auto binding = lookup(fast->second.receiver);
        if (binding && binding->global) {
            fastReceiverText = moduleRef(fast->second.receiver);
        }
        const auto previous = activeFastDataViews_.find(fast->second.receiver);
        if (previous != activeFastDataViews_.end()) previousRaw = previous->second;
        if (fast->second.exactReduction) {
            line(out, indent, "#if defined(__clang__)");
            line(out, indent, "#pragma clang fp reassociate(on)");
            line(out, indent, "#endif");
        }
        line(out, indent, "const std::uint8_t* const " + fastRawName + " = " +
                             fastReceiverText + "->rawData;");
        line(out, indent, "if (static_cast<double>(" + expr(*fast->second.bound) +
                             ") > static_cast<double>(" + fastReceiverText +
                             "->byteLength / " + std::to_string(fast->second.width) +
                             ")) ::cp3::runtime::typedArrayPanic(\"DataView access is outside the view\");");
        activeFastDataViews_[fast->second.receiver] = fastRawName;
    }
    if (fastWriteLoop) {
        std::unordered_set<std::uint32_t> checked;
        std::size_t ordinal = 0;
        for (const FastTypedArrayWrite& write : fastWrites->second.writes) {
            std::string receiverText = mapIdent(write.receiver);
            const auto binding = lookup(write.receiver);
            if (binding && binding->global) receiverText = moduleRef(write.receiver);
            std::optional<std::string> previous;
            const auto active = activeFastTypedArrays_.find(write.receiverBinding);
            if (active != activeFastTypedArrays_.end()) previous = active->second;
            previousTypedPointers.push_back({write.receiverBinding, previous});
            const std::string rawName = "cp3_fast_typed_bytes_" +
                std::to_string(w.nodeId) + "_" + std::to_string(ordinal++);
            line(out, indent, "std::uint8_t* const " + rawName + " = " +
                 receiverText + "->buffer->data() + " + receiverText +
                 "->byteOffset;");
            if (checked.insert(write.receiverBinding).second) {
                line(out, indent, "if (static_cast<double>(" +
                     expr(*fastWrites->second.bound) + ") > static_cast<double>(" +
                     receiverText + "->length)) ::cp3::runtime::typedArrayPanic(\"TypedArray access is outside the view\");");
            }
            activeFastTypedArrays_[write.receiverBinding] = rawName;
        }
    }
    const auto finishFastLoop = [&]() {
        if (!fastScope) return;
        if (fastLoop) {
            if (previousRaw) activeFastDataViews_[fast->second.receiver] = *previousRaw;
            else activeFastDataViews_.erase(fast->second.receiver);
        }
        for (auto it = previousTypedPointers.rbegin();
             it != previousTypedPointers.rend(); ++it) {
            if (it->second) activeFastTypedArrays_[it->first] = *it->second;
            else activeFastTypedArrays_.erase(it->first);
        }
        --indent;
        line(out, indent, "}");
    };
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
        finishFastLoop();
        return continueLabel;
    }
    const std::string condition = w.condition ? cond(*w.condition) : "false";
    if (fastLoop && fast->second.exactReduction) {
        line(out, indent, "#if defined(__clang__)");
        line(out, indent, "#pragma clang loop vectorize(enable)");
        line(out, indent, "#endif");
    }
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
    finishFastLoop();
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
    // `for await (...)` awaits every value it binds; it is the same loop with
    // an await on the bound value.
    if (f.isAwait) {
        ArrayEmitter::forAwait(*this, f, jsLabel, out, indent);
        return;
    }
    ArrayEmitter::forOf(*this, f, jsLabel, out, indent);
}

// The `for...in` binding: the key (the index, or a `Map`'s key) bound to the
// loop variable. A destructuring target unpacks the key the same way the
// `for...of` loop unpacks an element.
void Generator::emitForInBinding(const ForInStatement& f,
                                 const std::string& keyExpr,
                                 const std::string& keyCpp, std::string& out,
                                 int indent) {
    const auto* vd =
        f.left && f.left->kind == NodeKind::VariableDeclaration
            ? static_cast<const VariableDeclaration*>(f.left.get())
            : nullptr;
    if (vd != nullptr) {
        const std::string cv = (vd->kind == VarKind::Const) ? "const " : "";
        for (const auto& declPtr : vd->declarations) {
            const VariableDeclarator* d = declPtr.get();
            if (d == nullptr || !d->id) continue;
            if (d->id->kind == NodeKind::Identifier) {
                const std::string name =
                    mapIdent(static_cast<const Identifier&>(*d->id).name);
                const std::optional<Type> bt = bindingType(*d);
                const CppInfo ci = bt ? classify(*bt) : CppInfo{};
                line(out, indent,
                     cv + (ci.representable ? ci.cpp : keyCpp) + " " + name +
                         " = " + keyExpr + ";");
                declare(static_cast<const Identifier&>(*d->id).name, bt);
            } else {
                const std::optional<Type> bt = resolvedBindingType(*d);
                PatternEmitter(*this).emitBindings(*d->id, keyExpr, bt, out,
                                                   indent);
            }
        }
        return;
    }
    if (f.left && isPatternNode(*f.left)) {
        PatternEmitter(*this).emitBindings(*f.left, keyExpr, Type::unknown(),
                                           out, indent);
        return;
    }
    // An assignment target (`for (existing in src)`) is not a binding.
    reportCodegenError(f, diagcode::kTypeMismatch,
                       "type mismatch: the for...in target must be a binding");
}

// `for (const k in source)`: the keys of a `Map` and the indices of an
// ordered collection (an array, a `Set`) - spec 06: "loop over keys/indices".
// A plain object (or a class instance) is banned (spec 08): its field list is
// known at compile time.
void Generator::emitForIn(const ForInStatement& f, const std::string& jsLabel,
                          std::string& out, int indent) {
    if (intOn()) intFail();
    const Type src = f.right ? effectiveType(*f.right) : Type::unknown();
    const bool isMap = src.kind == TypeKind::Class && src.name == "Map";
    const bool isSet = src.kind == TypeKind::Class && src.name == "Set";
    const bool isArray = src.kind == TypeKind::Array;
    if (!f.right || (!isMap && !isSet && !isArray)) {
        reportCodegenError(f, diagcode::kTypeMismatch,
                           "type mismatch: for...in needs an array, a Set or a "
                           "Map");
        line(out, indent, "{");
        line(out, indent + 1, "/* for...in: unsupported source */");
        line(out, indent, "}");
        return;
    }

    const std::string breakLabel = freshLabel("forin_end_");
    const std::string continueLabel = freshLabel("forin_next_");
    // The source handle is bound to a named local before the loop: the
    // expression may build a fresh array (a literal) and the loop must not
    // rebuild it on every iteration.
    const std::string source = freshLabel("forin_src_");
    const std::string recv = expr(*f.right);
    // The key: the index of an ordered collection, a `Map`'s own key type.
    std::string keyExpr;
    std::string keyCpp = "double";
    std::string header;
    if (isMap) {
        const Type keyType =
            src.parts.empty() ? Type::unknown() : src.parts.front();
        const CppInfo kc = classify(keyType);
        if (!kc.representable) {
            reportCodegenError(f, diagcode::kTypeMismatch,
                               "type mismatch: a Map key type has no C++ "
                               "spelling");
            line(out, indent, "{");
            line(out, indent + 1, "/* for...in: unsupported Map key */");
            line(out, indent, "}");
            return;
        }
        keyCpp = kc.cpp;
        // `m.keys()` is the runtime's insertion-ordered key array (D44).
        const std::string keys = freshLabel("forin_keys_");
        const std::string item = freshLabel("forin_key_");
        needsCollections_ = true;
        header = "for (auto " + keys + " = (" + recv + ")->keys(); " + kc.cpp +
                 " " + item + " : *" + keys + ") {";
        keyExpr = item;
    } else {
        // The indices: the array's slots, the `Set`'s entries. The counter is
        // declared before the loop: a C++ `for` header carries one
        // declaration list, so it cannot hold the source handle and the
        // counter at once.
        needsTypedArray_ = true;
        const std::string idx = freshLabel("forin_idx_");
        line(out, indent, "double " + idx + " = 0.0;");
        header = "for (auto " + source + " = " + recv + "; " + idx +
                 " < static_cast<double>(" + source + "->size()); " + idx +
                 " += 1.0) {";
        keyExpr = idx;
    }

    pushFlowFrame(true, breakLabel, continueLabel, jsLabel);
    pushScope();
    line(out, indent, header);
    emitForInBinding(f, keyExpr, keyCpp, out, indent + 1);
    if (f.body) emitBranch(*f.body, out, indent + 1);
    line(out, indent + 1, continueLabel + ": ;");
    line(out, indent, "}");
    popScope();
    popFlowFrame();
    line(out, indent, breakLabel + ": ;");
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

// The "does it hold a value" test of an optional (`std::optional`) or of a
// flavored `Maybe` (the three-way nullable).
std::string optionalHasText(Shape shape, const std::string& text) {
    return shape == Shape::Optional ? text + ".has_value()" : text + ".hasValue()";
}

// The value an optional (`std::optional`) or a flavored `Maybe` holds.
std::string optionalValueText(Shape shape, const std::string& text) {
    return shape == Shape::Optional ? "*" + text : text + ".value()";
}

}  // namespace

// The comparison of a switch case test against the discriminant. A `switch`
// compares with `===` (JS): two different primitive kinds never match (D15), a
// union matches when it holds the test's alternative, and an optional matches
// when it holds the value. A test that cannot match the discriminant is dead
// code, but its expression is still evaluated (JS evaluates the case
// expressions in order, until one matches).
std::string Generator::switchCaseCompare(const std::string& disc,
                                         const CppInfo& discInfo,
                                         const std::string& test,
                                         const CppInfo& testInfo) {
    // The same representation: compare directly.
    if (discInfo.representable && testInfo.representable &&
        discInfo.cpp == testInfo.cpp) {
        return switchCompare(disc, test);
    }
    // A primitive union on either side: the held alternative must match.
    const bool lUnion = discInfo.shape == Shape::Union;
    const bool rUnion = testInfo.shape == Shape::Union;
    if (lUnion || rUnion) {
        needsUnion_ = true;
        const std::string uni = lUnion ? disc : test;
        const std::string other = lUnion ? test : disc;
        return "(::cp3::runtime::unions::strictEq(" + uni + ", " + other + "))";
    }
    // An optional (or a flavored `Maybe`) on either side: it matches only
    // when it holds a value, and the value must match.
    const bool lOpt =
        discInfo.shape == Shape::Optional || discInfo.shape == Shape::Maybe;
    const bool rOpt =
        testInfo.shape == Shape::Optional || testInfo.shape == Shape::Maybe;
    if (lOpt && rOpt) {
        return "((" + optionalHasText(discInfo.shape, disc) + ") && (" +
               optionalHasText(testInfo.shape, test) + ") && (" +
               optionalValueText(discInfo.shape, disc) + " == " +
               optionalValueText(testInfo.shape, test) + "))";
    }
    if (lOpt) {
        return "((" + optionalHasText(discInfo.shape, disc) + ") && (" +
               optionalValueText(discInfo.shape, disc) + " == " + test + "))";
    }
    if (rOpt) {
        return "((" + optionalHasText(testInfo.shape, test) + ") && (" + disc +
               " == " + optionalValueText(testInfo.shape, test) + "))";
    }
    // A number-backed enum member against a plain number: the underlying
    // value compares (JS parity, the same rule the `==` lowering uses). A
    // string-backed member never equals a number.
    if (discInfo.shape == Shape::Enum && testInfo.cpp == "double") {
        const auto it = enums_.find(discInfo.type.name);
        if (it == enums_.end() || !it->second.stringBacked) {
            return "((static_cast<double>(" + disc + ")) == " + test + ")";
        }
    }
    if (testInfo.shape == Shape::Enum && discInfo.cpp == "double") {
        const auto it = enums_.find(testInfo.type.name);
        if (it == enums_.end() || !it->second.stringBacked) {
            return "((" + disc + " == static_cast<double>(" + test + ")))";
        }
    }
    // A native number against a plain number: compare numerically.
    if ((discInfo.shape == Shape::Native &&
         (testInfo.shape == Shape::Native || testInfo.shape == Shape::Number)) ||
        (testInfo.shape == Shape::Native &&
         (discInfo.shape == Shape::Native || discInfo.shape == Shape::Number))) {
        return switchCompare(disc, test);
    }
    // Two different kinds: the test can never match, but it is still
    // evaluated (the comma operator keeps the order JS prescribes).
    return "(" + test + ", false)";
}

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
        const std::string cond =
            switchCaseCompare(sw, discInfo, expr(*c->test), testInfo);
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
        // Sema rejects an undefined label (CP3306); the codegen repeats the
        // diagnostic so the statement never lowers to a C++ `break` outside a
        // loop.
        reportCodegenError(b, diagcode::kUndefinedLabel,
                           "undefined label '" + b.label + "'");
        line(out, indent, "{ /* break to an undefined label */ }");
        return;
    }
    if (!flowStack_.empty()) {
        line(out, indent, "goto " + flowStack_.back().breakLabel + ";");
        return;
    }
    // Sema rejects this form (CP3502); the codegen repeats the diagnostic so
    // the statement never lowers to a C++ `break` outside a loop.
    reportCodegenError(b, diagcode::kBreakOutsideLoop,
                       "break outside of a loop or switch");
    line(out, indent, "{ /* break outside a loop or switch */ }");
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
        // Sema rejects both forms (an undefined label, and a label that does
        // not denote an iteration statement), so this only runs for a program
        // that reached codegen without the frontend diagnostic. The error is
        // reported here too, with the same wording, and the statement lowers
        // to a no-op: a C++ `continue` outside a loop would not compile.
        bool found = false;
        for (auto it = labelStack_.rbegin(); it != labelStack_.rend(); ++it) {
            if (it->name == c.label) {
                found = true;
                break;
            }
        }
        if (found) {
            reportCodegenError(
                c, diagcode::kContinueNonIterationLabel,
                "continue label '" + c.label +
                    "' does not denote an iteration statement");
        } else {
            reportCodegenError(c, diagcode::kUndefinedLabel,
                               "undefined label '" + c.label + "'");
        }
        line(out, indent, "{ /* continue outside an iteration statement */ }");
        return;
    }
    for (auto it = flowStack_.rbegin(); it != flowStack_.rend(); ++it) {
        if (it->isLoop) {
            line(out, indent, "goto " + it->continueLabel + ";");
            return;
        }
    }
    // Sema reports this form (CP3503); the codegen repeats the diagnostic so
    // the statement never lowers to a C++ `continue` outside a loop.
    reportCodegenError(c, diagcode::kContinueOutsideLoop,
                       "continue outside of a loop");
    line(out, indent, "{ /* continue outside of a loop */ }");
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
