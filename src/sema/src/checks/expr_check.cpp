// =============================================================================
// src/sema/src/checks/expr_check.cpp - primary expression inference.
//
// Owns `inferExpr` (the expression dispatch), the literal/identifier/template/
// array/object cases, arrow-function inference and the `new` / `await` /
// range / weak / spread cases, plus `typeOfExpr` and `dynamicExpr`. Member,
// index and call inference live in member_check.cpp; operators in ops_check.cpp
// (STYLE.md §8).
// =============================================================================
#include "detail/analyzer.h"

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "cp3/diagnostic_codes.hpp"

namespace cp3 {

Type Analyzer::typeOfExpr(Expr& e) {
    if (e.nodeId != 0) {
        const auto it = out_.nodeTypes.find(e.nodeId);
        if (it != out_.nodeTypes.end()) return it->second;
    }
    return Type::unknown();
}

Type Analyzer::inferExpr(Expr& e) {
    switch (e.kind) {
        case NodeKind::NumberLiteral: {
            auto& n = static_cast<NumberLiteral&>(e);
            return withType(n, Type::numberLiteral(n.value));
        }
        case NodeKind::StringLiteral: {
            auto& s = static_cast<StringLiteral&>(e);
            return withType(s, Type::stringLiteral(s.value));
        }
        case NodeKind::BoolLiteral: {
            auto& b = static_cast<BoolLiteral&>(e);
            return withType(b, Type::boolLiteral(b.value));
        }
        case NodeKind::NullLiteral:
            return withType(e, Type::nullType());
        case NodeKind::UndefinedLiteral:
            return withType(e, Type::undefined());
        case NodeKind::TemplateLiteral:
            return inferTemplate(static_cast<TemplateLiteral&>(e));
        case NodeKind::RegExpLiteral:
            // Core regex literal (D48): the nominal RegExp class; the member
            // surface lives in checks/regex_check.cpp.
            return withType(e, regexLiteralType());
        case NodeKind::Identifier:
            return inferIdentifier(static_cast<Identifier&>(e));
        case NodeKind::ThisExpr:
            return withType(e, thisType());
        case NodeKind::ArrayLiteral:
            return inferArray(static_cast<ArrayLiteral&>(e));
        case NodeKind::ObjectLiteral:
            return inferObject(static_cast<ObjectLiteral&>(e));
        case NodeKind::MemberExpr:
            return inferMember(static_cast<MemberExpr&>(e));
        case NodeKind::IndexExpr:
            return inferIndex(static_cast<IndexExpr&>(e));
        case NodeKind::CallExpr:
            return inferCallOrFamily(static_cast<CallExpr&>(e));
        case NodeKind::NewExpr:
            return inferNew(static_cast<NewExpr&>(e));
        case NodeKind::UnaryExpr:
            return inferUnary(static_cast<UnaryExpr&>(e));
        case NodeKind::UpdateExpr:
            return inferUpdate(static_cast<UpdateExpr&>(e));
        case NodeKind::BinaryExpr:
            return inferBinary(static_cast<BinaryExpr&>(e));
        case NodeKind::LogicalExpr:
            return inferLogical(static_cast<LogicalExpr&>(e));
        case NodeKind::ConditionalExpr:
            return inferConditional(static_cast<ConditionalExpr&>(e));
        case NodeKind::AssignmentExpr:
            return inferAssignment(static_cast<AssignmentExpr&>(e));
        case NodeKind::ArrowFunction:
            return withType(e, inferArrow(static_cast<ArrowFunction&>(e)));
        case NodeKind::AwaitExpr:
            return inferAwait(static_cast<AwaitExpr&>(e));
        case NodeKind::RangeExpr:
            return inferRange(static_cast<RangeExpr&>(e));
        case NodeKind::WeakExpr:
            return inferWeak(static_cast<WeakExpr&>(e));
        case NodeKind::SpreadElement:
            return inferSpread(static_cast<SpreadElement&>(e));
        default:
            return withType(e, ambient());
    }
}

Type Analyzer::inferTemplate(TemplateLiteral& tl) {
    for (auto& q : tl.quasis) {
        if (q) setType(*q, Type::string());
    }
    for (auto& ex : tl.expressions) {
        if (ex) inferExpr(*ex);
    }
    return withType(tl, Type::string());
}

Type Analyzer::inferIdentifier(Identifier& id) {
    if (id.name == "arguments") {
        reportBannedArguments(id.span);
        return withType(id, ambient());
    }
    // D22: `super` names the base instance inside a derived class body.
    if (id.name == "super") {
        if (!classStack_.empty()) {
            const ClassInfo* ci = findClass(classStack_.back());
            if (ci != nullptr && !ci->baseName.empty()) {
                return withType(id,
                                Type::named(TypeKind::Class, ci->baseName));
            }
        }
        reportUndefinedName(id.span, id.name);
        return withType(id, Type::error());
    }
    Binding b;
    if (!lookup(id.name, b)) {
        reportUndefinedName(id.span, id.name);
        return withType(id, Type::error());
    }
    return withType(id, b.type);
}

Type Analyzer::inferArray(ArrayLiteral& al) {
    std::vector<Type> elems;
    for (auto& e : al.elements) {
        if (!e) continue;
        if (e->kind == NodeKind::SpreadElement) {
            auto& sp = static_cast<SpreadElement&>(*e);
            Type at = sp.argument ? inferExpr(*sp.argument) : Type::unknown();
            elems.push_back(at.kind == TypeKind::Array ? widen(elemOf(at))
                                                       : Type::unknown());
            continue;
        }
        elems.push_back(widen(inferExpr(*e)));
    }
    Type elem = Type::unknown();
    if (!elems.empty()) {
        elem = elems.front();
        for (std::size_t i = 1; i < elems.size(); ++i) {
            elem = unify(elem, elems[i]);
        }
    }
    return withType(al, Type::array(elem));
}

Type Analyzer::inferObject(ObjectLiteral& ol) {
    std::vector<Field> fields;
    bool open = false;
    for (auto& p : ol.properties) {
        if (!p) continue;
        if (p->value && p->value->kind == NodeKind::SpreadElement) {
            // `{ ...src, b: 2 }`: a spread of a closed object shape contributes
            // its fields (earlier fields win, JS order); any other source keeps
            // the result open.
            const Type st = inferExpr(*p->value);
            const Shape* shape = shapeOf(st);
            if (st.isUnknown() || shape == nullptr || shape->open ||
                shape->tuple) {
                open = true;
                continue;
            }
            for (const Field& f : shape->fields) {
                if (!hasFieldNamed(fields, f.name)) fields.push_back(f);
            }
            continue;
        }
        std::string name;
        Type vt = Type::unknown();
        if (p->key && p->key->kind == NodeKind::Identifier && !p->computed) {
            name = static_cast<Identifier&>(*p->key).name;
        } else if (p->key && p->key->kind == NodeKind::StringLiteral) {
            name = static_cast<StringLiteral&>(*p->key).value;
        } else if (p->key && p->key->kind == NodeKind::NumberLiteral) {
            name = static_cast<NumberLiteral&>(*p->key).raw;
        } else {
            open = true;
        }
        if (p->value) {
            vt = widen(inferExpr(*p->value));
        } else if (!name.empty()) {
            // D13: shorthand `{ a }` behaves as `{ a: a }`.
            Binding b;
            if (lookup(name, b)) {
                vt = widen(b.type);
            } else {
                reportUndefinedName(p->span, name);
            }
        }
        if (!name.empty() && !hasFieldNamed(fields, name)) {
            fields.push_back(Field{name, vt, false});
        }
    }
    return withType(ol, makeObjectType(std::move(fields), open, false, {}));
}

Type Analyzer::inferNew(NewExpr& n) {
    // The JS-compat family constructors (`new Int8Array(...)`, `new Array(2)`,
    // `new DataView(buf)`): nominal types from the family grammar
    // (src/std/typed-arrays/sema.cpp). A name the program shadowed is not
    // ambient, so user declarations keep the generic path below.
    if (n.callee && n.callee->kind == NodeKind::Identifier) {
        auto& id = static_cast<Identifier&>(*n.callee);
        Binding b;
        // A named std import may be bound under another spelling
        // (`import { Map as M } from "std/collections"`); the construct tables
        // key on the exported name, so resolve the alias first.
        const std::string ctor = importedName(id.name);
        // `new EventEmitter()` (D74, spec §14): a language builtin, always in
        // scope. The type is nominal; its members are typed in
        // member_check.cpp and codegen lowers the construction to the runtime.
        if (lookup(id.name, b) && isAmbientType(b.type) &&
            ctor == "EventEmitter") {
            setType(id, b.type);
            for (auto& a : n.arguments) {
                if (a) inferExpr(*a);
            }
            if (!n.arguments.empty()) {
                notImplemented(n.span, "new EventEmitter(...) with arguments");
            }
            return withType(n, Type::named(TypeKind::Class, "EventEmitter"));
        }
        if (lookup(id.name, b) && isAmbientType(b.type) &&
            isFamilyCtorName(ctor)) {
            setType(id, b.type);
            return withType(n, inferFamilyConstruct(ctor, n.arguments,
                                                    n.span));
        }
        if (lookup(id.name, b) && isAmbientType(b.type)) {
            // `new WebSocket(url)` / `new WebSocketServer({ port })`: an
            // ambient name the network table knows (src/std/network/sema.cpp).
            setType(id, b.type);
            Type net = inferNetworkConstruct(ctor, n.arguments, n.span);
            if (!net.isUnknown()) return withType(n, net);
        }
        if (lookup(id.name, b) && isAmbientType(b.type)) {
            // `new Map<string, number>()` / `new Set<number>()`: the
            // std/collections family (src/std/collections/sema.cpp).
            setType(id, b.type);
            Type coll = inferCollectionsConstruct(ctor, n.typeArgs);
            if (!coll.isUnknown()) return withType(n, coll);
        }
        // User classes (spec 15): `new C(args)` checks the constructor and
        // yields the nominal instance type; abstract classes are CP3413
        // (class_conform_check.cpp).
        if (lookup(id.name, b) && b.type.kind == TypeKind::Class &&
            findClass(id.name)) {
            return withType(n, inferUserConstruct(id, b, n));
        }
    }
    if (n.callee) inferExpr(*n.callee);
    for (auto& a : n.arguments) {
        if (a && a->kind == NodeKind::SpreadElement) {
            auto& sp = static_cast<SpreadElement&>(*a);
            if (sp.argument) inferExpr(*sp.argument);
        } else if (a) {
            inferExpr(*a);
        }
    }
    return withType(n, ambient());
}

Type Analyzer::inferAwait(AwaitExpr& a) {
    // Only an `async` function (or an async arrow) and the top level may
    // await: a plain function or a non-async arrow has no coroutine to
    // suspend in.
    if (!funcStack_.empty() && !funcStack_.back().isAsync) {
        report(diagcode::kAwaitOutsideAsync, a.span,
               "'await' is only allowed in an async function or at the top "
               "level");
    }
    Type arg = a.argument ? inferExpr(*a.argument) : Type::unknown();
    if (arg.isError()) return withType(a, Type::error());
    // `Promise<T>` unwraps to T; DECISIONS D41: any other value yields itself.
    if (isPromiseType(arg)) return withType(a, promiseValue(arg));
    return withType(a, arg);
}

Type Analyzer::inferRange(RangeExpr& r) {
    if (r.start) inferExpr(*r.start);
    if (r.end) inferExpr(*r.end);
    return withType(r, Type::array(Type::number()));
}

Type Analyzer::inferWeak(WeakExpr& w) {
    if (w.value) inferExpr(*w.value);
    return withType(w, ambient());
}

Type Analyzer::inferSpread(SpreadElement& sp) {
    Type t = sp.argument ? inferExpr(*sp.argument) : Type::unknown();
    return withType(sp, t);
}

Expr* Analyzer::dynamicExpr(Node& n) {
    // Best-effort: treat the node as an expression if it is one.
    switch (n.kind) {
        case NodeKind::Identifier:
        case NodeKind::NumberLiteral:
        case NodeKind::StringLiteral:
        case NodeKind::BoolLiteral:
        case NodeKind::NullLiteral:
        case NodeKind::UndefinedLiteral:
        case NodeKind::CallExpr:
        case NodeKind::MemberExpr:
        case NodeKind::IndexExpr:
        case NodeKind::BinaryExpr:
        case NodeKind::LogicalExpr:
        case NodeKind::UnaryExpr:
        case NodeKind::UpdateExpr:
        case NodeKind::AssignmentExpr:
        case NodeKind::ConditionalExpr:
        case NodeKind::ArrayLiteral:
        case NodeKind::ObjectLiteral:
        case NodeKind::TemplateLiteral:
            return static_cast<Expr*>(&n);
        default:
            return nullptr;
    }
}

// =============================================================================
// Arrow functions
// =============================================================================
Type Analyzer::inferArrow(ArrowFunction& arrow) {
    if (!arrow.typeParams.empty()) notImplemented(arrow.span, "generic arrow functions");
    registerTypeParams(arrow.typeParams);

    std::vector<Type> params;
    std::vector<bool> optional;
    buildArrowParamTypes(arrow, params, optional);
    Type ret = inferArrowReturn(arrow, params);
    if (arrow.isAsync && !isPromiseType(ret)) {
        // spec §14 / D40: calling an async arrow yields a Promise.
        ret = promiseOf(ret);
    }
    Type ft = Type::function(params, ret);
    if (arrow.nodeId != 0) {
        FuncSig as;
        as.params = params;
        as.optional = optional;
        as.ret = ret;
        as.isAsync = arrow.isAsync;
        as.asyncValueRet = isPromiseType(ret) ? promiseValue(ret) : ret;
        as.declNodeId = arrow.nodeId;
        publishFunction(as, "");
        arrowSigs_[arrow.nodeId] = std::move(as);
    }
    return ft;
}

void Analyzer::buildArrowParamTypes(ArrowFunction& arrow,
                                    std::vector<Type>& params,
                                    std::vector<bool>& optional) {
    for (auto& p : arrow.params) {
        if (!p) {
            params.push_back(Type::unknown());
            optional.push_back(false);
            continue;
        }
        Type pt = ambient();
        if (p->type) {
            pt = resolveType(*p->type);
        } else if (p->defaultValue) {
            pt = widen(inferExpr(*p->defaultValue));
        }
        params.push_back(pt);
        optional.push_back(p->optional || p->defaultValue != nullptr);
    }
}

void Analyzer::declareArrowParams(ArrowFunction& arrow,
                                  const std::vector<Type>& params) {
    for (std::size_t i = 0; i < arrow.params.size() && i < params.size(); ++i) {
        Parameter* p = arrow.params[i].get();
        if (!p) continue;
        Type t = nullableIf(p->optional, params[i]);
        declarePattern(p->name.get(), t, false, p->nodeId);
    }
}

Type Analyzer::inferArrowReturn(ArrowFunction& arrow,
                                const std::vector<Type>& params) {
    pushScope();
    declareArrowParams(arrow, params);
    Type ret = Type::unknown();
    FuncCtx ctx;
    ctx.annotated = false;
    // Only an async arrow may await inside its body.
    ctx.isAsync = arrow.isAsync;
    funcStack_.push_back(ctx);
    if (arrow.body) {
        if (arrow.expressionBody) {
            ret = widen(inferExpr(*static_cast<Expr*>(arrow.body.get())));
        } else {
            visitStmt(*static_cast<Stmt*>(arrow.body.get()));
        }
    }
    FuncCtx finalCtx = funcStack_.back();
    funcStack_.pop_back();
    popScope();
    if (!arrow.expressionBody) {
        ret = finalCtx.returnTypes.empty() ? Type::voidType()
                                           : Type::unionOf(finalCtx.returnTypes);
    } else if (arrow.returnType) {
        ret = resolveType(*arrow.returnType);
    } else if (arrow.body && arrow.body->kind != NodeKind::ReturnStatement) {
        ret = widen(ret);
    }
    if (arrow.returnType) ret = resolveType(*arrow.returnType);
    return ret;
}

}  // namespace cp3
