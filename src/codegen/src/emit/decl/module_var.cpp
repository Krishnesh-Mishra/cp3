// =============================================================================
// src/codegen/src/emit/module_var.cpp - module-scope variable lowering
// (STYLE.md section 8).
//
// Module-scope `let`/`const`/`var` lowers to a namespace-scope global so that
// functions declared in the unit can read it (a `main`-local cannot be seen by
// them, S1). The declaration moves to the decls section; a non-const
// initializer runs at the statement's original position in `main` (JS order),
// while a const must initialize in place.
//
// Two forms: an identifier binding (the global form, with a `main`-local
// fallback when there is no namespace-scope spelling), and a destructuring
// binding, which declares each bound name as a namespace-scope global and
// assigns it at the statement's position in `main`. The helper
// (`detail::ModuleVarEmitter`) keeps the whole module-variable concern on one
// friend object, so the Generator declaration stays within budget.
// =============================================================================
#include "detail/generator/generator.h"

#include <optional>
#include <string>

#include "cp3/diag/diagnostic_codes.hpp"
#include "detail/generator/cpp_writer.h"
#include "detail/generator/family.h"
#include "detail/generator/mangle.h"
#include "detail/expr/pattern_emitter.h"
#include "detail/generator/type_map.h"

namespace cp3 {

using namespace detail;

namespace {

bool isStaticInitExpr(const Expr& e) {
    switch (e.kind) {
        case NodeKind::NumberLiteral:
        case NodeKind::StringLiteral:
        case NodeKind::BoolLiteral:
        case NodeKind::NullLiteral:
        case NodeKind::UndefinedLiteral:
            return true;
        case NodeKind::UnaryExpr: {
            const auto& u = static_cast<const UnaryExpr&>(e);
            return u.operand && isStaticInitExpr(*u.operand);
        }
        case NodeKind::BinaryExpr: {
            const auto& b = static_cast<const BinaryExpr&>(e);
            return b.left && b.right && isStaticInitExpr(*b.left) &&
                   isStaticInitExpr(*b.right);
        }
        case NodeKind::LogicalExpr: {
            const auto& l = static_cast<const LogicalExpr&>(e);
            return l.left && l.right && isStaticInitExpr(*l.left) &&
                   isStaticInitExpr(*l.right);
        }
        case NodeKind::ConditionalExpr: {
            const auto& c = static_cast<const ConditionalExpr&>(e);
            return c.condition && c.whenTrue && c.whenFalse &&
                   isStaticInitExpr(*c.condition) &&
                   isStaticInitExpr(*c.whenTrue) &&
                   isStaticInitExpr(*c.whenFalse);
        }
        default:
            return false;
    }
}

// A declared tuple's C++ spelling: `classify` spells only all-primitive
// tuples, while a tuple holding record/class handles is still a `std::tuple`
// of those handles.
std::optional<CppInfo> tupleDeclaredInfo(const Type& t) {
    std::string cpp;
    if (!isTupleType(t) || !tupleCpp(t, cpp)) return std::nullopt;
    CppInfo info;
    info.shape = Shape::Object;
    info.representable = true;
    info.cpp = cpp;
    info.type = t;
    return info;
}

// Sema's permissive "not statically known" marker (`$ambient`), which the
// `object` annotation resolves to. A declaration spelled `object` stores
// whatever its initializer holds.
bool isAmbientDeclared(const Type& t) {
    return t.kind == TypeKind::Object && t.name == "$ambient";
}

}  // namespace

// Emit a namespace-scope module-state declaration for the shared header and
// record the matching definition for the owning unit's TU. In the ordinary
// build this is the single inline definition every TU of the monolithic
// binary shares. In the dev module-libs build every DLL would get its own
// copy of an inline variable, so the shared header carries `extern` and the
// owning unit's TU is the one image that defines it.
void Generator::moduleStateLine(std::string& decls, int indent,
                                const std::string& tl, const std::string& cv,
                                const std::string& typeCpp,
                                const std::string& name,
                                const std::string& initText) {
    if (moduleLibs_) {
        line(decls, indent, "extern " + tl + cv + typeCpp + " " + name + ";");
        splitStateDefs_ += tl + cv + typeCpp + " " + name;
        splitStateDefs_ += initText.empty() ? "{};\n" : " = " + initText + ";\n";
        return;
    }
    line(decls, indent, "inline " + tl + cv + typeCpp + " " + name +
                            (initText.empty() ? "{};"
                                              : " = " + initText + ";"));
}

namespace detail {

struct ModuleVarEmitter {
    explicit ModuleVarEmitter(Generator& generator) : g(generator) {}

    Generator& g;

    void run(const VariableDeclaration& vd, std::string& decls,
             std::string& mainBody);
    void pattern(const VariableDeclarator& d, std::string& decls,
                 std::string& mainBody);
    void binding(const VariableDeclarator& d, VarKind kind, std::string& decls,
                 std::string& mainBody);
    void entryLocal(const VariableDeclarator& d, VarKind kind,
                    std::string& mainBody);
    void noInit(const CppInfo& ci, const std::string& name,
                const std::string& cv, std::string& decls);
    void reportType(const VariableDeclarator& d);
};

// Module-scope `let`/`const`/`var` lowers to a namespace-scope global so that
// functions declared in the unit can read it (a `main`-local cannot be seen by
// them, S1). The declaration moves to the decls section; a non-const
// initializer runs at the statement's original position in `main` (JS order),
// while a const must initialize in place.
//
// Two forms: an identifier binding (the global form, with a `main`-local
// fallback when there is no namespace-scope spelling), and a destructuring
// binding, which declares each bound name as a namespace-scope global and
// assigns it at the statement's position in `main`.
void ModuleVarEmitter::run(const VariableDeclaration& vd, std::string& decls,
                           std::string& mainBody) {
    for (const auto& declPtr : vd.declarations) {
        const VariableDeclarator* d = declPtr.get();
        if (!d) continue;
        if (!d->id) {
            // A declarator without a binding name is a syntax error the parser
            // already reported; there is no binding to lower.
            continue;
        }
        if (d->id->kind != NodeKind::Identifier) {
            pattern(*d, decls, mainBody);
            continue;
        }
        binding(*d, vd.kind, decls, mainBody);
    }
}

// A destructuring binding materializes its source, then emits one binding per
// name. When the unit has functions, classes or exports, those names must be
// namespace-scope globals so the functions can read them (S1); the statement's
// position in `main` still performs the initialization, in JS order.
void ModuleVarEmitter::pattern(const VariableDeclarator& d, std::string& decls,
                               std::string& mainBody) {
    PatternEmitter emitter(g);
    if (g.moduleNeedsGlobals_) {
        emitter.globals = &decls;
        emitter.globalsAreModuleScope = true;
        emitter.globalsThreadLocal = g.scaleMode_;
    }
    // The binding text is assembled separately so a scaled server can replay
    // the same initialization on every loop thread (D79).
    std::string bindings;
    if (!d.init) {
        // No initializer: there is no source to read, so every name the
        // pattern introduces is bound to `undefined` - the same lowering a
        // plain `let x;` gets (the module-variable unit's `noInit`).
        emitter.emitUndefinedBindings(*d.id, bindings, 1);
    } else {
        std::optional<Type> srcType = g.resolvedBindingType(d);
        // An empty array literal whose element type sema could not pin
        // (`const [x = 7] = []`): the pattern's defaults carry it. The hint
        // feeds both the literal lowering and the source type the walk reads.
        std::optional<Type> hinted;
        if (d.init->kind == NodeKind::ArrayLiteral) {
            const auto& lit = static_cast<const ArrayLiteral&>(*d.init);
            if (lit.elements.empty() &&
                arrayElemOf(g.effectiveType(lit)).isUnknown()) {
                hinted = g.arrayDestructureHint(*d.id);
            }
        }
        if (hinted) srcType = Type::array(*hinted);
        const std::optional<Type> savedExpected = g.expected_;
        g.expected_ = srcType;
        const std::string initText =
            hinted ? g.arrayLiteralText(static_cast<const ArrayLiteral&>(*d.init),
                                        *hinted)
                   : g.expr(*d.init);
        g.expected_ = savedExpected;
        const std::string tmp = "cp3_d" + std::to_string(g.uniqueCounter_++);
        line(bindings, 1, "auto " + tmp + " = " + initText + ";");
        emitter.emitBindings(*d.id, tmp, srcType, bindings, 1);
    }
    mainBody += bindings;
    if (!g.scaleMode_) return;
    // Replay every initialization line on each child loop thread. The source
    // temporary is declared in this text too, so the assignments keep the
    // value they were lowered against.
    std::size_t start = 0;
    while (start < bindings.size()) {
        const std::size_t end = bindings.find('\n', start);
        std::string text = bindings.substr(
            start, end == std::string::npos ? std::string::npos : end - start);
        const std::size_t first = text.find_first_not_of(" \t");
        if (first != std::string::npos) {
            g.moduleInitLines_.push_back(text.substr(first));
        }
        if (end == std::string::npos) break;
        start = end + 1;
    }
}

// One identifier binding: the namespace-scope global (with the `main`-body
// assignment for a non-const initializer), or the `main`-local fallback.
void ModuleVarEmitter::binding(const VariableDeclarator& d, VarKind kind,
                               std::string& decls, std::string& mainBody) {
    const bool isConst = kind == VarKind::Const;
    const std::string cv = isConst ? "const " : "";
    // D79: a scaled server gives every loop thread its own module bindings.
    const std::string tl = g.scaleMode_ ? "thread_local " : "";
    const std::string name =
        mapIdent(static_cast<const Identifier&>(*d.id).name);
    const std::string& rawName = static_cast<const Identifier&>(*d.id).name;
    reportType(d);
    if (!g.moduleNeedsGlobals_) {
        entryLocal(d, kind, mainBody);
        return;
    }
    const std::optional<Type> declared = g.resolvedBindingType(d);
    CppInfo ci = declared ? classify(*declared) : CppInfo{};
    if (d.type) {
        if (auto dt = g.typeOfNode(*d.type)) {
            if (auto tuple = tupleDeclaredInfo(*dt)) ci = *tuple;
        }
    }
    if (d.init && !ci.representable) {
        const CppInfo fromInit = g.info(*d.init);
        if (fromInit.representable) ci = fromInit;
    }
    std::string valueArrayElement;
    if (g.stackValueArrayDecls_.count(d.nodeId) != 0 &&
        declared && declared->kind == TypeKind::Array &&
        !declared->parts.empty()) {
        CppInfo element = classify(declared->parts.front());
        constexpr const char* refPrefixes[] = {
            "::cp3::runtime::Ref<", "::cp3::runtime::LocalRef<"};
        std::size_t prefixSize = 0;
        for (const char* prefix : refPrefixes) {
            const std::size_t size = std::char_traits<char>::length(prefix);
            if (element.cpp.rfind(prefix, 0) == 0) {
                prefixSize = size;
                break;
            }
        }
        if (element.shape == Shape::ClassRef && element.representable &&
            prefixSize != 0 &&
            element.cpp.size() > prefixSize && element.cpp.back() == '>') {
            valueArrayElement = element.cpp.substr(
                prefixSize, element.cpp.size() - prefixSize - 1);
            ci.cpp = "::cp3::runtime::Array<" + valueArrayElement + ">";
            ci.shape = Shape::Array;
            ci.representable = true;
        }
    }
    if (!ci.representable) {
        // No namespace-scope spelling (e.g. an arrow binding, or a
        // top-level `await` initializer that needs the coroutine context):
        // keep the binding a `main`-local, the pre-globals lowering.
        g.emitLineDirective(d, mainBody);
        line(mainBody, 1, g.declaratorText(d, kind) + ";");
        g.declare(rawName, g.resolvedBindingType(d));
        return;
    }
    if (!d.init) {
        noInit(ci, name, cv, decls);
        g.declare(rawName, declared, /*cell=*/false, /*global=*/true);
        return;
    }
    // `let a: number[] = Array(3)` / `let a: number[] = []`: the
    // annotation fixes an otherwise unknowable element type
    // (src/language/typed-arrays/codegen.cpp). The annotation is also the shape an
    // object literal is built against.
    std::string initText;
    {
        const std::optional<Type> savedExpected = g.expected_;
        g.expected_ = ci.type;
        initText = g.modelInitText(d, ci.type);
        if (initText.empty() && d.type && ci.shape == Shape::Object &&
            d.init->kind == NodeKind::ObjectLiteral) {
            // An explicitly annotated record fixes the struct type; build that
            // struct from the literal (a field may be a union/option, whose
            // struct differs from the literal's narrower shape).
            initText = g.objectLiteralForType(
                static_cast<const ObjectLiteral&>(*d.init), ci.type);
        }
        if (initText.empty()) initText = g.declaratorInitText(d, ci.type);
        if (initText.empty()) initText = g.expr(*d.init);
        g.expected_ = savedExpected;
    }
    if (!valueArrayElement.empty() && d.init->kind == NodeKind::ArrayLiteral &&
        static_cast<const ArrayLiteral&>(*d.init).elements.empty()) {
        initText = "::cp3::runtime::makeArray<" + valueArrayElement +
                   ">(std::vector<" + valueArrayElement + ">{})";
    }
    if (ci.shape == Shape::Optional && isNullishLiteral(*d.init)) {
        initText = "std::nullopt";
    }
    if (isConst && initText.find("co_await") != std::string::npos) {
        // A const initializer that awaits only compiles inside the entry
        // coroutine: keep the binding a `main`-local (no static init).
        g.emitLineDirective(d, mainBody);
        line(mainBody, 1, g.declaratorText(d, kind) + ";");
        g.declare(rawName, g.resolvedBindingType(d));
        return;
    }
    if (isConst) {
        if (isStaticInitExpr(*d.init)) {
            g.emitLineDirective(d, decls);
            g.moduleStateLine(decls, 1, tl, cv, ci.cpp, name, initText);
        } else {
            // The initializer reads runtime state: evaluate it inside
            // `main` at its source position, after initArgv and the rest
            // of the runtime bootstrap. The binding keeps namespace scope
            // so functions can read it; language-level const-ness is
            // enforced by sema.
            g.moduleStateLine(decls, 1, tl, "", ci.cpp, name);
            g.emitLineDirective(d, mainBody);
            line(mainBody, 1, g.moduleRef(rawName) + " = " + initText + ";");
            if (g.scaleMode_) {
                g.moduleInitLines_.push_back(name + " = " + initText + ";");
            }
        }
        g.declare(rawName, declared, /*cell=*/false, /*global=*/true);
        return;
    }
    g.moduleStateLine(decls, 1, tl, "", ci.cpp, name);
    g.emitLineDirective(d, mainBody);
    line(mainBody, 1, g.moduleRef(rawName) + " = " + initText + ";");
    if (g.scaleMode_) {
        g.moduleInitLines_.push_back(name + " = " + initText + ";");
    }
    g.declare(rawName, declared, /*cell=*/false, /*global=*/true);
}

// The entry function is the only reader (no declared function, class, export
// or worker): declare the binding in `main`, where a loop body's calls cannot
// alias it, so the compiler keeps it in a register across the body.
void ModuleVarEmitter::entryLocal(const VariableDeclarator& d, VarKind kind,
                                   std::string& mainBody) {
    const std::string& rawName =
        static_cast<const Identifier&>(*d.id).name;
    g.emitLineDirective(d, mainBody);
    if (g.stackAllocDecls_.count(d.nodeId) != 0) {
        g.emitDeclaratorStmt(d, kind, mainBody, 1);
    } else {
        line(mainBody, 1, g.declaratorText(d, kind) + ";");
        g.declare(rawName, g.resolvedBindingType(d));
    }
}

void ModuleVarEmitter::noInit(const detail::CppInfo& ci,
                              const std::string& name,
                              const std::string& cv, std::string& decls) {
    const std::string tl = g.scaleMode_ ? "thread_local " : "";
    if (ci.representable && ci.shape != Shape::Void) {
        g.moduleStateLine(decls, 1, tl, cv, ci.cpp, name);
    } else {
        if (g.moduleLibs_) {
            line(decls, 1, "extern " + tl + cv + "cp3::runtime::Undefined " + name + ";");
            g.splitStateDefs_ += tl + cv + "cp3::runtime::Undefined " + name +
                                 " = cp3::runtime::undefined;\n";
        } else {
            line(decls, 1, "inline " + tl + cv + "auto " + name +
                              " = cp3::runtime::undefined;");
        }
    }
}

// A declared type without a C++ spelling is a hard error: silently deducing a
// narrower initializer type would not enforce the annotation. A tuple with
// record/class-handle elements is spellable even though `classify` keeps its
// all-primitive gate for ordinary tuple values.
void ModuleVarEmitter::reportType(const VariableDeclarator& d) {
    if (!d.type) return;
    auto dt = g.typeOfNode(*d.type);
    if (!dt) return;
    if (isAmbientDeclared(*dt)) {
        // `object` is the permissive "untyped value" spelling: the binding
        // stores whatever the initializer holds (the storage falls back to
        // the initializer's type below).
        if (!d.init) {
            g.reportUnsupported(d, diagcode::kTypeMismatch,
                                "type mismatch: an 'object' binding needs an "
                                "initializer");
        }
        return;
    }
    if (classify(*dt).representable || dt->isUnknown() || dt->isError() ||
        isChannelHandleType(*dt) || tupleDeclaredInfo(*dt)) {
        return;
    }
    g.reportUnsupported(d, diagcode::kTypeMismatch,
                        "type mismatch: this declared type has no C++ spelling");
}

}  // namespace detail

void Generator::emitModuleVar(const VariableDeclaration& vd, std::string& decls,
                              std::string& mainBody) {
    ModuleVarEmitter(*this).run(vd, decls, mainBody);
}

}  // namespace cp3
