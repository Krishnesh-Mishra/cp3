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
// binding, which stays in the `main` body (its names are locals). The helper
// (`detail::ModuleVarEmitter`) keeps the whole module-variable concern on one
// friend object, so the Generator declaration stays within budget.
// =============================================================================
#include "detail/generator.h"

#include <optional>
#include <string>

#include "detail/cpp_writer.h"
#include "detail/mangle.h"
#include "detail/pattern_emitter.h"
#include "detail/type_map.h"

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

}  // namespace

namespace detail {

struct ModuleVarEmitter {
    explicit ModuleVarEmitter(Generator& generator) : g(generator) {}

    Generator& g;

    void run(const VariableDeclaration& vd, std::string& decls,
             std::string& mainBody);
    void pattern(const VariableDeclarator& d, std::string& mainBody);
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
// binding, which stays in the `main` body (its names are locals).
void ModuleVarEmitter::run(const VariableDeclaration& vd, std::string& decls,
                           std::string& mainBody) {
    for (const auto& declPtr : vd.declarations) {
        const VariableDeclarator* d = declPtr.get();
        if (!d || !d->id) {
            if (d) g.notImplemented(*d, "destructuring binding");
            continue;
        }
        if (d->id->kind != NodeKind::Identifier) {
            pattern(*d, mainBody);
            continue;
        }
        binding(*d, vd.kind, decls, mainBody);
    }
}

// A destructuring binding materializes its source, then emits one plain
// binding per name; everything stays in the `main` body.
void ModuleVarEmitter::pattern(const VariableDeclarator& d,
                               std::string& mainBody) {
    const std::optional<Type> srcType = g.resolvedBindingType(d);
    std::string initText;
    if (d.init) {
        const std::optional<Type> savedExpected = g.expected_;
        g.expected_ = srcType;
        initText = g.expr(*d.init);
        g.expected_ = savedExpected;
    } else {
        g.notImplemented(d, "destructuring binding without an initializer");
        initText = "cp3::runtime::undefined";
    }
    const std::string tmp = "cp3_d" + std::to_string(g.uniqueCounter_++);
    line(mainBody, 1, "auto " + tmp + " = " + initText + ";");
    PatternEmitter(g).emitBindings(*d.id, tmp, srcType, mainBody, 1);
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
    if (d.init && !ci.representable) {
        const CppInfo fromInit = g.info(*d.init);
        if (fromInit.representable) ci = fromInit;
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
    // (src/std/typed-arrays/codegen.cpp). The annotation is also the shape an
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
            line(decls, 1, tl + cv + ci.cpp + " " + name + " = " +
                              initText + ";");
        } else {
            // The initializer reads runtime state: evaluate it inside
            // `main` at its source position, after initArgv and the rest
            // of the runtime bootstrap. The binding keeps namespace scope
            // so functions can read it; language-level const-ness is
            // enforced by sema.
            line(decls, 1, tl + ci.cpp + " " + name + "{};");
            g.emitLineDirective(d, mainBody);
            line(mainBody, 1, g.moduleRef(rawName) + " = " + initText + ";");
            if (g.scaleMode_) {
                g.moduleInitLines_.push_back(name + " = " + initText + ";");
            }
        }
        g.declare(rawName, declared, /*cell=*/false, /*global=*/true);
        return;
    }
    line(decls, 1, tl + ci.cpp + " " + name + "{};");
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
    line(mainBody, 1, g.declaratorText(d, kind) + ";");
    g.declare(rawName, g.resolvedBindingType(d));
}

void ModuleVarEmitter::noInit(const detail::CppInfo& ci,
                              const std::string& name,
                              const std::string& cv, std::string& decls) {
    const std::string tl = g.scaleMode_ ? "thread_local " : "";
    if (ci.representable && ci.shape != Shape::Void) {
        line(decls, 1, tl + cv + ci.cpp + " " + name + "{};");
    } else {
        line(decls, 1, tl + cv + "auto " + name +
                          " = cp3::runtime::undefined;");
    }
}

// A declared type M1 cannot represent (`number | string`, an object / array /
// class type, ...) keeps the unit "not implemented" instead of being quietly
// mis-typed (same contract as local declarations).
void ModuleVarEmitter::reportType(const VariableDeclarator& d) {
    if (!d.type) return;
    auto dt = g.typeOfNode(*d.type);
    if (!dt || classify(*dt).representable || dt->isUnknown() || dt->isError() ||
        isChannelHandleType(*dt)) {
        return;
    }
    g.notImplemented(d, "this declared type");
}

}  // namespace detail

void Generator::emitModuleVar(const VariableDeclaration& vd, std::string& decls,
                              std::string& mainBody) {
    ModuleVarEmitter(*this).run(vd, decls, mainBody);
}

}  // namespace cp3
