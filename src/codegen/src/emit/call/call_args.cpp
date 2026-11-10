// =============================================================================
// src/codegen/src/emit/call_args.cpp - argument lowering and value calls
// (STYLE.md section 8).
//
// `emit/call.cpp` keeps the dispatch entry point; this unit owns the pieces a
// call needs once its callee is known: optional/undefined handling, rest
// packing, the `std::function` vs boxed `Listener` forms, and the static
// dispatch for identifier and member callees. They live on a friend helper
// (`detail::CallBuilder`) so the Generator declaration stays within budget
// without introducing a second copy of the emitter state.
// =============================================================================
#include "detail/generator/generator.h"

#include <algorithm>
#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "detail/expr/call_builder.h"
#include "detail/class/class_model.h"
#include "detail/generator/cpp_writer.h"
#include "detail/generator/mangle.h"
#include "detail/expr/spread_call.h"
#include "detail/int/timers.h"
#include "detail/generator/type_map.h"
#include "cp3/diag/diagnostic_codes.hpp"

namespace cp3 {

using namespace detail;

namespace detail {
// default expression, or is declared optional (`a?: T` lowers to
// `std::optional<T> = std::nullopt`).
bool optionalParamAt(const ParamList& params, std::size_t i) {
    if (i >= params.size()) return false;
    const Parameter* p = params[i].get();
    return p && (p->optional || p->defaultValue != nullptr);
}

const Parameter* paramAt(const ParamList& params, std::size_t i) {
    if (i >= params.size()) return nullptr;
    return params[i].get();
}

// The index of the (first) rest parameter of `params`: it absorbs the whole
// argument vector, so the fixed parameters before it are still passed
// positionally. false when there is none.
bool CallBuilder::findRestParam(const ParamList* params,
                                std::size_t& restAt) const {
    if (!params) return false;
    for (std::size_t i = 0; i < params->size(); ++i) {
        const Parameter* p = (*params)[i].get();
        if (p && p->isRest) {
            restAt = i;
            return true;
        }
    }
    return false;
}

// The element type the rest parameter packs into (`E` of its `E[]`).
std::optional<Type> CallBuilder::restElemType(
    const std::vector<std::optional<Type>>& ptypes, std::size_t restAt) const {
    if (restAt >= ptypes.size() || !ptypes[restAt]) return std::nullopt;
    const Type& t = *ptypes[restAt];
    if (t.kind == TypeKind::Array && !t.parts.empty()) return t.parts.front();
    return std::nullopt;
}

// The element type at this call site. A generic callee whose rest element
// type is one of its type parameters cannot be spelled by the caller: the
// packed array then carries the type of the arguments the rest parameter
// absorbs, and C++ deduces the callee's type parameter from that array.
std::optional<Type> CallBuilder::restElemTypeAt(
    const CallExpr& c, const std::vector<std::optional<Type>>& ptypes,
    std::size_t restAt) const {
    const std::optional<Type> elem = restElemType(ptypes, restAt);
    if (!elem || elem->kind != TypeKind::TypeParam) return elem;
    Type unified = Type::unknown();
    bool any = false;
    for (std::size_t i = restAt; i < c.arguments.size(); ++i) {
        const Expr* arg = c.arguments[i].get();
        if (arg == nullptr || arg->kind == NodeKind::SpreadElement) continue;
        const Type at = g.effectiveType(*arg);
        unified = any ? unify(unified, at) : at;
        any = true;
    }
    if (!any) return elem;
    return unified;
}

// The C++ spelling of the cp3 `undefined` value for a rest element type that
// admits it: an optional element (`(number | undefined)[]`) is empty, an
// `undefined` element is the undefined singleton, a three-way nullish
// element (`T | null | undefined`) is the undefined flavor. "" when the
// element type has no room for an undefined value.
std::string undefinedElementText(const Type& elem) {
    if (elem.isUndefined()) return "cp3::runtime::undefined";
    const CppInfo ec = classify(elem);
    if (ec.shape == Shape::Optional) return "std::nullopt";
    if (ec.shape == Shape::Maybe) {
        return ec.cpp + "(cp3::runtime::undefined)";
    }
    return std::string();
}

namespace {

// The value shapes `EventValue` can carry (cp3/async/emitter.hpp): the
// primitive values plus the two nullish ones. A listener call erases its
// arguments into these, so any other shape is a type error there.
bool eventValueShape(Shape s) {
    return s == Shape::Number || s == Shape::String || s == Shape::Bool ||
           s == Shape::Undefined || s == Shape::Null;
}

}  // namespace

// Stage 1: the text of every argument, plus the explicit `undefined`s that
// must not be passed. Each positional argument is emitted against its
// parameter's expected type (`ptypes`).
CallBuilder::ArgTexts CallBuilder::callArgTexts(
    const CallExpr& c, const ParamList* params,
    const std::vector<std::optional<Type>>& ptypes, bool applyDefaults,
    bool reifyObjectArgs) {
    const std::size_t count = c.arguments.size();
    const std::size_t paramCount = params ? params->size() : 0;
    std::size_t restAt = paramCount;
    const bool hasRest = findRestParam(params, restAt);
    std::optional<Type> elem =
        hasRest ? restElemTypeAt(c, ptypes, restAt) : std::nullopt;

    ArgTexts texts;
    texts.value.resize(count);
    texts.drop.assign(count, false);
    for (std::size_t i = 0; i < count; ++i) {
        const Expr* arg = c.arguments[i].get();
        if (!arg) {
            texts.value[i] = "0.0";
            continue;
        }
        if (arg->kind == NodeKind::SpreadElement) {
            texts.spreadIndex = i;
            if (hasRest && i < restAt) {
                // A spread that reaches a rest callee's fixed parameters. The
                // source is bound once here; callArgs() maps its elements onto
                // the fixed parameters and the packed rest array at run time
                // (the argument vector is the concatenation of every argument,
                // so a later plain argument can still fill a fixed parameter
                // when the spread is too short).
                const auto& spread = static_cast<const SpreadElement&>(*arg);
                const Type at = spread.argument
                                    ? g.effectiveType(*spread.argument)
                                    : Type::unknown();
                const CppInfo ec =
                    at.kind == TypeKind::Array && !at.parts.empty()
                        ? classify(at.parts.front())
                        : CppInfo{};
                if (spread.argument == nullptr ||
                    at.kind != TypeKind::Array || at.parts.empty() ||
                    !ec.representable || ec.shape == Shape::Void) {
                    g.reportTypeError(
                        *arg,
                        "type mismatch: a spread argument must be an array "
                        "with a C++-representable element type");
                    texts.value[i] = "0.0";
                    continue;
                }
                const std::string name =
                    "cp3_lead_spread_" + std::to_string(i);
                const std::optional<Type> saved = g.expected_;
                g.expected_ = at;
                const std::string source = g.expr(*spread.argument);
                g.expected_ = saved;
                texts.prologue +=
                    "const auto& " + name + " = " + source + "; ";
                texts.value[i] = std::string();
                continue;
            }
            if (!spreadArg(c, params, ptypes, texts)) {
                // The spread has no lowering because the construct is wrong,
                // not because codegen is missing it: say which error it is.
                if (hasRest) {
                    g.reportTypeError(
                        *arg,
                        "type mismatch: a spread argument must be an "
                        "array of the rest parameter's element type");
                } else if (i + 1 != count) {
                    g.reportCodegenError(
                        c, diagcode::kArityMismatch,
                        "a spread argument must be the last argument of a "
                        "call with a fixed argument list");
                } else {
                    g.reportTypeError(
                        *arg,
                        "type mismatch: a spread argument must be an array");
                }
                texts.value[i] = "0.0";
            }
            continue;
        }
        // A generic function's parameter type is opaque at the call site, so
        // an object-literal argument is reified into a generated aggregate
        // struct (support/generic.cpp).
        if (reifyObjectArgs && arg->kind == NodeKind::ObjectLiteral) {
            texts.value[i] =
                g.reifyObjectArg(static_cast<const ObjectLiteral&>(*arg));
            continue;
        }
        const bool optionalHere = i < paramCount && optionalParamAt(*params, i);
        const bool inRest = hasRest && i >= restAt;
        if (arg->kind == NodeKind::UndefinedLiteral && optionalHere && !inRest) {
            texts.drop[i] = true;
            continue;
        }
        if (arg->kind == NodeKind::UndefinedLiteral && inRest) {
            // `undefined` is a value of the rest array when the element type
            // admits it (`(number | undefined)[]` lowers to an optional
            // element); otherwise the element type has no room for it.
            const std::string undef =
                elem ? undefinedElementText(*elem) : std::string();
            if (!undef.empty()) {
                texts.value[i] = undef;
                continue;
            }
            g.reportTypeError(
                *arg,
                "type mismatch: 'undefined' is not assignable to the rest "
                "parameter's element type");
            texts.value[i] = "0.0";
            continue;
        }
        const std::optional<Type> saved = g.expected_;
        if (inRest) {
            g.expected_ = elem;
        } else if (i < ptypes.size()) {
            g.expected_ = ptypes[i];
        }
        texts.value[i] = g.expr(*arg);
        g.expected_ = saved;
    }
    // An explicit `undefined` that is not trailing must be substituted (a
    // C++ `nullptr`/`undefined` cannot bind the parameter).
    for (std::size_t i = 0; i < count; ++i) {
        if (!texts.drop[i]) continue;
        const Parameter* p = paramAt(*params, i);
        if (applyDefaults || i + 1 < count || hasRest) {
            texts.value[i] = (p && p->defaultValue) ? g.expr(*p->defaultValue)
                                                    : "std::nullopt";
        }
    }
    // A proven non-owning parameter takes a C++ reference rather than Ref<T>.
    // Dereferencing works for both an owning Ref<T> argument and a stack
    // allocation already lowered to T*.
    if (params != nullptr) {
        const std::size_t n = std::min(count, params->size());
        for (std::size_t i = 0; i < n; ++i) {
            const Parameter* parameter = (*params)[i].get();
            if (parameter == nullptr ||
                g.nonOwningParamDecls_.count(parameter->nodeId) == 0 ||
                texts.drop[i] || texts.value[i].empty()) continue;
            const Expr* arg = c.arguments[i].get();
            if (arg == nullptr || arg->kind == NodeKind::SpreadElement) continue;
            texts.value[i] = "(*(" + texts.value[i] + "))";
        }
    }
    return texts;
}

// Whether the argument list has a spread at or after `from`.
namespace {
bool hasSpreadArg(const CallExpr& c, std::size_t from) {
    for (std::size_t i = from; i < c.arguments.size(); ++i) {
        const Expr* arg = c.arguments[i].get();
        if (arg && arg->kind == NodeKind::SpreadElement) return true;
    }
    return false;
}
}  // namespace

// Stage 2a: the rest callee's argument vector.
std::vector<std::string> CallBuilder::packRestArgs(
    const CallExpr& c, const ArgTexts& texts, const std::optional<Type>& elem,
    std::size_t restAt) {
    const std::size_t count = texts.value.size();
    std::vector<std::string> out;
    for (std::size_t i = 0; i < restAt && i < count; ++i) {
        out.push_back(texts.value[i]);
    }
    const CppInfo ec = elem ? classify(*elem) : CppInfo{};
    std::string elemCpp;
    if (elem && elem->kind != TypeKind::TypeParam && ec.representable &&
        ec.shape != Shape::Void) {
        elemCpp = ec.cpp;
    }
    if (elemCpp.empty() && elem && elem->kind == TypeKind::TypeParam) {
        // An untyped rest parameter with no argument to deduce from: the
        // packed array is empty, so any element type is a valid witness.
        elemCpp = "double";
    }
    if (elemCpp.empty()) {
        g.reportTypeError(
            c,
            "type mismatch: this rest parameter's element type has no C++ "
            "representation");
        elemCpp = "double";
    }
    if (texts.spreadRest) {
        out.push_back(texts.value[0]);
    } else if (count > restAt && hasSpreadArg(c, restAt)) {
        // A rest callee absorbs the whole argument vector, so a spread joins
        // the packed array at its source position - before, between or after
        // fixed arguments, with no arity switch.
        std::string body = "std::vector<" + elemCpp + "> cp3_rest;";
        for (std::size_t i = restAt; i < count; ++i) {
            const Expr* arg = c.arguments[i].get();
            if (arg && arg->kind == NodeKind::SpreadElement) {
                const std::string name =
                    "cp3_rest_spread_" + std::to_string(i);
                body += " { const auto& cp3_s = " + name +
                        "; cp3_rest.insert(cp3_rest.end(), cp3_s->begin(), "
                        "cp3_s->end()); }";
                continue;
            }
            if (!texts.value[i].empty()) {
                body += " cp3_rest.push_back(" + texts.value[i] + ");";
            }
        }
        body += " return ::cp3::runtime::makeArray<" + elemCpp +
                ">(std::move(cp3_rest));";
        out.push_back("[&]{ " + body + " }()");
    } else if (count <= restAt) {
        // No trailing argument: the rest parameter is an empty array.
        out.push_back("::cp3::runtime::makeArray<" + elemCpp + ">()");
    } else {
        // The rest parameter collects the arguments *after* the fixed ones
        // (JS: `f(a, ...rest)` binds only the trailing arguments).
        std::vector<std::string> elems;
        elems.reserve(count - restAt);
        for (std::size_t i = restAt; i < count; ++i) {
            elems.push_back(texts.value[i].empty() ? "0.0" : texts.value[i]);
        }
        out.push_back("::cp3::runtime::makeArray<" + elemCpp +
                      ">(std::vector<" + elemCpp + ">{" +
                      joinStrings(elems, ", ") + "})");
    }
    return out;
}

// Stage 2b: the plain callee's argument vector. A trailing dropped argument is
// omitted so the C++ default applies; `applyDefaults` appends the callee's
// defaults at the call site (a value callee - a lambda - cannot carry C++
// default arguments).
std::vector<std::string> CallBuilder::plainArgList(const ArgTexts& texts,
                                                   const ParamList* params,
                                                   bool applyDefaults) {
    if (texts.hasSpread) {
        // A trailing spread: the fixed arguments come first, then the ones the
        // spread fills. The extra fixed arguments (more than the callee takes)
        // are dropped, as JS does.
        const std::size_t fixed = texts.fixedCount;
        std::vector<std::string> out(texts.value.begin(),
                                     texts.value.begin() +
                                         static_cast<std::ptrdiff_t>(fixed));
        for (const std::string& value : texts.spreadValues) out.push_back(value);
        return out;
    }
    const std::size_t paramCount = params ? params->size() : 0;
    std::size_t passed = texts.value.size();
    if (!applyDefaults) {
        while (passed > 0 && texts.drop[passed - 1]) --passed;
    }
    std::vector<std::string> out(texts.value.begin(),
                                 texts.value.begin() +
                                     static_cast<std::ptrdiff_t>(passed));
    if (applyDefaults) {
        for (std::size_t i = out.size(); i < paramCount; ++i) {
            const Parameter* p = paramAt(*params, i);
            if (!p || p->isRest) break;
            if (p->defaultValue) {
                out.push_back(g.expr(*p->defaultValue));
            } else if (p->optional) {
                out.push_back("std::nullopt");
            } else {
                break;
            }
        }
    }
    return out;
}

CallBuilder::CallArgs CallBuilder::callArgs(
    const CallExpr& c, const ParamList* params,
    const std::vector<std::optional<Type>>& ptypes, bool applyDefaults,
    bool reifyObjectArgs) {
    std::size_t restAt = params ? params->size() : 0;
    const bool hasRest = findRestParam(params, restAt);
    const ArgTexts texts =
        callArgTexts(c, params, ptypes, applyDefaults, reifyObjectArgs);
    CallArgs out;
    if (hasRest) {
        // A spread that reaches a fixed parameter of a rest callee. The
        // argument vector is the concatenation of every argument, so the
        // fixed positions are only known at run time: map them onto the bound
        // spread sources and the plain argument texts, then pack the rest.
        bool leadingSpread = false;
        for (std::size_t i = 0; i < restAt && i < c.arguments.size(); ++i) {
            const Expr* arg = c.arguments[i].get();
            if (arg && arg->kind == NodeKind::SpreadElement) {
                leadingSpread = true;
                break;
            }
        }
        if (leadingSpread) {
            out.prologue = texts.prologue;
            // Every spread must have a binding: the leading ones were bound by
            // callArgTexts, the others by spreadArg. A failed one has already
            // reported; stop before referring to a missing binding.
            for (std::size_t i = 0; i < c.arguments.size(); ++i) {
                const Expr* arg = c.arguments[i].get();
                if (!arg || arg->kind != NodeKind::SpreadElement) continue;
                const auto& spread = static_cast<const SpreadElement&>(*arg);
                if (!spread.argument) {
                    out.values = texts.value;
                    return out;
                }
                const Type at = g.effectiveType(*spread.argument);
                const CppInfo ec =
                    at.kind == TypeKind::Array && !at.parts.empty()
                        ? classify(at.parts.front())
                        : CppInfo{};
                if (at.kind != TypeKind::Array || at.parts.empty() ||
                    !ec.representable || ec.shape == Shape::Void) {
                    out.values = texts.value;
                    return out;
                }
            }
            // The element type the packed rest array carries. A generic rest
            // parameter is unified from the spread elements and the trailing
            // plain arguments.
            std::optional<Type> elem = restElemTypeAt(c, ptypes, restAt);
            if (!elem || elem->kind == TypeKind::TypeParam) {
                Type unified = Type::unknown();
                bool any = false;
                for (std::size_t i = 0; i < c.arguments.size(); ++i) {
                    const Expr* arg = c.arguments[i].get();
                    if (!arg) continue;
                    if (arg->kind == NodeKind::SpreadElement) {
                        const auto& spread =
                            static_cast<const SpreadElement&>(*arg);
                        if (!spread.argument) continue;
                        const Type at = g.effectiveType(*spread.argument);
                        if (at.kind != TypeKind::Array || at.parts.empty()) {
                            continue;
                        }
                        unified = any ? unify(unified, at.parts.front())
                                      : at.parts.front();
                        any = true;
                    } else if (i >= restAt) {
                        const Type at = g.effectiveType(*arg);
                        unified = any ? unify(unified, at) : at;
                        any = true;
                    }
                }
                if (any) elem = unified;
            }
            const CppInfo ec = elem ? classify(*elem) : CppInfo{};
            std::string elemCpp;
            if (elem && elem->kind != TypeKind::TypeParam &&
                ec.representable && ec.shape != Shape::Void) {
                elemCpp = ec.cpp;
            }
            if (elemCpp.empty() && elem &&
                elem->kind == TypeKind::TypeParam) {
                elemCpp = "double";
            }
            if (elemCpp.empty()) {
                g.reportTypeError(
                    c,
                    "type mismatch: this rest parameter's element type has "
                    "no C++ representation");
                elemCpp = "double";
            }
            g.needsTypedArray_ = true;

            const auto spreadName = [restAt](std::size_t i) {
                return (i < restAt ? "cp3_lead_spread_"
                                   : "cp3_rest_spread_") +
                       std::to_string(i);
            };
            std::vector<std::string> starts(c.arguments.size());
            std::vector<std::string> sizes(c.arguments.size());
            std::string cursor = "0";
            for (std::size_t i = 0; i < c.arguments.size(); ++i) {
                starts[i] = cursor;
                const Expr* arg = c.arguments[i].get();
                sizes[i] =
                    arg && arg->kind == NodeKind::SpreadElement
                        ? "(" + spreadName(i) + ")->size()"
                        : "1";
                if (i + 1 < c.arguments.size()) {
                    cursor = cursor == "0"
                                 ? sizes[i]
                                 : "(" + cursor + " + " + sizes[i] + ")";
                }
            }

            // The fixed parameters: the argument that covers the position at
            // run time supplies it. A required parameter with no argument is
            // the cp3 out-of-range read; an optional/defaulted one takes its
            // default.
            std::vector<std::string> fixed;
            fixed.reserve(restAt);
            std::size_t firstSpread = restAt;
            for (std::size_t i = 0; i < c.arguments.size(); ++i) {
                const Expr* arg = c.arguments[i].get();
                if (arg && arg->kind == NodeKind::SpreadElement) {
                    firstSpread = i;
                    break;
                }
            }
            for (std::size_t j = 0; j < restAt; ++j) {
                const Parameter* p = paramAt(*params, j);
                const Type pt = (j < ptypes.size() && ptypes[j])
                                    ? *ptypes[j]
                                    : Type::unknown();
                const CppInfo pc = classify(pt);
                const bool optional =
                    p != nullptr && (p->optional || p->defaultValue != nullptr);
                const bool optionalType =
                    pc.representable && pc.shape == Shape::Optional &&
                    !pt.parts.empty() &&
                    classify(pt.parts.front()).representable;
                std::string text;
                if (optional) {
                    if (optionalType) {
                        const CppInfo inner = classify(pt.parts.front());
                        text = p->defaultValue
                                   ? "std::optional<" + inner.cpp + ">(" +
                                         g.expr(*p->defaultValue) + ")"
                                   : std::string("std::nullopt");
                    } else {
                        text = p->defaultValue
                                   ? g.expr(*p->defaultValue)
                                   : std::string("0.0");
                    }
                } else if (firstSpread < c.arguments.size()) {
                    // The read is out of range exactly when no argument
                    // covers the position.
                    text = "::cp3::runtime::arrayAt(" +
                           spreadName(firstSpread) + ", " +
                           std::to_string(j) + ".0)";
                } else {
                    text = "0.0";
                }
                for (std::size_t i = c.arguments.size(); i > 0; --i) {
                    const std::size_t k = i - 1;
                    const Expr* arg = c.arguments[k].get();
                    std::string value;
                    if (arg && arg->kind == NodeKind::SpreadElement) {
                        const std::string index =
                            starts[k] == "0"
                                ? std::to_string(j) + ".0"
                                : "static_cast<double>(" +
                                      std::to_string(j) + " - (" +
                                      starts[k] + "))";
                        value = "::cp3::runtime::arrayAt(" +
                                spreadName(k) + ", " + index + ")";
                    } else {
                        value = texts.value[k].empty() ? "0.0"
                                                       : texts.value[k];
                    }
                    if (optionalType) {
                        const CppInfo inner = classify(pt.parts.front());
                        value = "std::optional<" + inner.cpp + ">(" + value +
                                ")";
                    }
                    const std::string cond =
                        "(" + std::to_string(j) + " >= " + starts[k] +
                        " && " + std::to_string(j) + " < " + starts[k] +
                        " + " + sizes[k] + ")";
                    text = "(" + cond + " ? " + value + " : " + text + ")";
                }
                fixed.push_back(std::move(text));
            }

            // The rest array: every argument's overlap with the positions at
            // or after the rest parameter, in source order.
            std::string body = "std::vector<" + elemCpp + "> cp3_rest;";
            for (std::size_t i = 0; i < c.arguments.size(); ++i) {
                const Expr* arg = c.arguments[i].get();
                if (arg && arg->kind == NodeKind::SpreadElement) {
                    body += " { const std::size_t cp3_from = (" + starts[i] +
                            ") > " + std::to_string(restAt) + " ? (" +
                            starts[i] + ") : " + std::to_string(restAt) +
                            "; for (std::size_t cp3_k = cp3_from - (" +
                            starts[i] + "); cp3_k < (" + sizes[i] +
                            "); ++cp3_k) cp3_rest.push_back((*" +
                            spreadName(i) + ")[cp3_k]); }";
                    continue;
                }
                if (starts[i] == "0" && restAt > 0) continue;
                body += " if ((" + starts[i] + ") >= " +
                        std::to_string(restAt) + ") cp3_rest.push_back(" +
                        (texts.value[i].empty() ? "0.0" : texts.value[i]) +
                        ");";
            }
            out.values = std::move(fixed);
            out.values.push_back(
                "[&]{ " + body + " return ::cp3::runtime::makeArray<" +
                elemCpp + ">(std::move(cp3_rest)); }()");
            return out;
        }
        // The element type at THIS call site: the substituted type argument,
        // or the unified type of the arguments the rest parameter absorbs.
        std::optional<Type> elem = restElemTypeAt(c, ptypes, restAt);
        // An untyped rest parameter keeps its type parameter when every
        // argument is a spread; the spread's element type is the concrete
        // witness the packed array uses.
        if (!elem || elem->kind == TypeKind::TypeParam) {
            for (std::size_t i = restAt; i < c.arguments.size(); ++i) {
                const Expr* arg = c.arguments[i].get();
                if (!arg || arg->kind != NodeKind::SpreadElement) continue;
                const auto& spread = static_cast<const SpreadElement&>(*arg);
                if (!spread.argument) continue;
                const Type at = g.effectiveType(*spread.argument);
                if (at.kind == TypeKind::Array && !at.parts.empty()) {
                    elem = at.parts.front();
                    break;
                }
            }
        }
        out.values = packRestArgs(c, texts, elem, restAt);
        // A rest-spread binds its source in the prologue, exactly like the
        // fixed-argument spread path does.
        out.prologue = texts.prologue;
        return out;
    }
    out.values = plainArgList(texts, params, applyDefaults);
    out.prologue = texts.prologue;
    return out;
}

// The call text, the trailing-spread lowering and the parameter texts a
// spread fills live in emit/call_spread.cpp.

// A call through a function-typed value: `text(...)` for the
// `std::function` spelling, `text.invoke(...)` for the boxed Listener.
// `params` is the callee's parameter list when it has one (a value callee's
// call-site defaults / rest packing); an unsupported callee reports and stubs.
std::string CallBuilder::emitFunctionCall(const CallExpr& c,
                                          const std::string& text,
                                          const Type& fnType,
                                          const ParamList* params) {
    const CppInfo ci = classify(fnType);
    if (ci.shape == Shape::Function) {
        std::vector<std::optional<Type>> ptypes;
        ptypes.reserve(fnType.parts.size());
        for (std::size_t i = 0; i + 1 < fnType.parts.size(); ++i) {
            ptypes.push_back(fnType.parts[i]);
        }
        // A lambda cannot carry C++ default arguments, so a value callee's
        // defaults are passed at the call site.
        const CallArgs args = callArgs(c, params, ptypes, params != nullptr);
        return callText(text, args);
    }
    if (ci.shape == Shape::Listener) {
        g.needsEventEmitter_ = true;
        if (c.arguments.empty()) return text + ".invoke()";
        if (c.arguments.size() == 1 && c.arguments.front() &&
            c.arguments.front()->kind != NodeKind::SpreadElement) {
            return text + ".invoke(::cp3::runtime::EventValue(" +
                   g.expr(*c.arguments.front()) + "))";
        }
        // A listener takes a list of erased values (up to kMaxListenerArgs):
        // pass every argument, with a spread's elements spliced in. A value
        // `EventValue` cannot carry is a type error, not a missing feature.
        std::string body = "std::vector<::cp3::runtime::EventValue> cp3_vals;";
        for (const auto& argPtr : c.arguments) {
            const Expr* arg = argPtr.get();
            if (!arg) continue;
            // The direct arguments need the same check the spread path
            // applies (an unemittable value is a diagnostic, not a C++
            // wall of errors).
            if (arg->kind != NodeKind::SpreadElement) {
                const CppInfo ec = classify(g.effectiveType(*arg));
                if (!ec.representable || !eventValueShape(ec.shape)) {
                    g.reportTypeError(
                        *arg,
                        "type mismatch: a listener argument must be a "
                        "primitive value (null, number, boolean, string or "
                        "undefined)");
                    return "0.0";
                }
            }
            if (arg->kind == NodeKind::SpreadElement) {
                const auto& spread = static_cast<const SpreadElement&>(*arg);
                if (!spread.argument) continue;
                const Type at = g.effectiveType(*spread.argument);
                const CppInfo ec =
                    at.kind == TypeKind::Array && !at.parts.empty()
                        ? classify(at.parts.front())
                        : CppInfo{};
                if (!ec.representable || !eventValueShape(ec.shape)) {
                    g.reportTypeError(
                        *arg,
                        "type mismatch: a listener argument must be a "
                        "primitive value (null, number, boolean, string or "
                        "undefined)");
                    return "0.0";
                }
                body += " { const auto& cp3_s = " + g.expr(*spread.argument) +
                        "; for (const auto& cp3_e : *cp3_s) cp3_vals.push_back("
                        "::cp3::runtime::EventValue(cp3_e)); }";
                continue;
            }
            body += " cp3_vals.push_back(::cp3::runtime::EventValue(" +
                    g.expr(*arg) + "));";
        }
        return "[&]{ " + body + " return " + text +
               ".invoke(cp3_vals.data(), cp3_vals.size()); }()";
    }
    g.reportCodegenError(c, diagcode::kTypeMismatch,
                         "type mismatch: this value is not callable");
    return "0.0";
}

std::string CallBuilder::emitIdentifierCall(const CallExpr& c) {
    const std::string name = static_cast<const Identifier&>(*c.callee).name;
    // A local module's imported binding (emit/program.cpp's link table): the
    // target unit's qualified C++ name. A link miss reports at the use site.
    if (std::string linked = g.emitLinkedCall(c, name); !linked.empty()) {
        return linked;
    }
    // The Error hierarchy is callable as well as constructible: `Error("x")`
    // and `new Error("x")` produce the same handle. A user function or a
    // shadowing binding still wins.
    if (g.functions_.find(name) == g.functions_.end() &&
        g.importBindings_.find(name) == g.importBindings_.end() &&
        !g.hasValueBinding(name)) {
        if (const ClassModel* cm = ModelRegistry::get().classByName(name);
            cm != nullptr && cm->builtin) {
            return g.emitErrorCall(c, *cm);
        }
    }
    if (g.functions_.find(name) == g.functions_.end() &&
        g.importBindings_.find(name) == g.importBindings_.end() &&
        !g.hasValueBinding(name) && isTimerBuiltinName(name)) {
        return g.emitTimerBuiltin(c, name);
    }
    // `String(value)`: the scalar conversion global (a user-declared `String`
    // function wins - checked above).
    if (name == "String" && g.functions_.find(name) == g.functions_.end()) {
        return g.emitStringBuiltin(c);
    }
    if ((name == "parseInt" || name == "parseFloat") &&
        g.functions_.find(name) == g.functions_.end()) {
        return g.emitParseBuiltin(c, name);
    }
    if ((name == "Number" || name == "Boolean" || name == "isNaN" ||
         name == "isFinite") &&
        g.functions_.find(name) == g.functions_.end()) {
        return g.emitCoercionBuiltin(c, name);
    }
    if ((name == "atob" || name == "btoa") &&
        g.functions_.find(name) == g.functions_.end()) {
        return g.emitBase64Builtin(c, name);
    }
    if (name == "parseTo" && g.functions_.find(name) == g.functions_.end()) {
        return g.emitParseToBuiltin(c);
    }
    if (name == "structuredClone" &&
        g.functions_.find(name) == g.functions_.end()) {
        return g.emitCloneBuiltin(c);
    }
    const auto declIt = g.functionDecls_.find(name);
    const FunctionDeclaration* decl =
        declIt != g.functionDecls_.end() ? declIt->second : nullptr;
    // A `let f = <T>(x: T) => x` binding: the arrow owns the type parameters,
    // so an explicit type-argument list names them (spec �16) exactly like a
    // declared generic function's.
    const bool genericArrow = g.isGenericArrowBinding(name);
    bool generic =
        genericArrow || (decl != nullptr && !decl->typeParams.empty());
    if (!generic) {
        // An imported generic function: the declaring unit's AST is not in
        // this unit's function table, but the link table recorded whether
        // the target declares type parameters.
        if (const auto linked = g.linkedNames_.find(name);
            linked != g.linkedNames_.end() && linked->second.isGeneric) {
            generic = true;
        }
    }
    if (!c.typeArgs.empty() && !generic) {
        // A type argument list on a function that declares no type parameters
        // is a type error (sema reports it first; the codegen repeats it so a
        // codegen-only pass stays honest).
        g.reportUnsupported(c, diagcode::kTypeArgsOnNonGeneric,
                            "type arguments are not allowed here: '" + name +
                                "' is not a generic function");
        return "0.0";
    }
    // A name that is not a function declared in this unit: a locally bound
    // function value (`let add = (a, b) => ...; add(1, 2)`) still calls as
    // a value; anything else is unresolved.
    if (g.functions_.find(name) == g.functions_.end()) {
        if (auto b = g.lookup(name); b && b->type.kind == TypeKind::Function) {
            // A `let f = (...) => ...` binding carries the arrow's declared
            // parameter defaults, which its C++ lambda cannot: pass them at
            // this call site.
            const ParamList* params = nullptr;
            if (auto arrow = g.captures_.arrowOf.find(name);
                arrow != g.captures_.arrowOf.end()) {
                params = &arrow->second->params;
            }
            std::string callee =
                g.emitIdentifier(static_cast<const Identifier&>(*c.callee));
            if (genericArrow && !c.typeArgs.empty()) {
                // `f<number>(x)`: the explicit type argument names the
                // arrow's own type parameter, so the C++ call instantiates
                // the generic lambda's template explicitly.
                callee = g.genericArrowCallee(callee, c);
            }
            return emitFunctionCall(c, callee, b->type, params);
        }
        return g.emitUnresolvedCall(c, name);
    }
    if (g.asyncFunctions_.count(name) != 0) {
        // Calling an async function starts async work whose continuations
        // only run when the loop does.
        g.needsEventLoop_ = true;
    }
    std::vector<std::optional<Type>> ptypes;
    if (decl) {
        ptypes.reserve(decl->params.size());
        for (std::size_t i = 0; i < decl->params.size(); ++i) {
            ptypes.push_back(decl->params[i]
                                 ? g.parameterType(*decl->params[i], *decl, i)
                                 : std::nullopt);
        }
        if (generic && !c.typeArgs.empty()) {
            // The explicit type arguments fix the callee's type parameters:
            // the call site spells the substituted types (a rest parameter
            // packs its arguments into an array of the substituted element
            // type, which is what C++ deduces the parameter from).
            for (std::size_t i = 0; i < ptypes.size(); ++i) {
                if (!ptypes[i]) continue;
                ptypes[i] = g.substituteTypeArgs(*ptypes[i], *decl, c);
            }
        }
    }
    const std::string callee =
        generic ? g.calleeText(name, c) : g.moduleRef(name);
    const CallArgs args =
        callArgs(c, decl ? &decl->params : nullptr, ptypes,
                 /*applyDefaults=*/false, generic);
    return callText(callee, args);
}

// The member-call hooks for the receiver shapes that own a call outright: a
// native binding's own function (`dm.add(...)`, lowered against the binding's
// recorded C++ name and parameter list) and the parallel family (spec 13).
// "" when neither owns the call.
std::string CallBuilder::emitOwnedCall(const CallExpr& c, const MemberExpr& m) {
    if (std::string native = g.emitNativeCall(c, m); !native.empty()) {
        return native;
    }
    if (m.object && m.object->kind == NodeKind::Identifier &&
        static_cast<const Identifier&>(*m.object).name == "parallel") {
        // `parallel.for` / `parallel.reduce` / `parallel.run` (spec 13).
        return g.emitParallelCall(c, m);
    }
    return std::string();
}

std::string CallBuilder::emitMemberCall(const CallExpr& c,
                                        const MemberExpr& m) {
    // The receiver shapes that own a call outright: a native binding's own
    // function and the parallel family.
    if (std::string owned = emitOwnedCall(c, m); !owned.empty()) {
        return owned;
    }
    // A call through an import form with no C++ lowering (a namespace
    // import/re-export): the use site is a program error.
    if (std::string miss = g.linkedMemberMiss(m); !miss.empty()) {
        g.reportCodegenError(*m.object, diagcode::kUnresolvedImport,
                             "unresolved import: " + miss);
        return "0.0";
    }
    // The typed worker handle surface (D80, spec 13): `w.postMessage`,
    // `w.on(...)`, `await w.request(v)`, `w.terminate()`, and the worker
    // side's `thread.on(...)` / `thread.postMessage(v)`.
    if (std::string handle = g.emitWorkerHandleCall(c, m); !handle.empty()) {
        return handle;
    }
    // `Promise.all/race/allSettled(...)`. A declared function, imported
    // binding, or local value shadows the global; the member call then goes
    // through the ordinary class/value path.
    if (m.object && m.object->kind == NodeKind::Identifier &&
        static_cast<const Identifier&>(*m.object).name == "Promise" &&
        g.functions_.find("Promise") == g.functions_.end() &&
        g.importBindings_.find("Promise") == g.importBindings_.end() &&
        !g.hasValueBinding("Promise")) {
        return g.emitPromiseStatic(c, m);
    }
    // A Promise method (`then`/`catch`/`finally`); `on/once/off/emit` (D74).
    if (m.object && detail::isPromiseType(g.effectiveType(*m.object))) {
        return g.emitPromiseMemberCall(c, m);
    }
    if (m.object && detail::isEventEmitterType(g.effectiveType(*m.object))) {
        return g.emitEventEmitterCall(c, m);
    }
    // A method call on a generic function's type parameter (`s.area()` where
    // `s: T extends Shape`): `cp3_member_base` dereferences a Ref<T> class
    // handle and leaves a value struct alone.
    if (m.object && g.info(*m.object).shape == Shape::TypeParam) {
        return "cp3_member_base(" + g.expr(*m.object) + ")." +
               mapIdent(m.property) + "(" +
               joinStrings(g.emitPlainArgs(c), ", ") + ")";
    }
    // A class method / interface method call (spec 15) before the builtin
    // namespace surface: a class member name owns its spelling. This hook lived
    // in the pre-redrive dispatcher and was lost in the call_args split.
    if (std::string model = g.emitModelCall(c, m); !model.empty()) {
        return model;
    }
    // `Math.*`, `path.*`, `os.*`, `JSON.stringify`, `process.*`, and the
    // primitive `string` method surface (emit_builtin.cpp).
    if (std::string builtin = g.emitBuiltinCall(c, m); !builtin.empty()) {
        return builtin;
    }
    // The primitive value formatters (`age.toString()`).
    if (c.arguments.empty() && m.property == "toString" && m.object) {
        const CppInfo recv = g.info(*m.object);
        if (recv.shape == Shape::Number || recv.shape == Shape::Bool ||
            recv.shape == Shape::Native) {
            return "::cp3::runtime::toString(" + g.expr(*m.object) + ")";
        }
    }
    // A member read whose value is a function (a class field holding a
    // callable): call it through the value-call lowering.
    const Type memberType = g.effectiveType(m);
    if (memberType.kind == TypeKind::Function) {
        const std::optional<Type> saved = g.expected_;
        g.expected_ = memberType;
        const std::string callee = g.expr(m);
        g.expected_ = saved;
        return emitFunctionCall(c, callee, memberType, nullptr);
    }
    // An ambient/unknown member has no C++ method spelling: the call is a
    // real type error at the use site (sema rejects a member call on
    // `unknown`; this is the codegen-only backstop).
    g.reportCodegenError(c, diagcode::kTypeMismatch,
                         "type mismatch: '" + m.property +
                             "' is not a method of this value");
    return "0.0";
}

}  // namespace detail

// A call to a local module's imported binding (emit/program.cpp's link table):
// the target unit's qualified C++ name, with the callee's declared parameters
// applied to optional/default arguments. A name that is not linked (or is a
// function of this unit) returns "" so the caller keeps its other lowerings.
// A link miss reports an unresolved-import error here, at the use site.
std::string Generator::emitLinkedCall(const CallExpr& c,
                                      const std::string& name) {
    if (functions_.count(name) != 0) return std::string();
    const auto it = linkedNames_.find(name);
    if (it == linkedNames_.end()) return std::string();
    const LinkedName& ln = it->second;
    if (ln.cpp.empty()) {
        // The imported name has no C++ lowering (a namespace import, a
        // type-only export, a binding whose type cp3 cannot spell): calling
        // it is a program error, not a missing feature.
        reportCodegenError(c, diagcode::kUnresolvedImport,
                           "unresolved import: " + ln.miss);
        return "0.0";
    }
    if (ln.fn == nullptr) {
        // A linked name with a C++ spelling that is not a function (a
        // re-exported value): calling it is a program error.
        reportCodegenError(c, diagcode::kUnresolvedImport,
                           "unresolved import: '" + name +
                               "' is not a function");
        return "0.0";
    }
    // The explicit type-argument list of the call (`f<number>(...)` ->
    // `<double>`): the target unit's template is instantiated at the call
    // site, exactly like a call inside the unit itself.
    std::string callee = ln.cpp;
    if (!c.typeArgs.empty()) {
        if (ln.fn->typeParams.empty() && !ln.isGeneric) {
            reportUnsupported(c, diagcode::kTypeArgsOnNonGeneric,
                              "type arguments are not allowed here: '" + name +
                                  "' is not a generic function");
            return "0.0";
        }
        std::vector<std::string> typeArgs;
        typeArgs.reserve(c.typeArgs.size());
        for (const auto& ta : c.typeArgs) {
            if (ta) typeArgs.push_back(typeArgText(*ta));
        }
        callee += "<" + joinStrings(typeArgs, ", ") + ">";
    }
    if (ln.isAsync) {
        // Calling an async function starts async work whose continuations only
        // run when the loop does.
        needsEventLoop_ = true;
    }
    detail::CallBuilder calls(*this);
    std::vector<std::optional<Type>> ptypes;
    ptypes.reserve(ln.fn->params.size());
    for (std::size_t i = 0; i < ln.fn->params.size(); ++i) {
        ptypes.push_back(ln.fn->params[i]
                             ? parameterType(*ln.fn->params[i], *ln.fn, i)
                             : std::nullopt);
    }
    const detail::CallBuilder::CallArgs args =
        calls.callArgs(c, &ln.fn->params, ptypes, /*applyDefaults=*/false);
    return calls.callText(callee, args);
}

// The link miss for a member access whose object is an imported binding with
// no C++ lowering (a namespace import/re-export), reported at the use site.
std::string Generator::linkedMemberMiss(const MemberExpr& m) const {
    if (!m.object || m.object->kind != NodeKind::Identifier) return std::string();
    const auto linked = linkedNames_.find(
        static_cast<const Identifier&>(*m.object).name);
    if (linked == linkedNames_.end() || !linked->second.cpp.empty()) {
        return std::string();
    }
    return linked->second.miss;
}

std::string Generator::emitCall(const CallExpr& c) {
    // `super(...)` is lowered into the constructor's C++ base-initializer
    // (ctorBaseInitText); the expression statement itself has no C++ body.
    if (c.callee && c.callee->kind == NodeKind::Identifier &&
        static_cast<const Identifier&>(*c.callee).name == "super") {
        return std::string();
    }
    if (isConsoleMember(c)) {
        const auto& m = static_cast<const MemberExpr&>(*c.callee);
        std::string text = emitConsoleCall(c, m);
        // A member outside the console surface is not a console call: the
        // ordinary member call owns it (a user value that shadows the
        // console name). Sema rejects an unknown member of the console
        // namespace itself (CP3305, with the documented names as
        // candidates), so this path is the shadowing one.
        if (!text.empty()) return text;
    }
    CallBuilder calls(*this);
    if (c.callee->kind == NodeKind::Identifier) {
        return calls.emitIdentifierCall(c);
    }
    if (c.callee->kind == NodeKind::MemberExpr) {
        return calls.emitMemberCall(c, static_cast<const MemberExpr&>(*c.callee));
    }
    // A call through an indexed function value (`closures[i](x)`): the element
    // expression is the callee.
    if (c.callee->kind == NodeKind::IndexExpr) {
        const Type fnType = effectiveType(*c.callee);
        if (fnType.kind == TypeKind::Function) {
            return calls.emitFunctionCall(c, expr(*c.callee), fnType, nullptr);
        }
    }
    // A call through any other expression (`f()(x)`, an immediately invoked
    // arrow, a conditional function value): the callee is a function value.
    const Type fnType = effectiveType(*c.callee);
    if (fnType.kind == TypeKind::Function) {
        const std::optional<Type> saved = expected_;
        expected_ = fnType;
        const std::string callee = expr(*c.callee);
        expected_ = saved;
        return calls.emitFunctionCall(c, callee, fnType, nullptr);
    }
    // An ambient/unknown callee has no C++ call spelling: the call is a real
    // type error at the use site (sema rejects a call on `unknown`; this is
    // the codegen-only backstop).
    reportTypeError(c, "type mismatch: this value is not callable");
    return "0.0";
}

}  // namespace cp3
