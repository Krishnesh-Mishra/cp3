// =============================================================================
// src/language/arrays/codegen_methods.cpp - the regular-array method surface
// (docs/language-spec/09-arrays-strings.md): mutation, callback iteration,
// searching, slicing, the in-place mutators, `for...of` and `for await`.
//
// detail::ArrayEmitter (generator.h) holds the static entry points; Generator
// friends it for the private emitters it drives. The lowering calls the
// runtime extras in cp3/array_extras.hpp with the element type from sema;
// callback methods lower the arrow with the element type as its parameter
// context. A callback may also be a named function (the runtime takes any
// callable), and a type the element or the result has no C++ spelling is
// reported as a type error instead of being lowered.
// =============================================================================
#include "detail/generator/generator.h"

#include <cstddef>
#include <string>
#include <vector>

#include "detail/generator/cpp_writer.h"
#include "detail/generator/family.h"
#include "detail/generator/mangle.h"
#include "detail/expr/pattern_emitter.h"
#include "detail/generator/type_map.h"
#include "cp3/diag/diagnostic_codes.hpp"
#include "cp3/host/native_types.hpp"

namespace cp3 {

using namespace detail;

namespace {

// The arrow argument at `index`, or null: the caller reports the unsupported
// non-arrow form (a named function callback is not lowered).
const ArrowFunction* arrowArg(const CallExpr& c, std::size_t index) {
    if (index >= c.arguments.size() || !c.arguments[index]) return nullptr;
    const Expr* arg = c.arguments[index].get();
    if (arg->kind != NodeKind::ArrowFunction) return nullptr;
    return static_cast<const ArrowFunction*>(arg);
}

bool isOneOf(const std::string& prop, const char* const* names,
             std::size_t count) {
    for (std::size_t i = 0; i < count; ++i) {
        if (prop == names[i]) return true;
    }
    return false;
}

template <std::size_t N>
bool isOneOf(const std::string& prop, const char* const (&names)[N]) {
    return isOneOf(prop, names, N);
}

bool numericValueType(const Type& type) {
    return type.kind == TypeKind::Number || isNativeType(type);
}

bool pureNumericExpr(const SemaResult& sema,
                     const std::unordered_map<std::uint32_t, double>& constants,
                     const Expr& expr,
                     const std::unordered_set<std::uint32_t>& parameters) {
    switch (expr.kind) {
        case NodeKind::NumberLiteral:
            return true;
        case NodeKind::Identifier: {
            const auto binding = sema.resolvedBindings.find(expr.nodeId);
            if (binding == sema.resolvedBindings.end()) return false;
            return parameters.contains(binding->second) ||
                   constants.contains(binding->second);
        }
        case NodeKind::UnaryExpr: {
            const auto& unary = static_cast<const UnaryExpr&>(expr);
            return (unary.op == UnaryOp::Negate || unary.op == UnaryOp::Plus) &&
                   unary.operand &&
                   pureNumericExpr(sema, constants, *unary.operand, parameters);
        }
        case NodeKind::BinaryExpr: {
            const auto& binary = static_cast<const BinaryExpr&>(expr);
            switch (binary.op) {
                case BinaryOp::Add: case BinaryOp::Subtract:
                case BinaryOp::Multiply: case BinaryOp::Divide:
                case BinaryOp::Modulo: case BinaryOp::Equal:
                case BinaryOp::NotEqual: case BinaryOp::StrictEqual:
                case BinaryOp::StrictNotEqual: case BinaryOp::LessThan:
                case BinaryOp::GreaterThan: case BinaryOp::LessThanEqual:
                case BinaryOp::GreaterThanEqual: case BinaryOp::BitAnd:
                case BinaryOp::BitOr: case BinaryOp::BitXor:
                case BinaryOp::ShiftLeft: case BinaryOp::ShiftRight:
                case BinaryOp::UnsignedShiftRight:
                    return binary.left && binary.right &&
                           pureNumericExpr(sema, constants, *binary.left, parameters) &&
                           pureNumericExpr(sema, constants, *binary.right, parameters);
                default:
                    return false;
            }
        }
        default:
            return false;
    }
}

bool pureArrow(const SemaResult& sema,
               const std::unordered_map<std::uint32_t, double>& constants,
               const ArrowFunction& arrow,
               std::size_t arity) {
    if (arrow.isAsync || !arrow.webserverionBody || !arrow.body ||
        arrow.body->kind == NodeKind::BlockStatement ||
        arrow.params.size() != arity) return false;
    std::unordered_set<std::uint32_t> parameters;
    for (const auto& param : arrow.params) {
        if (!param || param->isRest || param->optional || param->defaultValue ||
            !param->name || param->name->kind != NodeKind::Identifier)
            return false;
        parameters.insert(param->nodeId);
    }
    return pureNumericExpr(sema, constants,
                           static_cast<const Expr&>(*arrow.body), parameters);
}

bool pureMapFilterReduce(const SemaResult& sema,
                         const std::unordered_map<std::uint32_t, double>& constants,
                         const CallExpr& reduce,
                         const MemberExpr& reduceMember,
                         const CallExpr*& filter, const MemberExpr*& filterMember,
                         const CallExpr*& map, const MemberExpr*& mapMember,
                         const ArrowFunction*& mapFn,
                         const ArrowFunction*& filterFn,
                         const ArrowFunction*& reduceFn) {
    if (reduceMember.property != "reduce" || reduce.arguments.size() != 2 ||
        !reduce.arguments[1]) return false;
    const Expr* filterExpr = reduceMember.object.get();
    if (!filterExpr || filterExpr->kind != NodeKind::CallExpr) return false;
    filter = static_cast<const CallExpr*>(filterExpr);
    if (!filter->callee || filter->callee->kind != NodeKind::MemberExpr)
        return false;
    filterMember = static_cast<const MemberExpr*>(filter->callee.get());
    if (filterMember->property != "filter" || !filterMember->object) return false;
    const Expr* mapExpr = filterMember->object.get();
    if (!mapExpr || mapExpr->kind != NodeKind::CallExpr) return false;
    map = static_cast<const CallExpr*>(mapExpr);
    if (!map->callee || map->callee->kind != NodeKind::MemberExpr)
        return false;
    mapMember = static_cast<const MemberExpr*>(map->callee.get());
    if (mapMember->property != "map" || !mapMember->object ||
        map->arguments.size() != 1 || filter->arguments.size() != 1)
        return false;
    mapFn = arrowArg(*map, 0);
    filterFn = arrowArg(*filter, 0);
    reduceFn = arrowArg(reduce, 0);
    if (!mapFn || !filterFn || !reduceFn ||
        !pureArrow(sema, constants, *mapFn, 1) ||
        !pureArrow(sema, constants, *filterFn, 1) ||
        !pureArrow(sema, constants, *reduceFn, 2))
        return false;
    const Type* sourceType = sema.typeOf(mapMember->object->nodeId);
    const Type* mappedType = sema.typeOf(map->nodeId);
    const Type* accumulatorType = sema.typeOf(reduce.nodeId);
    const Type* initialType = sema.typeOf(reduce.arguments[1]->nodeId);
    if (!sourceType || !mappedType || !accumulatorType || !initialType)
        return false;
    const Type& source = *sourceType;
    const Type& mapped = *mappedType;
    const Type& accumulator = *accumulatorType;
    const Type& initial = *initialType;
    return source.kind == TypeKind::Array && mapped.kind == TypeKind::Array &&
           !source.parts.empty() && !mapped.parts.empty() &&
           numericValueType(source.parts.front()) &&
           numericValueType(mapped.parts.front()) &&
           numericValueType(accumulator) && numericValueType(initial);
}

}  // namespace

// The callback set (`map` / `filter` / `reduce` / `find` / `findIndex` /
// `some` / `every`): each arrow lowers with its parameter context.
std::string detail::ArrayEmitter::callbackText(Generator& g, const CallExpr& c,
                                               const MemberExpr& m,
                                               const CppInfo& ec,
                                               const std::string& recvText) {
    const std::string& prop = m.property;
    const ArrowFunction* cb = arrowArg(c, 0);
    // The C++ spelling of the receiver for the third callback parameter. The
    // runtime adapter passes the same handle it is iterating.
    const std::string arrayCpp = "::cp3::runtime::Array<" + ec.cpp + ">";
    const std::vector<std::string> callbackContext = {ec.cpp, "double",
                                                      arrayCpp};
    // A named function (or a function-typed binding) is a callback too: the
    // runtime takes any callable, so the function's own C++ signature is
    // used and the element type is not a parameter context.
    const Expr* fn = nullptr;
    if (cb == nullptr) {
        fn = (c.arguments.size() > 0 && c.arguments[0]) ? c.arguments[0].get()
                                                         : nullptr;
        if (fn == nullptr) {
            // No callback at all: sema reports the arity mismatch, and the
            // diagnostic is repeated here so the call never lowers to a
            // runtime call with a missing argument.
            g.reportCodegenError(c, diagcode::kArityMismatch,
                                 "arity mismatch: expected 1, got 0 arguments");
            return "0.0";
        }
    }
    if (prop == "map") {
        // The result array's element type: the callback's return type.
        const Type resultElem = arrayElemOf(g.effectiveType(c));
        CppInfo rc;
        if (!arrayElementInfo(resultElem, rc)) {
            g.reportArrayElementType(resultElem, c, "the result of map()");
            return "0.0";
        }
        const std::string call =
            cb != nullptr ? g.emitArrow(*cb, callbackContext) : g.expr(*fn);
        return "::cp3::runtime::arrayMap<" + rc.cpp + ">(" + recvText + ", " +
               call + ")";
    }
    if (prop == "flatMap") {
        // The callback returns an array; its element is the flattened result's
        // element (`U[]` for `(value) => U[]`).
        const Type resultElem = arrayElemOf(g.effectiveType(c));
        CppInfo rc;
        if (!arrayElementInfo(resultElem, rc)) {
            g.reportArrayElementType(resultElem, c,
                                     "the result of flatMap()");
            return "0.0";
        }
        const std::string call =
            cb != nullptr ? g.emitArrow(*cb, callbackContext) : g.expr(*fn);
        return "::cp3::runtime::arrayFlatMap<" + rc.cpp + ">(" + recvText +
               ", " + call + ")";
    }
    if (prop == "forEach") {
        const std::string call =
            cb != nullptr ? g.emitArrow(*cb, callbackContext) : g.expr(*fn);
        return "::cp3::runtime::arrayForEach<" + ec.cpp + ">(" + recvText +
               ", " + call + ")";
    }
    if (prop == "reduce" || prop == "reduceRight") {
        // The accumulator parameter has the fold's result type: the initial
        // value's when given, the element's otherwise (a no-initial fold seeds
        // from the first element, JS-style). The accumulator is a plain value
        // (not an array element), so any type with a C++ spelling fits.
        const bool right = prop == "reduceRight";
        const CppInfo rc = classify(g.effectiveType(c));
        if (!rc.representable || rc.shape == Shape::Void) {
            g.reportTypeError(c, "type mismatch: the result of " + prop +
                                 "() has no C++ spelling");
            return "0.0";
        }
        const std::string fold =
            cb != nullptr
                ? g.emitArrow(*cb, {rc.cpp, ec.cpp, "double", arrayCpp})
                : g.expr(*fn);
        const std::string base = right ? "arrayReduceRight" : "arrayReduce";
        if (c.arguments.size() > 1 && c.arguments[1]) {
            return "::cp3::runtime::" + base + "<" + rc.cpp + ">(" + recvText +
                   ", " + fold + ", " + g.expr(*c.arguments[1]) + ")";
        }
        // A no-initial fold keeps the element type as the accumulator type
        // (JS seeds the accumulator with the first element), so a callback
        // that returns another type cannot be typed: the program is
        // ill-typed, not a missing feature (the rule TypeScript's `reduce`
        // overload follows).
        if (rc.cpp != ec.cpp) {
            g.reportTypeError(c, "type mismatch: a reduce without an initial "
                                 "value keeps the element type '" +
                                 describe(ec.type) + "' as the accumulator "
                                 "type, but the callback returns '" +
                                 describe(rc.type) + "'");
            return "0.0";
        }
        return "::cp3::runtime::" + base + "NoInit<" + rc.cpp + ">(" + recvText +
               ", " + fold + ")";
    }
    const char* fnName = nullptr;
    if (prop == "filter") fnName = "arrayFilter";
    else if (prop == "find") fnName = "arrayFind";
    else if (prop == "findLast") fnName = "arrayFindLast";
    else if (prop == "findIndex") fnName = "arrayFindIndex";
    else if (prop == "findLastIndex") fnName = "arrayFindLastIndex";
    else if (prop == "some") fnName = "arraySome";
    else if (prop == "every") fnName = "arrayEvery";
    if (fnName == nullptr) return std::string();
    const std::string call =
        cb != nullptr ? g.emitArrow(*cb, callbackContext) : g.expr(*fn);
    return "::cp3::runtime::" + std::string(fnName) + "<" + ec.cpp + ">(" +
           recvText + ", " + call + ")";
}

// The dispatch: returns "" when the member is not a regular-array method this
// unit lowers (the caller keeps its own lowering path).
std::string detail::ArrayEmitter::methodCall(Generator& g, const CallExpr& c,
                                             const MemberExpr& m) {
    if (!m.object) return std::string();
    static const char* const kCallbacks[] = {
        "map",     "flatMap", "filter", "reduce",    "reduceRight", "find",
        "findLast", "findIndex", "findLastIndex", "some", "every",
        "forEach"};
    static const char* const kSimple[] = {
        "push", "pop", "unshift", "shift", "unique", "shuffle",
        "indexOf", "lastIndexOf", "includes", "binarySearch", "slice", "flat",
        "at", "toSorted", "toReversed", "splice", "concat", "reverse", "keys",
        "values", "entries", "toSpliced", "with", "copyWithin"};
    const std::string& prop = m.property;
    if (prop == "reduce") {
        const CallExpr* filter = nullptr;
        const MemberExpr* filterMember = nullptr;
        const CallExpr* map = nullptr;
        const MemberExpr* mapMember = nullptr;
        const ArrowFunction* mapFn = nullptr;
        const ArrowFunction* filterFn = nullptr;
        const ArrowFunction* reduceFn = nullptr;
        if (pureMapFilterReduce(g.sema_, g.numericConstBindings_, c, m,
                                filter, filterMember, map,
                                mapMember, mapFn, filterFn, reduceFn)) {
            const Type sourceElem = arrayElemOf(g.effectiveType(*mapMember->object));
            const CppInfo sourceInfo = classify(sourceElem);
            const CppInfo mapInfo = classify(arrayElemOf(g.effectiveType(*map)));
            const CppInfo accInfo = classify(g.effectiveType(c));
            const std::string source = g.familyReceiverText(
                *mapMember->object, sourceElem, c);
            return "::cp3::runtime::arrayMapFilterReduce<" + accInfo.cpp + ">(" +
                   source + ", " + g.emitArrow(*mapFn, {sourceInfo.cpp}) +
                   ", " + g.emitArrow(*filterFn, {mapInfo.cpp}) + ", " +
                   g.emitArrow(*reduceFn, {accInfo.cpp, mapInfo.cpp}) + ", " +
                   g.expr(*c.arguments[1]) + ")";
        }
    }
    const bool callback = isOneOf(prop, kCallbacks);
    if (!callback && !isOneOf(prop, kSimple)) {
        return std::string();
    }
    const Type recv = g.effectiveType(*m.object);
    if (recv.kind != TypeKind::Array) return std::string();
    const Type elem = arrayElemOf(recv);
    CppInfo ec;
    if (!arrayElementInfo(elem, ec)) {
        // The receiver's element type has no C++ spelling: the method cannot
        // be lowered, and the diagnostic says why (the same one the array's
        // own declaration reports).
        g.reportArrayElementType(elem, c, "an array method");
        return "0.0";
    }
    g.needsTypedArray_ = true;
    const std::string recvText = g.familyReceiverText(*m.object, elem, c);
    if (callback) return callbackText(g, c, m, ec, recvText);

    auto arg = [&c, &g](std::size_t i) -> std::string {
        return i < c.arguments.size() && c.arguments[i]
                   ? g.expr(*c.arguments[i])
                   : std::string();
    };
    if (prop == "push" || prop == "unshift") {
        if (prop == "push" && g.isStackValueArray(*m.object) &&
            c.arguments.size() == 1 && c.arguments.front() &&
            c.arguments.front()->kind == NodeKind::NewExpr) {
            CppInfo valueInfo = g.info(*c.arguments.front());
            std::string valueType = valueInfo.cpp;
            constexpr const char* refPrefixes[] = {
                "::cp3::runtime::Ref<", "::cp3::runtime::LocalRef<"};
            std::size_t prefixSize = 0;
            std::string factoryName;
            for (std::size_t i = 0; i < 2; ++i) {
                const std::size_t size =
                    std::char_traits<char>::length(refPrefixes[i]);
                if (valueType.rfind(refPrefixes[i], 0) == 0) {
                    prefixSize = size;
                    factoryName = i == 0 ? "makeRef" : "makeLocalRef";
                    break;
                }
            }
            if (valueInfo.shape == Shape::ClassRef && valueInfo.representable &&
                prefixSize != 0 &&
                valueType.size() > prefixSize && valueType.back() == '>') {
                valueType = valueType.substr(
                    prefixSize, valueType.size() - prefixSize - 1);
                const std::string refText = g.expr(*c.arguments.front());
                const std::string exactFactory =
                    "::cp3::runtime::" + factoryName + "<" + valueType + ">(";
                if (refText.rfind(exactFactory, 0) == 0 &&
                    refText.size() > exactFactory.size() && refText.back() == ')') {
                    const std::string valueText =
                        valueType + "(" +
                        refText.substr(exactFactory.size(),
                                       refText.size() - exactFactory.size() - 1) +
                        ")";
                    return "::cp3::runtime::arrayPush<" + valueType + ">(" +
                           recvText + ", " + valueText + ")";
                }
            }
        }
        const char* fn = prop == "push" ? "arrayPush" : "arrayUnshift";
        return "::cp3::runtime::" + std::string(fn) + "<" + ec.cpp + ">(" +
               recvText + ", " + arg(0) + ")";
    }
    if (prop == "pop" || prop == "shift") {
        const char* fn = prop == "pop" ? "arrayPop" : "arrayShift";
        return "::cp3::runtime::" + std::string(fn) + "<" + ec.cpp + ">(" +
               recvText + ")";
    }
    if (prop == "unique" || prop == "shuffle") {
        const char* fn = prop == "unique" ? "arrayUnique" : "arrayShuffle";
        return "::cp3::runtime::" + std::string(fn) + "<" + ec.cpp + ">(" +
               recvText + ")";
    }
    if (prop == "indexOf" || prop == "includes" || prop == "binarySearch" ||
        prop == "lastIndexOf") {
        const char* fn = prop == "indexOf"   ? "arrayIndexOf"
                         : prop == "includes" ? "arrayIncludes"
                         : prop == "lastIndexOf" ? "arrayLastIndexOf"
                                                 : "arrayBinarySearch";
        std::string out = "::cp3::runtime::" + std::string(fn) + "<" + ec.cpp +
                          ">(" + recvText + ", " + arg(0);
        if (c.arguments.size() > 1) out += ", " + arg(1);
        return out + ")";
    }
    if (prop == "splice") {
        if (c.arguments.size() < 2) {
            g.reportCodegenError(c, diagcode::kArityMismatch,
                                 "arity mismatch: splice() takes at least 2 "
                                 "arguments (start, deleteCount)");
            return "0.0";
        }
        std::string items = "std::vector<" + ec.cpp + ">{";
        for (std::size_t i = 2; i < c.arguments.size(); ++i) {
            if (i > 2) items += ", ";
            items += arg(i);
        }
        items += "}";
        return "::cp3::runtime::arraySplice<" + ec.cpp + ">(" + recvText +
               ", " + arg(0) + ", " + arg(1) + ", " + items + ")";
    }
    if (prop == "toSpliced") {
        if (c.arguments.size() < 2) {
            g.reportCodegenError(c, diagcode::kArityMismatch,
                                 "arity mismatch: toSpliced() takes at least 2 "
                                 "arguments (start, deleteCount)");
            return "0.0";
        }
        std::string items = "std::vector<" + ec.cpp + ">{";
        for (std::size_t i = 2; i < c.arguments.size(); ++i) {
            if (i > 2) items += ", ";
            items += arg(i);
        }
        items += "}";
        return "::cp3::runtime::arrayToSpliced<" + ec.cpp + ">(" + recvText +
               ", " + arg(0) + ", " + arg(1) + ", " + items + ")";
    }
    if (prop == "with") {
        return "::cp3::runtime::arrayWith<" + ec.cpp + ">(" + recvText + ", " +
               arg(0) + ", " + arg(1) + ")";
    }
    if (prop == "copyWithin") {
        std::string out = "::cp3::runtime::arrayCopyWithin<" + ec.cpp + ">(" +
                          recvText + ", " + arg(0) + ", " + arg(1);
        if (c.arguments.size() > 2) out += ", " + arg(2);
        return out + ")";
    }
    if (prop == "concat") {
        std::string rest = "std::vector<" + ec.cpp + ">{";
        for (std::size_t i = 0; i < c.arguments.size(); ++i) {
            if (i > 0) rest += ", ";
            rest += arg(i);
        }
        rest += "}";
        return "::cp3::runtime::arrayConcat<" + ec.cpp + ">(" + recvText +
               ", " + rest + ")";
    }
    if (prop == "reverse") {
        return "::cp3::runtime::arrayReverse<" + ec.cpp + ">(" + recvText + ")";
    }
    if (prop == "keys") {
        return "::cp3::runtime::arrayKeys<" + ec.cpp + ">(" + recvText + ")";
    }
    if (prop == "values") {
        return "::cp3::runtime::arrayValues<" + ec.cpp + ">(" + recvText + ")";
    }
    if (prop == "entries") {
        g.needsTuple_ = true;
        return "::cp3::runtime::arrayEntries<" + ec.cpp + ">(" + recvText + ")";
    }
    if (prop == "slice") {
        std::string out =
            "::cp3::runtime::arraySlice<" + ec.cpp + ">(" + recvText;
        for (const auto& a : c.arguments) {
            out += ", ";
            out += a ? g.expr(*a) : "0.0";
        }
        return out + ")";
    }
    if (prop == "at") {
        return "::cp3::runtime::arrayElementAt<" + ec.cpp + ">(" + recvText +
               ", " + arg(0) + ")";
    }
    if (prop == "toReversed") {
        return "::cp3::runtime::arrayToReversed<" + ec.cpp + ">(" + recvText +
               ")";
    }
    if (prop == "toSorted") {
        if (c.arguments.empty()) {
            return "::cp3::runtime::arrayToSorted<" + ec.cpp + ">(" + recvText +
                   ")";
        }
        const Expr* comparator =
            c.arguments.front() ? c.arguments.front().get() : nullptr;
        if (comparator == nullptr) {
            g.reportCodegenError(c, diagcode::kTypeMismatch,
                                 "type mismatch: the toSorted comparator must "
                                 "be a function");
            return "0.0";
        }
        const std::string cmp =
            comparator->kind == NodeKind::ArrowFunction
                ? g.emitArrow(static_cast<const ArrowFunction&>(*comparator),
                              {ec.cpp, ec.cpp})
                : g.expr(*comparator);
        return "::cp3::runtime::arrayToSortedWith<" + ec.cpp + ">(" + recvText +
               ", " + cmp + ")";
    }
    // `flat(depth?)`: one level by default (JS); a literal depth peels that
    // many levels by nesting `arrayFlat` calls. An array whose elements are
    // not arrays has nothing to flatten, and JS returns a copy of the
    // receiver.
    if (prop == "flat") {
        int depth = 1;
        if (!c.arguments.empty() && c.arguments.front() &&
            c.arguments.front()->kind == NodeKind::NumberLiteral) {
            depth = static_cast<int>(
                static_cast<NumberLiteral&>(*c.arguments.front()).value);
        }
        if (depth < 0) depth = 0;
        if (depth == 0 || elem.kind != TypeKind::Array) {
            return "::cp3::runtime::arrayFromArray<" + ec.cpp + ">(" + recvText +
                   ")";
        }
        // Peel `depth` layers: each `arrayFlat` drops one.
        std::string text = recvText;
        Type inner = elem;
        for (int i = 0; i < depth && inner.kind == TypeKind::Array; ++i) {
            CppInfo ic;
            if (!arrayElementInfo(arrayElemOf(inner), ic)) {
                g.reportArrayElementType(arrayElemOf(inner), c,
                                         "a flattened array");
                return "0.0";
            }
            text = "::cp3::runtime::arrayFlat<" + ic.cpp + ">(" + text + ")";
            inner = arrayElemOf(inner);
        }
        return text;
    }
}

// The `for...of` source: the element type the loop binds and the C++ type of
// one element. False when the source is not one cp3 can walk - an array whose
// element type has no C++ spelling, or a source with no iterator surface (the
// JSC proposal defers the iterator protocol, so only arrays and strings are
// walkable today).
bool detail::ArrayEmitter::forOfSource(Generator& g, const ForOfStatement& f,
                                       CppInfo& ec, bool& isString) {
    const Type source = f.right ? g.effectiveType(*f.right) : Type::unknown();
    isString = source.kind == TypeKind::String;
    if (isString) {
        // A string source walks its UTF-8 code points (the JS for-of order;
        // docs 09: cp3 strings are UTF-8, so the element is a whole
        // character, not a byte).
        ec = classify(Type::string());
        return true;
    }
    if (source.kind == TypeKind::Array) {
        const Type elem = arrayElemOf(source);
        if (arrayElementInfo(elem, ec)) return true;
        // cp3 has no dynamic array, so an element type with no C++ spelling
        // is a type error, not a missing feature.
        g.reportTypeError(f, "type mismatch: an array cannot hold elements of "
                             "type '" + describe(elem) + "'");
        return false;
    }
    g.reportTypeError(f, "type mismatch: for-of needs an array or a string");
    return false;
}

// The assignment a `for...of` loop makes to a target that is not a binding: a
// member field or an array element (`for (box.value of a)`,
// `for (out[0] of a)`). The ordinary assignment lowering owns the spelling,
// so the loop reuses it with the loop's element as the assigned value.
std::string detail::ArrayEmitter::forOfTargetAssign(Generator& g,
                                                    const Node& target,
                                                    const std::string& item) {
    // The value the assignment stores: the loop's element, spelled as the
    // C++ local the range-for binds.
    AssignmentExpr assign(target.span);
    assign.op = AssignOp::Assign;
    assign.value = std::make_unique<Identifier>(target.span, item);
    if (target.kind == NodeKind::MemberExpr) {
        const auto& member = static_cast<const MemberExpr&>(target);
        // A class/interface field write, then a structural object's field
        // write - the same order the ordinary assignment uses.
        if (std::string w = g.emitModelAssign(assign, member); !w.empty()) {
            return w;
        }
        return g.emitMemberAssign(assign, member);
    }
    if (target.kind == NodeKind::IndexExpr) {
        return g.familyIndexWriteText(static_cast<const IndexExpr&>(target),
                                      item);
    }
    return std::string();
}

// `for (const x of a)`: over a regular array or a string; the binding takes
// the element type. The source handle is bound to a named local before the
// range-for: iterating `*(temporary Ref)` lets the backing vector die at the
// end of the range-init full-webserverion (a use-after-free seen as
// std::bad_alloc).
//
// `awaitValues` is the `for await` form: every bound value is awaited, so
// the binding takes the AWAITED value (a promise resolves to its value, D41)
// and the await needs a coroutine to suspend in.
void detail::ArrayEmitter::forOfImpl(Generator& g, const ForOfStatement& f,
                                     const std::string& jsLabel,
                                     std::string& out, int indent,
                                     bool awaitValues) {
    if (g.intOn()) g.intFail();
    const auto* vd = f.left && f.left->kind == NodeKind::VariableDeclaration
                         ? static_cast<const VariableDeclaration*>(f.left.get())
                         : nullptr;
    CppInfo ec;
    bool isString = false;
    if (!forOfSource(g, f, ec, isString)) {
        line(out, indent, "/* CP3401: the for-of source is not walkable */");
        return;
    }
    const Type elem = ec.type;
    // The awaited value: a promise resolves to its value, anything else is its
    // own value (D41).
    Type bound = elem;
    if (awaitValues && isPromiseType(elem)) bound = promiseValue(elem);
    const CppInfo bc = classify(bound);
    if (awaitValues && !bc.representable) {
        g.reportTypeError(f, "type mismatch: the awaited value of for await "
                             "has no C++ spelling");
        line(out, indent, "/* for await: unsupported element type */");
        return;
    }
    // The await suspends, so it needs a coroutine (an `async function` body
    // or the top level).
    if (awaitValues && !g.awaitContextOk(f)) {
        line(out, indent, "/* for await: no coroutine to suspend in */");
        return;
    }
    if (!isString) g.needsTypedArray_ = true;
    const std::string recv = g.familyReceiverText(*f.right, elem, f);
    const std::string item = "cp3_item" + std::to_string(g.uniqueCounter_++);
    const std::string range = "cp3_range" + std::to_string(g.uniqueCounter_++);
    const std::string breakLabel = g.freshLabel("forof_end_");
    const std::string continueLabel = g.freshLabel("forof_next_");
    // The range webserverion can itself be a temporary array handle (an array
    // literal builds a fresh `makeArray(...)`): keep the handle in the C++20
    // init-statement so its storage outlives the loop. Iterating the handle
    // directly would read freed storage under `/std:c++20` (the P2718R0
    // lifetime extension is C++23 only).
    g.pushFlowFrame(true, breakLabel, continueLabel, jsLabel);
    g.pushScope();
    // A string walks its code points; an array its elements (through the
    // handle's vector).
    const std::string rangeInit =
        isString ? "auto " + range + " = ::cp3::runtime::strings::codePoints(" +
                       recv + ")"
                 : "auto " + range + " = " + recv;
    if (isString) g.needsString_ = true;
    line(out, indent,
         "for (" + rangeInit + "; " + ec.cpp + " " + item + " : " +
             (isString ? range : "*" + range) + ") {");
    // The bound value: the element itself, or its awaited form.
    const std::string valueText =
        awaitValues ? "(co_await ::cp3::runtime::awaitValue(" + item + "))"
                    : item;
    if (vd != nullptr) {
        const std::string cv = (vd->kind == VarKind::Const) ? "const " : "";
        for (const auto& declPtr : vd->declarations) {
            const VariableDeclarator* d = declPtr.get();
            if (d == nullptr || !d->id) continue;
            if (d->id->kind == NodeKind::Identifier) {
                const std::string name =
                    mapIdent(static_cast<const Identifier&>(*d->id).name);
                std::optional<Type> bt = g.bindingType(*d);
                if (!bt) bt = bound;
                const CppInfo ci = classify(*bt);
                line(out, indent + 1,
                     cv + (ci.representable ? ci.cpp : bc.cpp) + " " + name +
                         " = " + valueText + ";");
                g.declare(static_cast<const Identifier&>(*d->id).name, bt);
            } else {
                std::optional<Type> bt = g.resolvedBindingType(*d);
                if (!bt) bt = bound;
                PatternEmitter(g).emitBindings(*d->id, valueText, bt, out,
                                               indent + 1);
            }
        }
    } else if (f.left && isPatternNode(*f.left)) {
        PatternEmitter(g).emitBindings(*f.left, valueText, bound, out, indent + 1);
    } else {
        // A target that is not a binding: the loop assigns the element to it
        // (a field or an element); any other target is ill-typed.
        const std::string assign =
            f.left ? forOfTargetAssign(g, *f.left, valueText) : std::string();
        if (assign.empty()) {
            g.reportTypeError(f, "type mismatch: the for-of target must be a "
                                 "binding, a field or an array element");
            line(out, indent + 1,
                 "/* CP3401: the for-of target is not an assignment target */");
        } else {
            line(out, indent + 1, assign + ";");
        }
    }
    if (f.body) g.emitBranch(*f.body, out, indent + 1);
    line(out, indent + 1, continueLabel + ": ;");
    line(out, indent, "}");
    g.popScope();
    g.popFlowFrame();
    line(out, indent, breakLabel + ": ;");
}

void detail::ArrayEmitter::forOf(Generator& g, const ForOfStatement& f,
                                 const std::string& jsLabel, std::string& out,
                                 int indent) {
    forOfImpl(g, f, jsLabel, out, indent, /*awaitValues=*/false);
}

// `for await (const x of a)`: the same loop, with every bound value awaited.
// The await needs a coroutine to suspend in, so the form is only legal inside
// an `async function` or at the top level.
void detail::ArrayEmitter::forAwait(Generator& g, const ForOfStatement& f,
                                    const std::string& jsLabel, std::string& out,
                                    int indent) {
    forOfImpl(g, f, jsLabel, out, indent, /*awaitValues=*/true);
}

}  // namespace cp3
