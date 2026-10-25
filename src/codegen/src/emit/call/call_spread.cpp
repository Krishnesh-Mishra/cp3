// =============================================================================
// src/codegen/src/emit/call_spread.cpp - the spread argument lowering
// (STYLE.md section 8).
//
// A trailing spread argument (`f(a, ...xs)`) fills the parameters it reaches
// with the elements of the spread source, in source order (JS semantics, spec
// 06). The source is evaluated once: it is bound to a local in a prologue and
// the call is emitted inside a lambda, so the spread's elements are read from
// that one value.
//
// A rest callee packs the whole argument vector, so a spread after its fixed
// parameters joins the packed array at its source position (`f(1, ...xs)`,
// `f(...xs, 5)`); only a spread that also fills a fixed parameter still has no
// lowering.
//
// A parameter the call may leave out (a default or an optional one) takes its
// default when the spread carries no element for it - JS gives `undefined`
// there, and the default fires. A required parameter reads its element out of
// bounds when the spread is too short, which is the cp3 out-of-range error
// (spec 09): a cp3 parameter type never takes `undefined`.
//
// The callee dispatch (the other argument forms) lives in emit/call_args.cpp.
// =============================================================================
#include "detail/expr/call_builder.h"

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "detail/generator/cpp_writer.h"
#include "detail/generator/generator.h"
#include "detail/generator/type_map.h"

namespace cp3 {

using namespace detail;

namespace detail {

// The call text: `callee(args)`, or a lambda that binds the spread source
// once (JS evaluates a spread source once) and calls the callee with it.
std::string CallBuilder::callText(const std::string& callee,
                                  const CallArgs& args) {
    const std::string list = joinStrings(args.values, ", ");
    if (args.prologue.empty()) return callee + "(" + list + ")";
    return "[&]{ " + args.prologue + "return " + callee + "(" + list + "); }()";
}

// The argument text of a spread argument: a rest-only callee takes the array
// itself, a rest callee splices the spread's elements into its packed array,
// and a fixed-argument callee takes the spread's elements. False when the call
// has no lowering for the spread.
bool CallBuilder::spreadArg(const CallExpr& c, const ParamList* params,
                            const std::vector<std::optional<Type>>& ptypes,
                            ArgTexts& texts) {
    const std::size_t index = texts.spreadIndex;
    if (index >= c.arguments.size()) return false;
    const Expr* arg = c.arguments[index].get();
    if (arg == nullptr || arg->kind != NodeKind::SpreadElement) return false;
    const auto& spread = static_cast<const SpreadElement&>(*arg);
    if (!spread.argument) return false;
    std::size_t restAt = params ? params->size() : 0;
    const bool hasRest = findRestParam(params, restAt);
    if (hasRest && restAt == 0 && index == 0 && c.arguments.size() == 1) {
        // `f(...xs)` for a rest-only callee: the array itself.
        texts.spreadRest = true;
        const std::optional<Type> saved = g.expected_;
        if (restAt < ptypes.size()) g.expected_ = ptypes[restAt];
        texts.value[index] = g.expr(*spread.argument);
        g.expected_ = saved;
        return true;
    }
    if (hasRest) {
        // A rest callee absorbs the whole argument vector: a spread after the
        // fixed parameters joins the packed array at its source position,
        // before or between the rest elements, with no arity switch. A spread
        // that also fills a fixed parameter (one before the rest parameter)
        // would have to splice two arrays at run time.
        if (index < restAt) return false;
        const Type at = g.effectiveType(*spread.argument);
        if (at.kind != TypeKind::Array || at.parts.empty()) return false;
        const CppInfo ec = classify(at.parts.front());
        if (!ec.representable || ec.shape == Shape::Void) return false;
        // The element type the packed array uses: the rest parameter's
        // substituted type, the unified argument type, or - for an untyped
        // rest with only spreads - the unified spread element type.
        std::optional<Type> elem = restElemTypeAt(c, ptypes, restAt);
        if (!elem || elem->kind == TypeKind::TypeParam) {
            Type unified = Type::unknown();
            bool any = false;
            for (std::size_t i = restAt; i < c.arguments.size(); ++i) {
                const Expr* other = c.arguments[i].get();
                if (!other || other->kind != NodeKind::SpreadElement) continue;
                const auto& otherSpread =
                    static_cast<const SpreadElement&>(*other);
                if (!otherSpread.argument) continue;
                const Type otherAt = g.effectiveType(*otherSpread.argument);
                if (otherAt.kind != TypeKind::Array || otherAt.parts.empty()) {
                    continue;
                }
                unified = any ? unify(unified, otherAt.parts.front())
                              : otherAt.parts.front();
                any = true;
            }
            if (any) elem = unified;
        }
        if (!elem) return false;
        const CppInfo want = classify(*elem);
        if (!want.representable || want.shape == Shape::Void) return false;
        // The packed vector's element type takes the spread's elements (a
        // `number[]` spread into a `(number | undefined)[]` rest parameter
        // converts to the optional element).
        if (want.cpp != ec.cpp && !isAssignable(at.parts.front(), *elem)) {
            return false;
        }
        const std::string name = "cp3_rest_spread_" + std::to_string(index);
        const std::optional<Type> saved = g.expected_;
        g.expected_ = at;
        const std::string source = g.expr(*spread.argument);
        g.expected_ = saved;
        texts.prologue += "const auto& " + name + " = " + source + "; ";
        texts.value[index] = std::string();
        return true;
    }
    // A trailing spread into a fixed-argument callee.
    if (index + 1 != c.arguments.size()) return false;
    if (params != nullptr) {
        if (!spreadArgs(c, params, ptypes, texts)) return false;
    } else {
        // A value callee (`f(...xs)` through a `std::function`) has no
        // parameter list: its function type is the parameter list.
        ParamList synthetic;
        synthetic.reserve(ptypes.size());
        for (std::size_t i = 0; i < ptypes.size(); ++i) {
            synthetic.push_back(std::make_unique<Parameter>(c.span));
        }
        if (!spreadArgs(c, &synthetic, ptypes, texts)) return false;
    }
    texts.value[index] = std::string();
    return true;
}

// A trailing spread into a fixed-argument callee: the spread's elements fill
// the parameters the spread reaches (a parameter the call may leave out takes
// its default). The source is bound once, in a prologue.
bool CallBuilder::spreadArgs(const CallExpr& c, const ParamList* params,
                              const std::vector<std::optional<Type>>& ptypes,
                              ArgTexts& texts) {
    if (params == nullptr) return false;
    const std::size_t index = texts.spreadIndex;
    if (index >= c.arguments.size()) return false;
    const Expr* arg = c.arguments[index].get();
    if (arg == nullptr || arg->kind != NodeKind::SpreadElement) return false;
    const auto& spread = static_cast<const SpreadElement&>(*arg);
    if (spread.argument == nullptr) return false;
    const Type at = g.effectiveType(*spread.argument);
    if (at.kind != TypeKind::Array || at.parts.empty()) return false;
    const Type elem = at.parts.front();
    const CppInfo ec = classify(elem);
    if (!ec.representable || ec.shape == Shape::Void) return false;
    SpreadSource src;
    src.first = index;
    src.elem = elem;
    src.name = g.freshLabel("spread_");
    const std::optional<Type> saved = g.expected_;
    g.expected_ = at;
    const std::string text = g.expr(*spread.argument);
    g.expected_ = saved;
    g.needsTypedArray_ = true;
    texts.hasSpread = true;
    texts.prologue = "const auto& " + src.name + " = " + text + "; ";
    texts.fixedCount = index < params->size() ? index : params->size();
    texts.spreadValues = spreadValues(*params, ptypes, src);
    return true;
}

// The argument text of a parameter the call may leave out: the spread's
// element, or the parameter's default when the spread carries none for it.
std::string CallBuilder::spreadOmittableArg(const Parameter* p, const Type& pt,
                                            const std::string& element,
                                            const std::string& present) {
    const CppInfo pc = classify(pt);
    if (pc.representable && pc.shape == Shape::Optional && !pt.parts.empty()) {
        // An optional parameter is an `std::optional<T>`: the element is
        // wrapped in it, and the empty case is the default or nullopt.
        const CppInfo inner = classify(pt.parts.front());
        if (inner.representable) {
            const std::string fallback =
                (p != nullptr && p->defaultValue != nullptr)
                    ? "std::optional<" + inner.cpp + ">(" +
                          g.expr(*p->defaultValue) + ")"
                    : std::string("std::nullopt");
            return "(" + present + " ? std::optional<" + inner.cpp + ">(" +
                   element + ") : " + fallback + ")";
        }
    }
    const std::string fallback = (p != nullptr && p->defaultValue != nullptr)
                                    ? g.expr(*p->defaultValue)
                                    : std::string("0.0");
    return "(" + present + " ? " + element + " : " + fallback + ")";
}

// The argument texts of the parameters a spread fills: the i-th parameter the
// spread reaches takes the spread's i-th element. A parameter with a default
// (or an optional one) takes the default when the spread carries no element
// for it; a required parameter reads the element out of bounds, which is the
// cp3 out-of-range error (spec 09).
std::vector<std::string> CallBuilder::spreadValues(
    const ParamList& params, const std::vector<std::optional<Type>>& ptypes,
    const SpreadSource& src) {
    std::vector<std::string> out;
    for (std::size_t j = src.first; j < params.size(); ++j) {
        const Parameter* p = params[j].get();
        const std::size_t k = j - src.first;
        const std::string element = "(*" + src.name + ")[" + std::to_string(k) + "]";
        const std::string present =
            "(" + src.name + ")->size() > " + std::to_string(k);
        const Type pt = (j < ptypes.size() && ptypes[j]) ? *ptypes[j]
                                                          : Type::unknown();
        if (p != nullptr && (p->optional || p->defaultValue)) {
            out.push_back(spreadOmittableArg(p, pt, element, present));
            continue;
        }
        // A required parameter: the element is read out of bounds when the
        // spread is too short (the cp3 out-of-range error).
        out.push_back("::cp3::runtime::arrayAt(" + src.name + ", " +
                      std::to_string(k) + ".0)");
    }
    return out;
}

}  // namespace detail
}  // namespace cp3
