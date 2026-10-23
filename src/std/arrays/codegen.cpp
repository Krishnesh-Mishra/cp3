// =============================================================================
// src/std/arrays/codegen.cpp - the regular array's value forms.
//
// The `T[]` value: the receiver an array method takes (the array itself, or
// the array a constructor call or a literal builds) and the declarator
// initializers that lower specially (`let a: number[] = Array(3)`). The
// typed views, the buffer classes and DataView live in the sibling
// typed-arrays family (src/std/typed-arrays/).
//
// Every form the corpus does not pin reports CP3002 through `notImplemented`
// and returns a compilable stub.
// =============================================================================
#include "detail/generator/generator.h"

#include <cstddef>
#include <string>
#include <vector>

#include "detail/generator/family.h"
#include "detail/generator/type_map.h"
#include "cp3/host/native_types.hpp"

namespace cp3 {

using namespace detail;

// The receiver an array method takes: the array itself, or the array a
// constructor call or a literal builds.
std::string Generator::familyReceiverText(const Expr& obj, const Type& elem,
                                          const Node& site) {
    if (const ExprList* args = arrayCtorArgs(obj)) {
        return arrayCtorText(*args, elem, site);
    }
    if (obj.kind == NodeKind::ArrayLiteral) {
        return arrayLiteralText(static_cast<const ArrayLiteral&>(obj), elem);
    }
    return expr(obj);
}

// The initializer of a declaration that lowers specially: `let a: number[] =
// Array(3)` (element from the annotation) and `let a: number[] = []` (the
// annotation fixes an otherwise unknowable element type). "" means the caller
// lowers the initializer normally.
std::string Generator::declaratorInitText(const VariableDeclarator& d,
                                          const Type& declared) {
    // `let t: [string, number] = ["a", 1]`: the tuple annotation fixes the
    // element types, so brace-initialize the std::tuple directly instead of
    // lowering the literal as an Array (spec 03).
    if (std::string tuple = tupleDeclaratorInitText(d, declared);
        !tuple.empty()) {
        return tuple;
    }
    if (!d.init || declared.kind != TypeKind::Array) return std::string();
    if (const ExprList* args = arrayCtorArgs(*d.init)) {
        return arrayCtorText(*args, arrayElemOf(declared), *d.init);
    }
    if (d.init->kind == NodeKind::ArrayLiteral) {
        // The annotation's element type wins when it has a spelling: a class /
        // interface element (`let s: Shape[] = [new Sq()]`) must lower to the
        // annotated handle type, not the literal's inferred union. A native
        // element annotation and an otherwise unknowable element type take the
        // same path.
        const Type elem = arrayElemOf(declared);
        CppInfo dc;
        if (arrayElementInfo(elem, dc) || isNativeType(elem) ||
            arrayElemOf(effectiveType(*d.init)).isUnknown()) {
            return arrayLiteralText(static_cast<const ArrayLiteral&>(*d.init),
                                    elem);
        }
        return std::string();
    }
    return std::string();
}

}  // namespace cp3
