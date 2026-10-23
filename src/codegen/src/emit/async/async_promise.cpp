// =============================================================================
// src/codegen/src/emit/async_promise.cpp - the Promise combinators
// (`Promise.all` / `Promise.race` / `Promise.allSettled`), spec 14.
//
// The operand is an array of promises: an array literal or any expression of
// an array type (`Promise.all(promises)`). A plain value counts as an
// already-resolved promise, exactly like JS, so the operand's element type
// decides the promise's value type:
//
//   Promise.all([p1, p2])   -> the values, in input order (a cp3 array)
//   Promise.all([1, 2])     -> the values themselves (each wrapped in a
//                              resolved promise)
//   Promise.race([p1, p2])  -> the first promise to settle
//
// The runtime side is cp3/async/promise_combinators.hpp: `all` and
// `allSettled` resolve with a cp3 array, so the awaited value has the array
// surface (`.length`, `[i]`, `.join(",")`).
// =============================================================================
#include "detail/generator/generator.h"

#include <string>
#include <vector>

#include "detail/generator/cpp_writer.h"
#include "detail/generator/type_map.h"
#include "cp3/diag/diagnostic_codes.hpp"

namespace cp3 {

using namespace detail;

namespace {

// The element type of an array-typed operand (Unknown for anything else).
Type operandElementType(const Type& operand) {
    if (operand.kind != TypeKind::Array || operand.parts.empty()) {
        return Type::unknown();
    }
    return operand.parts.front();
}

}  // namespace

std::string Generator::emitPromiseCombinator(const CallExpr& c,
                                              const std::string& prop) {
    if (c.arguments.size() != 1 || !c.arguments.front()) {
        reportCodegenError(c, diagcode::kArityMismatch,
                           "arity mismatch: Promise." + prop +
                               " expects one array of promises");
        return "0.0";
    }
    const Expr& operand = *c.arguments.front();
    const bool literal = operand.kind == NodeKind::ArrayLiteral;
    const Type elem = operandElementType(effectiveType(operand));
    if (elem.isUnknown() && !literal) {
        reportCodegenError(c, diagcode::kTypeMismatch,
                           "type mismatch: Promise." + prop +
                               " expects an array of promises");
        return "0.0";
    }
    // The promise's value type: the operand's element type, unwrapped when it
    // is a promise (a plain value is a value of its own).
    const Type value = isPromiseType(elem) ? promiseValue(elem) : elem;
    const CppInfo valueInfo = classify(value);
    const CppInfo promiseInfo = classifyPromiseValue(promiseOf(value));
    if (!valueInfo.representable || !promiseInfo.representable ||
        valueInfo.shape == Shape::Void) {
        reportCodegenError(c, diagcode::kTypeMismatch,
                           "type mismatch: Promise." + prop +
                               " needs a promise value type with a C++ "
                               "spelling");
        return "0.0";
    }
    // A plain value is an already-resolved promise (JS parity).
    const bool wrap = !isPromiseType(elem);
    const std::string resolve =
        "::cp3::runtime::Promise<" + valueInfo.cpp + ">::resolve(";
    std::string vector;
    if (literal) {
        const auto& array = static_cast<const ArrayLiteral&>(operand);
        // A spread element (`Promise.all([...a, p])`) joins the list, so the
        // vector is built at runtime instead of in a braced initializer.
        bool hasSpread = false;
        for (const auto& element : array.elements) {
            if (element && element->kind == NodeKind::SpreadElement) {
                hasSpread = true;
                break;
            }
        }
        if (hasSpread) {
            const std::string v = freshLabel("cp3_promises_");
            std::string body = "std::vector<" + promiseInfo.cpp + "> " + v + ";";
            for (const auto& element : array.elements) {
                if (!element) continue;
                if (element->kind != NodeKind::SpreadElement) {
                    const std::string text = expr(*element);
                    body += " " + v + ".push_back(" +
                            (wrap ? resolve + text + ")" : text) + ");";
                    continue;
                }
                const auto& sp = static_cast<const SpreadElement&>(*element);
                // The spread source holds the promises, or - when a plain
                // value counts as an already-resolved promise - the values
                // that resolve.
                const std::string src =
                    spreadArrayText(sp, wrap ? valueInfo.cpp : promiseInfo.cpp);
                if (src.empty()) {
                    reportTypeError(*element,
                                    "type mismatch: a spread element of Promise." +
                                        prop + " must be an array of " +
                                        (wrap ? describe(value)
                                              : describe(promiseOf(value))));
                    return "0.0";
                }
                if (wrap) {
                    body += " { const auto& cp3_src = " + src +
                            "; for (auto cp3_it = cp3_src->begin(); "
                            "cp3_it != cp3_src->end(); ++cp3_it) { " +
                            v + ".push_back(" + resolve + "*cp3_it" + ")); } }";
                } else {
                    body += " { const auto& cp3_src = " + src + "; " + v +
                            ".insert(" + v + ".end(), cp3_src->begin(), "
                            "cp3_src->end()); }";
                }
            }
            vector = "[&]{ " + body + " return " + v + "; }()";
        } else {
            std::vector<std::string> elements;
            for (const auto& element : array.elements) {
                if (!element) continue;
                const std::string text = expr(*element);
                elements.push_back(wrap ? resolve + text + ")" : text);
            }
            vector = "std::vector<" + promiseInfo.cpp + ">{" +
                     joinStrings(elements, ", ") + "}";
        }
    } else {
        // The operand is an array value: its elements are the promises (or
        // the plain values), and the operand itself is evaluated once.
        const std::string item = freshLabel("cp3_value_");
        vector = "[&]{ std::vector<" + promiseInfo.cpp +
                 "> cp3_promises; for (const auto& " + item + " : *" +
                 expr(operand) + ") { cp3_promises.push_back(" +
                 (wrap ? resolve + item + ")" : item) +
                 "); } return cp3_promises; }()";
    }
    return "::cp3::runtime::" + prop + "(" + vector + ")";
}

}  // namespace cp3
