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

#include "detail/generator/cpp_writer.h"
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

}  // namespace

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
        const std::optional<Type> srcType = g.resolvedBindingType(d);
        const std::optional<Type> savedExpected = g.expected_;
        g.expected_ = srcType;
        const std::string initText = g.expr(*d.init);
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
