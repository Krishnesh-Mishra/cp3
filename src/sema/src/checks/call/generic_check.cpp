// =============================================================================
// src/sema/src/checks/generic_check.cpp - generic function call checking
// (spec §16).
//
// Owns the generic-function half of call checking: substituting a call's type
// arguments (explicit `<...>` and inferred from the argument types) into the
// callee's signature, checking explicit type arguments against the declared
// `extends` constraints, and the structural parameter/argument unification that
// binds a type parameter to an argument's type. The ordinary arity and
// assignability pass (`checkArgs`) then runs on the substituted signature, so
// generic calls obey exactly the same rules as ordinary ones.
//
// A type parameter is represented as a `TypeKind::TypeParam` whose `parts[0]`
// (when present) is its constraint; `hoistFunction` (support/scopes.cpp)
// registers the spelling of a named constraint so diagnostics can print it.
// =============================================================================
#include "detail/analyzer.h"

#include <cstddef>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "cp3/diag/diagnostic_codes.hpp"

namespace cp3 {

namespace {

// True when an argument type carries no information a type parameter could
// bind to (`unknown` / ambient / error): such an argument never binds `T`, so
// the call stays permissive instead of locking T to a meaningless type.
bool isPermissiveBinding(const Type& t) {
    return t.isUnknown() || t.isError() || isAmbientType(t);
}

}  // namespace

// The type a non-concrete receiver probes as: a literal binds to its widened
// base type, and a type parameter resolves through its `extends` constraint
// (`T extends Shape` makes `p.area` / `a[0]` legal). An unconstrained type
// parameter stays ambient (permissive), so its body does not cascade.
Type probeBase(const Type& t) {
    if (t.kind == TypeKind::Literal) return t.literalBaseType();
    if (t.kind == TypeKind::TypeParam && !t.parts.empty() &&
        !t.parts.front().isUnknown()) {
        return t.parts.front();
    }
    return ambient();
}

// Replaces every type-parameter leaf by its binding; unbound parameters stay
// in place (downstream checks treat a bare TypeParam permissively).
Type Analyzer::substituteType(const Type& t,
                              const std::unordered_map<std::string, Type>& subst) {
    if (t.kind == TypeKind::TypeParam) {
        auto it = subst.find(t.name);
        if (it != subst.end()) return it->second;
        return t;
    }
    switch (t.kind) {
        case TypeKind::Array: {
            Type out = t;
            out.parts.clear();
            out.parts.push_back(substituteType(elemOf(t), subst));
            return out;
        }
        case TypeKind::Class:
        case TypeKind::Function: {
            if (t.parts.empty()) return t;
            Type out = t;
            out.parts.clear();
            for (const Type& p : t.parts) {
                out.parts.push_back(substituteType(p, subst));
            }
            return out;
        }
        case TypeKind::Object: {
            Shape* sh = shapeOf(t);
            if (!sh) return t;
            if (sh->tuple) {
                std::vector<Type> elems;
                elems.reserve(sh->tupleElems.size());
                for (const Type& e : sh->tupleElems) {
                    elems.push_back(substituteType(e, subst));
                }
                return makeObjectType({}, false, true, std::move(elems));
            }
            if (sh->fields.empty() || sh->open) return t;
            std::vector<Field> fields;
            fields.reserve(sh->fields.size());
            for (const Field& f : sh->fields) {
                fields.push_back(Field{f.name, substituteType(f.type, subst),
                                       f.optional});
            }
            return makeObjectType(std::move(fields), false, false, {});
        }
        default:
            return t;
    }
}

// Binds the type parameters of `param` from the argument type `arg`. The walk
// mirrors the structural types a signature can nest: arrays, tuples, class
// type arguments and function types. It never fails on its own; an argument
// that does not really match surfaces through the substituted check.
void Analyzer::unifyParam(const Type& param, const Type& arg,
                          std::unordered_map<std::string, Type>& subst) {
    if (param.kind == TypeKind::TypeParam) {
        if (isPermissiveBinding(arg)) return;
        if (subst.find(param.name) == subst.end()) {
            subst[param.name] = widen(arg);
        }
        return;
    }
    if (param.kind == TypeKind::Array) {
        if (arg.kind == TypeKind::Array) {
            unifyParam(elemOf(param), elemOf(arg), subst);
            return;
        }
        // A tuple argument (`[1, "a"]` for `(A | B)[]`, say) still carries its
        // element types.
        if (isTupleType(arg)) {
            if (Shape* sh = shapeOf(arg)) {
                for (const Type& e : sh->tupleElems) {
                    unifyParam(elemOf(param), e, subst);
                }
            }
        }
        return;
    }
    if (param.kind == TypeKind::Class && arg.kind == TypeKind::Class &&
        param.name == arg.name) {
        const std::size_t n = param.parts.size() < arg.parts.size()
                                  ? param.parts.size()
                                  : arg.parts.size();
        for (std::size_t i = 0; i < n; ++i) {
            unifyParam(param.parts[i], arg.parts[i], subst);
        }
        return;
    }
    if (param.kind == TypeKind::Object && isTupleType(param) &&
        isTupleType(arg)) {
        Shape* psh = shapeOf(param);
        Shape* ash = shapeOf(arg);
        if (!psh || !ash) return;
        const std::size_t n = psh->tupleElems.size() < ash->tupleElems.size()
                                  ? psh->tupleElems.size()
                                  : ash->tupleElems.size();
        for (std::size_t i = 0; i < n; ++i) {
            unifyParam(psh->tupleElems[i], ash->tupleElems[i], subst);
        }
        return;
    }
    if (param.kind == TypeKind::Function && arg.kind == TypeKind::Function) {
        const std::size_t n = param.parts.size() < arg.parts.size()
                                  ? param.parts.size()
                                  : arg.parts.size();
        for (std::size_t i = 0; i < n; ++i) {
            unifyParam(param.parts[i], arg.parts[i], subst);
        }
    }
}

// A call to a generic function (spec §16). An explicit `<number>` type argument
// is checked against the declared `<T extends C>` constraint; inferred bindings
// are formed by structural unification and trusted. The substituted signature
// then runs the ordinary checkArgs pass, so arity and argument assignability
// behave exactly like a non-generic call.
Type Analyzer::inferGenericCall(CallExpr& call, const FuncSig& sig) {
    std::unordered_map<std::string, Type> subst;

    for (std::size_t i = 0;
         i < call.typeArgs.size() && i < sig.typeParams.size(); ++i) {
        TypeNode* ta = call.typeArgs[i].get();
        if (!ta) continue;
        const Type resolved = resolveType(*ta);
        const Type& tp = sig.typeParams[i];
        if (!tp.parts.empty()) {
            const Type& constraint = tp.parts.front();
            if (!constraint.isUnknown() && !isAmbientType(constraint) &&
                !wouldAccept(resolved, constraint, false)) {
                std::string cname = pretty(constraint);
                auto spelled = typeParamConstraintNames_.find(typeKey(constraint));
                if (spelled != typeParamConstraintNames_.end()) {
                    cname = spelled->second;
                }
                report(diagcode::kTypeMismatch, call.span,
                       "type mismatch: type argument '" + pretty(resolved) +
                           "' does not satisfy the constraint '" + cname + "'");
            }
        }
        subst[tp.name] = resolved;
    }

    // A parameter left unbound by an out-of-line type argument stays a bare
    // TypeParam and every check below treats it permissively.
    for (std::size_t i = 0; i < call.arguments.size(); ++i) {
        Expr* a = call.arguments[i].get();
        if (!a || a->kind == NodeKind::SpreadElement) continue;
        if (i < sig.params.size()) {
            unifyParam(sig.params[i], inferExpr(*a), subst);
        } else if (sig.rest && !sig.params.empty()) {
            unifyParam(sig.params.back(), inferExpr(*a), subst);
        }
    }

    FuncSig sub = sig;
    for (Type& p : sub.params) p = substituteType(p, subst);
    sub.ret = substituteType(sig.ret, subst);
    checkArgs(sub, call);
    return withType(call, sub.ret);
}

// A type argument list on a callee that declares no type parameters is a type
// error: there is nothing to substitute (the generic call's own arguments
// are inferred, and a generic callee takes them). A member call is left to
// its own family (`JSON.parse<T>`, `res.json<T>()`, `env.get<T>()`).
void Analyzer::checkTypeArgs(const FuncSig& sig, CallExpr& call) {
    if (call.typeArgs.empty() || !sig.typeParams.empty()) return;
    if (!call.callee || call.callee->kind != NodeKind::Identifier) return;
    const std::string callee =
        "'" + static_cast<Identifier&>(*call.callee).name + "'";
    report(diagcode::kTypeArgsOnNonGeneric, call.span,
           "type arguments are not allowed here: " + callee +
               " is not a generic function");
}

}  // namespace cp3
