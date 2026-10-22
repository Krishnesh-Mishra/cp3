// =============================================================================
// src/sema/src/support/declarations.cpp - declaration + signature building passes.
//
// Owns function-signature building (`buildSig`), variable/pattern declaration
// (`declareVariableDeclaration` / `declareDeclarator` / `declarePattern`),
// `for`-binding declarations, import bindings and interface registration.
// Publishing of the resulting SymbolInfo/FunctionInfo/nodeTypes entries is
// delegated to symbols.cpp (STYLE.md §8).
// =============================================================================
#include "detail/analyzer.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "cp3/diagnostic_codes.hpp"
#include "cp3/native_types.hpp"

namespace cp3 {

// =============================================================================
// Function signatures
// =============================================================================
// Builds a signature from parameters + optional return annotation. Type
// parameters (when present) are registered as type names so their use in
// annotations does not cascade into CP3302.
FuncSig Analyzer::buildSig(const std::string& name, ParamList& params,
                           TypePtr& retType,
                           const SourceSpan* spanForTypeParams) {
    FuncSig sig;
    sig.name = name;
    (void)spanForTypeParams;
    for (std::size_t i = 0; i < params.size(); ++i) {
        Parameter* p = params[i].get();
        if (!p) continue;
        if (p->isRest) {
            Type elem = Type::unknown();
            if (p->type) {
                Type ann = resolveType(*p->type);
                if (ann.kind == TypeKind::Array) elem = elemOf(ann);
            }
            sig.params.push_back(elem);
            sig.optional.push_back(true);
            sig.rest = true;
            continue;
        }
        Type pt = ambient();
        if (p->type) {
            pt = resolveType(*p->type);
        } else if (p->defaultValue) {
            pt = widen(inferExpr(*p->defaultValue));
        }
        sig.params.push_back(pt);
        sig.optional.push_back(p->optional || p->defaultValue != nullptr);
    }
    if (retType) {
        sig.annotatedReturn = true;
        sig.ret = resolveType(*retType);
    }
    return sig;
}

// =============================================================================
// Variables + patterns
// =============================================================================
void Analyzer::declareVariableDeclaration(VariableDeclaration& vd) {
    for (auto& d : vd.declarations) {
        if (d) declareDeclarator(*d, vd.kind);
    }
    // A doc comment sits above the whole statement, so it documents every
    // declarator it introduces.
    for (auto& d : vd.declarations) {
        if (d) publishDoc(d->nodeId, vd.span);
    }
}

void Analyzer::declareDeclarator(VariableDeclarator& d, VarKind kind) {
    const bool isConst = kind == VarKind::Const;
    const bool isVar = kind == VarKind::Var;
    bool hasAnn = d.type != nullptr;
    Type annType = hasAnn ? resolveType(*d.type) : Type::unknown();

    // Block scoping (spec §03): the name is visible inside its own
    // initializer, so a closure created there may refer to it
    // (`let id = setInterval(() => clearInterval(id), 1)`). Pre-declare the
    // binding with the annotation (or `unknown`) so that reference resolves,
    // then replace it with the fully inferred binding below.
    bool preDeclared = false;
    std::string preDeclaredName;
    if (d.id && d.id->kind == NodeKind::Identifier) {
        const auto& ident = static_cast<const Identifier&>(*d.id);
        preDeclaredName = ident.name;
        if (!ident.name.empty() &&
            scopes_.back().find(ident.name) == scopes_.back().end()) {
            Binding b;
            b.name = ident.name;
            b.type = hasAnn ? annType : Type::unknown();
            b.publish = false;
            b.span = ident.span;
            b.isAmbient = isAmbientType(b.type);
            scopes_.back().emplace(ident.name, std::move(b));
            preDeclared = true;
        }
    }

    bool hasInit = d.init != nullptr;
    Type initType = Type::unknown();
    if (hasInit) initType = inferExpr(*d.init);

    // Unannotated heterogeneous array literal is a type error.
    if (!hasAnn && hasInit && d.init->kind == NodeKind::ArrayLiteral &&
        isMixedArray(initType)) {
        report(diagcode::kTypeMismatch, d.init->span,
               "type mismatch: array literal mixes incompatible element types "
               "(annotate the array with a union type)");
    }

    // The `Array(n)` dense form (src/std/typed-arrays/sema.cpp): the element
    // type must be inferable from an annotation and default-initializable. Once
    // handled, the generic assign check must not re-report `unknown[]` -> `T[]`.
    const bool arrayCtorForm =
        hasInit && checkArrayCtorDeclarator(d, hasAnn, annType, initType);

    Type varType =
        hasAnn ? annType : (hasInit ? widen(initType) : Type::undefined());

    if (hasAnn && hasInit) {
        if (d.init->kind == NodeKind::ArrayLiteral &&
            (annType.kind == TypeKind::Array || isTupleType(annType))) {
            checkArrayLiteral(static_cast<ArrayLiteral&>(*d.init), annType);
        } else if (!arrayCtorForm) {
            checkAssign(initType, annType, d.init->span,
                        d.init->kind == NodeKind::ObjectLiteral);
        }
    }

    setType(d, varType);
    // Drop the pre-declared placeholder (only if we inserted it: a shadowing
    // inner-scope name must stay intact) and declare the real binding.
    if (preDeclared) scopes_.back().erase(preDeclaredName);
    declarePattern(d.id.get(), varType, isConst, d.nodeId, isVar);

    // Preserve an arrow literal's optional/rest parameter shape so calls
    // through the binding get correct arity checking: the frozen `Type` cannot
    // carry per-parameter optionality.
    if (d.init && d.init->kind == NodeKind::ArrowFunction && d.id &&
        d.id->kind == NodeKind::Identifier) {
        auto& id = static_cast<Identifier&>(*d.id);
        auto it = scopes_.back().find(id.name);
        auto as = arrowSigs_.find(d.init->nodeId);
        if (it != scopes_.back().end() && as != arrowSigs_.end()) {
            it->second.isFunction = true;
            it->second.sig = as->second;
        }
    }
}

bool Analyzer::isMixedArray(const Type& t) const {
    if (t.kind != TypeKind::Array || t.parts.empty()) return false;
    const Type& e = t.parts.front();
    if (!e.isUnion()) return false;
    std::size_t nonNull = 0;
    for (const Type& m : e.parts) {
        if (!m.isNullish()) ++nonNull;
    }
    return nonNull >= 2;
}

void Analyzer::checkArrayLiteral(ArrayLiteral& lit, const Type& target) {
    // A native element type (`r_'u8'[]`) takes the element's own literal type,
    // not its widened base: the literal must reach `checkAssign` so the
    // integral range rule can accept `[1, 2, 3]` for an `i64[]`.
    const bool nativeElement =
        target.kind == TypeKind::Array && !target.parts.empty() &&
        isNativeType(target.parts.front());
    std::vector<Type> elems;
    bool hasSpread = false;
    for (auto& e : lit.elements) {
        if (!e) {
            elems.push_back(Type::undefined());
            continue;
        }
        if (e->kind == NodeKind::SpreadElement) {
            hasSpread = true;
            auto& sp = static_cast<SpreadElement&>(*e);
            Type at = sp.argument ? inferExpr(*sp.argument) : Type::unknown();
            elems.push_back(at.kind == TypeKind::Array ? elemOf(at)
                                                       : Type::unknown());
            continue;
        }
        const Type inferred = inferExpr(*e);
        elems.push_back(nativeElement ? inferred : widen(inferred));
    }
    if (isTupleType(target)) {
        // A tuple's shape is fixed (`[A, B]`), so a spread element has no
        // position in it.
        if (hasSpread) {
            report(diagcode::kTypeMismatch, lit.span,
                   "type mismatch: a spread element has no fixed position in a "
                   "tuple literal");
            return;
        }
        Shape* sh = shapeOf(target);
        const std::vector<Type>* ts = sh ? &sh->tupleElems : nullptr;
        if (!ts || ts->size() != elems.size()) {
            report(diagcode::kTypeMismatch, lit.span,
                   "type mismatch: tuple literal does not match the declared "
                   "tuple type");
            return;
        }
        for (std::size_t i = 0; i < elems.size(); ++i) {
            checkAssign(elems[i], (*ts)[i], lit.span, false);
        }
        return;
    }
    Type et = target.parts.empty() ? Type::unknown() : target.parts.front();
    for (const Type& e : elems) {
        checkAssign(e, et, lit.span, false);
    }
}

void Analyzer::declarePattern(Node* id, const Type& type, bool isConst,
                              std::uint32_t declNodeId, bool isVar) {
    if (!id) return;
    switch (id->kind) {
        case NodeKind::Identifier: {
            auto& ident = static_cast<Identifier&>(*id);
            setType(ident, type);
            declareBinding(ident.name, type, ident.span,
                           BindingSpec{.isConst = isConst, .isVar = isVar,
                                       .declNodeId = declNodeId});
            return;
        }
        case NodeKind::AssignmentPattern: {
            auto& ap = static_cast<AssignmentPattern&>(*id);
            Type t = type;
            if (t.isUnknown() && ap.right) t = widen(inferExpr(*ap.right));
            declarePattern(ap.left.get(), t, isConst, declNodeId, isVar);
            return;
        }
        case NodeKind::RestElement:
            declareRestPattern(static_cast<RestElement&>(*id), type, isConst,
                               declNodeId);
            return;
        case NodeKind::ArrayPattern:
            declareArrayPattern(static_cast<ArrayPattern&>(*id), type, isConst,
                                declNodeId);
            return;
        case NodeKind::ObjectPattern:
            declareObjectPattern(static_cast<ObjectPattern&>(*id), type, isConst,
                                 declNodeId);
            return;
        default:
            return;
    }
}

void Analyzer::declareRestPattern(RestElement& re, const Type& type, bool isConst,
                                  std::uint32_t declNodeId) {
    Type elem = type.kind == TypeKind::Array ? elemOf(type) : Type::unknown();
    declarePattern(re.argument.get(), Type::array(elem), isConst, declNodeId);
}

void Analyzer::declareArrayPattern(ArrayPattern& ap, const Type& type,
                                   bool isConst, std::uint32_t declNodeId) {
    Shape* sh = shapeOf(type);
    std::size_t index = 0;
    for (auto& el : ap.elements) {
        if (!el) {
            ++index;
            continue;
        }
        Type et = Type::unknown();
        if (sh && sh->tuple && index < sh->tupleElems.size()) {
            et = sh->tupleElems[index];
        } else if (type.kind == TypeKind::Array) {
            et = elemOf(type);
        }
        declarePattern(el.get(), et, isConst, declNodeId);
        ++index;
    }
    if (ap.rest) {
        Type elem = type.kind == TypeKind::Array ? elemOf(type) : Type::unknown();
        declarePattern(ap.rest.get(), Type::array(elem), isConst, declNodeId);
    }
}

void Analyzer::declareObjectPattern(ObjectPattern& op, const Type& type,
                                    bool isConst, std::uint32_t declNodeId) {
    Shape* sh = shapeOf(type);
    for (auto& prop : op.properties) {
        if (!prop) continue;
        std::string name = keyName(prop->key.get());
        Type pt = Type::unknown();
        if (const Field* f = findField(sh, name)) pt = f->type;
        declarePattern(prop->value.get(), pt, isConst, declNodeId);
    }
    if (op.rest) {
        declarePattern(op.rest.get(), ambient(), isConst, declNodeId);
    }
}

void Analyzer::declareForBinding(Node* left, const Type& elemType) {
    if (!left) return;
    if (left->kind == NodeKind::VariableDeclaration) {
        auto& vd = static_cast<VariableDeclaration&>(*left);
        const bool isConst = vd.kind == VarKind::Const;
        const bool isVar = vd.kind == VarKind::Var;
        for (auto& d : vd.declarations) {
            if (!d) continue;
            Type t = d->type ? resolveType(*d->type) : elemType;
            setType(*d, t);
            declarePattern(d->id.get(), t, isConst, d->nodeId, isVar);
        }
    } else if (auto* e = dynamicExpr(*left)) {
        inferExpr(*e);
    }
}

// =============================================================================
// Imports + interfaces
// =============================================================================
void Analyzer::analyzeImport(ImportDeclaration& d) {
    if (!d.defaultBinding.empty())
        declareAmbient(d.defaultBinding, d.span, d.nodeId);
    if (!d.namespaceBinding.empty())
        declareAmbient(d.namespaceBinding, d.span, d.nodeId);
    // A native import's alias was declared by the native pre-pass
    // (collectNativeAliases), which knows the binding behind it.
    if (!d.alias.empty() && d.kind != ImportKind::Native)
        declareAmbient(d.alias, d.span, d.nodeId);
    const bool fromStd = d.source.rfind("std/", 0) == 0;
    for (auto& sp : d.specifiers) {
        if (!sp) continue;
        const std::string& local = sp->local.empty() ? sp->imported : sp->local;
        declareAmbient(local, sp->span, sp->nodeId);
        // A named import may also be a type (an exported local enum used as a
        // worker channel payload). Imports are compile-time and sema does not
        // fetch the other unit; registering the name as an ambient nominal
        // type lets `Thread<Color, Color>` resolve. Codegen re-emits imported
        // enum declarations from the module graph (CodegenOptions).
        typeNames_.emplace(local, Type::named(TypeKind::Class, local));
        // A named std import keeps the exported name it was bound to, so a
        // construct that keys on the canonical export (`new M<...>()` where
        // `M` is `Map`) still reaches the family lowering.
        if (fromStd && !sp->imported.empty() && !local.empty() &&
            local != sp->imported) {
            stdImportAliases_[local] = sp->imported;
        }
    }
}

std::string Analyzer::importedName(const std::string& local) const {
    const auto it = stdImportAliases_.find(local);
    return it == stdImportAliases_.end() ? local : it->second;
}

void Analyzer::registerInterface(InterfaceDeclaration& i) {
    std::vector<Field> fields;
    bool open = false;
    // A generic interface's own type parameters are in scope while its members
    // are resolved, so `value: T` resolves to a type parameter (D26).
    TypeParamScope saved;
    enterClassTypeParams(i.typeParams, saved);
    for (auto& m : i.members) {
        if (!m) continue;
        if (m->kind == NodeKind::PropertySignature) {
            addPropertySignature(fields, open,
                                 static_cast<PropertySignature&>(*m));
        } else if (m->kind == NodeKind::MethodSignature) {
            addMethodSignature(fields, open, static_cast<MethodSignature&>(*m));
        }
    }
    leaveClassTypeParams(saved);
    if (!i.typeParams.empty()) {
        // A generic interface keeps its members: every use substitutes the
        // type arguments into them (resolveNamedType).
        GenericInterface generic;
        generic.fields = fields;
        generic.open = open;
        for (const auto& tp : i.typeParams) {
            if (tp) generic.typeParams.push_back(tp->name);
        }
        genericInterfaces_[i.name] = std::move(generic);
    }
    typeNames_[i.name] = makeObjectType(std::move(fields), open, false, {});
}

// =============================================================================
// Declaration statements (dispatched from visitStmt via visitDeclStmt)
// =============================================================================
void Analyzer::visitDeclStmt(Stmt& s) {
    switch (s.kind) {
        case NodeKind::ClassDeclaration:
            visitClassDecl(static_cast<ClassDeclaration&>(s));
            return;
        case NodeKind::EnumDeclaration:
            visitEnumDecl(static_cast<EnumDeclaration&>(s));
            return;
        case NodeKind::TypeAliasDeclaration:
            visitTypeAlias(static_cast<TypeAliasDeclaration&>(s));
            return;
        case NodeKind::InterfaceDeclaration:
            visitInterfaceDecl(static_cast<InterfaceDeclaration&>(s));
            return;
        case NodeKind::ImportDeclaration:
            analyzeImport(static_cast<ImportDeclaration&>(s));
            return;
        case NodeKind::ExportDeclaration:
            visitExport(static_cast<ExportDeclaration&>(s));
            return;
        default:
            return;
    }
}

void Analyzer::visitClassDecl(ClassDeclaration& c) {
    // resolveClasses (checkStmts) already built the member table; an exported
    // class visited without that pre-pass is resolved here instead.
    if (classResolved_.insert(c.nodeId).second) resolveClass(c);
    checkClassBodies(c);
}

void Analyzer::visitEnumDecl(EnumDeclaration& e) {
    // The resolution pass already ran from `hoistType` (checkStmts hoists the
    // block's type declarations before visiting it); registerEnum is
    // idempotent, so this is the export-wrapped / nested fallback.
    registerEnum(e);
}

void Analyzer::visitTypeAlias(TypeAliasDeclaration& t) {
    // The hoisting pass resolved the alias already (scopes.cpp); a nested
    // alias is resolved here.
    if (declaredTypes_.count(t.nodeId) != 0) return;
    declareTypeAlias(t);
}

// A type alias declares a compile-time name for a type. The alias's own type
// parameters are in scope while the aliased type is resolved, so
// `type Pair<T> = [T, T]` resolves `T` to a type parameter; a use site
// substitutes its type arguments into the aliased type.
void Analyzer::declareTypeAlias(TypeAliasDeclaration& t) {
    declaredTypes_.insert(t.nodeId);
    std::vector<std::string> params;
    for (const auto& tp : t.typeParams) {
        if (tp) params.push_back(tp->name);
    }
    // The class scope helper binds a type-parameter list to the type-name
    // scope; a type alias uses the same binding.
    TypeParamScope saved;
    enterClassTypeParams(t.typeParams, saved);
    typeNames_[t.name] = t.type ? resolveType(*t.type) : Type::unknown();
    leaveClassTypeParams(saved);
    if (params.empty()) {
        typeAliasParams_.erase(t.name);
        return;
    }
    typeAliasParams_[t.name] = std::move(params);
}

// The aliased type of a type alias: the alias's own type arguments are
// substituted for its type parameters, so `Pair<number>` is `[number, number]`.
Type Analyzer::resolveTypeAlias(const std::string& name,
                                const std::vector<Type>& args) const {
    const auto it = typeNames_.find(name);
    if (it == typeNames_.end()) return Type::unknown();
    const auto params = typeAliasParams_.find(name);
    if (params == typeAliasParams_.end() || params->second.empty() ||
        args.empty()) {
        return it->second;
    }
    return substituteTypeParams(it->second, params->second, args);
}

// The type `t` with the type arguments `args` substituted for the type
// parameters `names` (the parameter at the same position).
Type Analyzer::substituteTypeParams(const Type& t,
                                    const std::vector<std::string>& names,
                                    const std::vector<Type>& args) const {
    if (t.kind == TypeKind::TypeParam) {
        for (std::size_t i = 0; i < names.size(); ++i) {
            if (names[i] != t.name) continue;
            if (i < args.size()) return args[i];
            return t;
        }
        return t;
    }
    Type out = t;
    for (Type& part : out.parts) {
        part = substituteTypeParams(part, names, args);
    }
    return out;
}

void Analyzer::visitInterfaceDecl(InterfaceDeclaration& i) {
    if (declaredTypes_.count(i.nodeId) == 0) registerInterface(i);
}

void Analyzer::visitExport(ExportDeclaration& e) {
    if (e.defaultExpr) inferExpr(*e.defaultExpr);
    if (e.declaration) visitStmt(*e.declaration);
}

}  // namespace cp3
