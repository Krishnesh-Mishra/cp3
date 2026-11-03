// cp3-lint: allow-file-size - the proof and its walkers share file-local facts.
// =============================================================================
// src/codegen/src/support/array_refs.cpp - array reads bound by reference.
//
// `const word = words[i]` used to lower to `arrayAt<E>`, which returns a fresh
// `std::string` per lookup. The element is only read, so the copy can be
// dropped by binding a const reference, provided the element cannot be
// invalidated while the reference is alive.
//
// The proof is whole-unit and complete-or-nothing: a construct this walk does
// not model rejects every candidate. A candidate is accepted only when:
//
//   * the array binding is declared once with an array-literal initializer (so
//     an incoming alias cannot be mutated through another name);
//   * the array is never mutated, never assigned, never exported and never
//     handed to unknown code anywhere in the unit (a mutator call, an element
//     or `length` write, a plain value read, a call argument, a return, an
//     object/fresh-array literal, an arrow capture, a `for...of` source);
//   * the array outlives the reference: it is a namespace-scope global, or it
//     is declared in the same frame as the candidate (an array in an outer
//     frame could die while the candidate's frame lives on);
//   * the result binding is a `const` declared once, with no declared type,
//     never written (including writes rooted at it, such as `word.length = 0`)
//     and read only in the frame that declares it (a read in a deeper frame is
//     a capture, and a capture can outlive the array).
//
// Element types are restricted to scalar-like shapes (number, string, boolean,
// enum): those lower to value types whose copy/alias behaviour is fully
// modelled here. Arrays, objects and class handles are rejected.
//
// The emitted read uses `arrayAtRef` (same bounds check and panic as
// `arrayAt`), documented in cp3/array_extras.hpp.
// =============================================================================
#include "detail/generator/generator.h"

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "detail/generator/family.h"

namespace cp3 {

using namespace detail;

namespace {

// The array members that read (or mutate) the receiver without storing it.
bool isKnownArrayMember(const std::string& name, bool& mutates) {
    static const std::unordered_set<std::string> kMutating = {
        "push", "pop", "shift", "unshift", "splice", "sort", "reverse",
        "fill", "copyWithin", "unique", "shuffle"};
    static const std::unordered_set<std::string> kReading = {
        "length", "forEach", "map", "filter", "reduce", "reduceRight", "find",
        "findIndex", "some", "every", "includes", "indexOf", "lastIndexOf",
        "join", "slice", "concat", "flat", "flatMap", "keys", "values",
        "entries", "toString", "at", "binarySearch", "toSorted",
        "toReversed"};
    mutates = kMutating.count(name) != 0;
    return mutates || kReading.count(name) != 0;
}

// A freshly allocated array: `[...]`.
//
// Only an array literal counts. The `Array(...)` / `new Array(...)` forms are
// excluded on purpose: `Array` is an ordinary global name, so a unit that
// declares its own `Array` (or receives one through an import) can hand back
// storage that another name in the unit also owns. A literal cannot.
bool freshArrayInit(const Expr* e) {
    return e != nullptr && e->kind == NodeKind::ArrayLiteral;
}

bool isScalarElement(Shape shape) {
    return shape == Shape::Number || shape == Shape::String ||
           shape == Shape::Bool || shape == Shape::Enum;
}

// The frames a name is read in. The walk records the shallowest and the
// deepest frame; a name read only in its own frame is never captured.
struct FrameRange {
    int min = -1;
    int max = -1;
    void add(int depth) {
        if (min < 0 || depth < min) min = depth;
        if (depth > max) max = depth;
    }
};

struct Facts {
    std::unordered_map<std::string, int> decls;
    std::unordered_map<std::string, const VariableDeclarator*> declSites;
    std::unordered_set<std::string> written;
    // Writes whose target is a member/index rooted at the name (`word[0] =`,
    // `word.length =`, `arr[0]++`).
    std::unordered_set<std::string> rootWritten;
    // The frames each name is read in (a read in a deeper frame than the
    // declaration is a capture).
    std::unordered_map<std::string, FrameRange> reads;
    // The frame each name is declared in.
    std::unordered_map<std::string, int> declFrame;
    // Names declared directly in the module body: a namespace-scope global,
    // which outlives every frame in the unit.
    std::unordered_set<std::string> globals;
    // Names mutated anywhere (a mutator call or any write).
    std::unordered_set<std::string> mutated;
    // Names read in a value position that could hand out or retain the value.
    std::unordered_set<std::string> escaped;
    // Names declared with a freshly allocated array initializer.
    std::unordered_set<std::string> freshArray;
    // The frame depth of the walk: 0 is the module frame, 1 a function or
    // arrow body, deeper for a nested one.
    int depth = 0;
    // The block depth of the walk: 0 at the module body level, deeper inside
    // a block. A module-body binding that is not inside a block is a global.
    int blockDepth = 0;
    // Every block of the unit, with the frame it sits in.
    std::vector<const BlockStatement*> blocks;
    std::vector<int> blockFrames;
    bool complete = true;
};

void scanStmt(const Stmt& st, Facts& f);
void scanExpr(const Expr& e, Facts& f);

// A read of `name` in the current frame.
void noteRead(const std::string& name, Facts& f) {
    f.reads[name].add(f.depth);
}

void markWritten(const std::string& name, Facts& f) {
    f.written.insert(name);
    f.mutated.insert(name);
}

void markRootWritten(const std::string& name, Facts& f) {
    f.rootWritten.insert(name);
    f.mutated.insert(name);
}

const Identifier* rootIdentifier(const Expr& e) {
    if (e.kind == NodeKind::Identifier) {
        return &static_cast<const Identifier&>(e);
    }
    if (e.kind == NodeKind::MemberExpr) {
        const auto& m = static_cast<const MemberExpr&>(e);
        return m.object ? rootIdentifier(*m.object) : nullptr;
    }
    if (e.kind == NodeKind::IndexExpr) {
        const auto& ix = static_cast<const IndexExpr&>(e);
        return ix.object ? rootIdentifier(*ix.object) : nullptr;
    }
    return nullptr;
}

// Visit the index expressions inside a member/index write target; the object
// chain itself is handled by `rootIdentifier`.
void scanTargetParts(const Expr& e, Facts& f) {
    if (e.kind == NodeKind::IndexExpr) {
        const auto& ix = static_cast<const IndexExpr&>(e);
        if (ix.index) scanExpr(*ix.index, f);
        if (ix.object) scanTargetParts(*ix.object, f);
        return;
    }
    if (e.kind == NodeKind::MemberExpr) {
        const auto& m = static_cast<const MemberExpr&>(e);
        if (m.object) scanTargetParts(*m.object, f);
    }
}

// A write target: `x =`, `x++`, `delete x`, `x.m =`, `x[i] =`, `x[i]++`.
void writeTarget(const Expr& e, Facts& f) {
    if (e.kind == NodeKind::Identifier) {
        markWritten(static_cast<const Identifier&>(e).name, f);
        return;
    }
    if (e.kind == NodeKind::MemberExpr || e.kind == NodeKind::IndexExpr) {
        if (const Identifier* root = rootIdentifier(e)) {
            markRootWritten(root->name, f);
        } else if (e.kind == NodeKind::MemberExpr) {
            const auto& m = static_cast<const MemberExpr&>(e);
            if (m.object) scanExpr(*m.object, f);
        } else {
            const auto& ix = static_cast<const IndexExpr&>(e);
            if (ix.object) scanExpr(*ix.object, f);
        }
        scanTargetParts(e, f);
        return;
    }
    // A destructuring assignment target is not modelled by this walk.
    f.complete = false;
}

// A binding-pattern declaration: names introduced plus default expressions.
void scanPatternDecl(const Node& n, Facts& f) {
    switch (n.kind) {
        case NodeKind::Identifier: {
            const std::string& name = static_cast<const Identifier&>(n).name;
            ++f.decls[name];
            if (f.declFrame.find(name) == f.declFrame.end()) {
                f.declFrame[name] = f.depth;
            }
            if (f.depth == 0 && f.blockDepth == 0) f.globals.insert(name);
            return;
        }
        case NodeKind::AssignmentPattern: {
            const auto& ap = static_cast<const AssignmentPattern&>(n);
            if (ap.left) scanPatternDecl(*ap.left, f);
            if (ap.right) scanExpr(*ap.right, f);
            return;
        }
        case NodeKind::RestElement: {
            const auto& re = static_cast<const RestElement&>(n);
            if (re.argument) scanPatternDecl(*re.argument, f);
            return;
        }
        case NodeKind::ArrayPattern: {
            const auto& ap = static_cast<const ArrayPattern&>(n);
            for (const auto& el : ap.elements) {
                if (el) scanPatternDecl(*el, f);
            }
            if (ap.rest) scanPatternDecl(*ap.rest, f);
            return;
        }
        case NodeKind::ObjectPattern: {
            const auto& op = static_cast<const ObjectPattern&>(n);
            for (const auto& prop : op.properties) {
                if (!prop) continue;
                if (prop->computed && prop->key) scanExpr(*prop->key, f);
                if (prop->value) scanPatternDecl(*prop->value, f);
            }
            if (op.rest) scanPatternDecl(*op.rest, f);
            return;
        }
        default:
            f.complete = false;
            return;
    }
}

// A binding-pattern write (assignment or `for (... of ...)` target).
void scanPatternWrite(const Node& n, Facts& f) {
    switch (n.kind) {
        case NodeKind::Identifier:
            markWritten(static_cast<const Identifier&>(n).name, f);
            return;
        case NodeKind::AssignmentPattern: {
            const auto& ap = static_cast<const AssignmentPattern&>(n);
            if (ap.left) scanPatternWrite(*ap.left, f);
            if (ap.right) scanExpr(*ap.right, f);
            return;
        }
        case NodeKind::RestElement: {
            const auto& re = static_cast<const RestElement&>(n);
            if (re.argument) scanPatternWrite(*re.argument, f);
            return;
        }
        case NodeKind::ArrayPattern: {
            const auto& ap = static_cast<const ArrayPattern&>(n);
            for (const auto& el : ap.elements) {
                if (el) scanPatternWrite(*el, f);
            }
            if (ap.rest) scanPatternWrite(*ap.rest, f);
            return;
        }
        case NodeKind::ObjectPattern: {
            const auto& op = static_cast<const ObjectPattern&>(n);
            for (const auto& prop : op.properties) {
                if (!prop) continue;
                if (prop->computed && prop->key) scanExpr(*prop->key, f);
                if (prop->value) scanPatternWrite(*prop->value, f);
            }
            if (op.rest) scanPatternWrite(*op.rest, f);
            return;
        }
        default:
            f.complete = false;
            return;
    }
}

void scanParams(const ParamList& params, Facts& f) {
    for (const auto& p : params) {
        if (!p) {
            f.complete = false;
            continue;
        }
        if (p->name) scanPatternDecl(*p->name, f);
        if (p->defaultValue) scanExpr(*p->defaultValue, f);
    }
}

void scanFunction(const FunctionDeclaration& fd, Facts& f) {
    if (!fd.name.empty()) ++f.decls[fd.name];
    // The parameters and the body live in a new frame: a name read here is
    // read in a deeper frame than a module-body declaration.
    const int saved = f.depth;
    ++f.depth;
    scanParams(fd.params, f);
    if (fd.body) scanStmt(*fd.body, f);
    f.depth = saved;
}

void scanArrow(const ArrowFunction& af, Facts& f) {
    const int saved = f.depth;
    ++f.depth;
    scanParams(af.params, f);
    if (!af.body) {
        f.complete = false;
        f.depth = saved;
        return;
    }
    if (auto* body = dynamic_cast<const Expr*>(af.body.get())) {
        scanExpr(*body, f);
    } else if (auto* body = dynamic_cast<const Stmt*>(af.body.get())) {
        scanStmt(*body, f);
    } else {
        f.complete = false;
    }
    f.depth = saved;
}

void scanExprList(const ExprList& xs, Facts& f) {
    for (const auto& x : xs) {
        if (x) scanExpr(*x, f);
    }
}

void scanObjectLiteral(const ObjectLiteral& ol, Facts& f) {
    for (const auto& prop : ol.properties) {
        if (!prop) continue;
        if (prop->computed && prop->key) scanExpr(*prop->key, f);
        if (prop->value) scanExpr(*prop->value, f);
    }
}

// Record a receiver read: a known mutator is a root write, a known reader
// only reads, and an unknown member may hand out or retain the receiver.
void scanReceiver(const std::string& name, const std::string& member,
                  Facts& f) {
    noteRead(name, f);
    bool mutates = false;
    if (!isKnownArrayMember(member, mutates)) {
        f.escaped.insert(name);
    } else if (mutates) {
        markRootWritten(name, f);
    }
}

void scanMemberExpr(const MemberExpr& m, Facts& f) {
    if (!m.object) {
        f.complete = false;
        return;
    }
    if (m.object->kind == NodeKind::Identifier) {
        scanReceiver(static_cast<const Identifier&>(*m.object).name, m.property, f);
        return;
    }
    scanExpr(*m.object, f);
}

void scanIndexExpr(const IndexExpr& ix, Facts& f) {
    if (!ix.object) {
        f.complete = false;
        return;
    }
    if (ix.object->kind == NodeKind::Identifier) {
        noteRead(static_cast<const Identifier&>(*ix.object).name, f);
    } else {
        scanExpr(*ix.object, f);
    }
    if (ix.index) scanExpr(*ix.index, f);
}

void scanCallee(const Expr& callee, Facts& f) {
    if (callee.kind != NodeKind::MemberExpr) {
        scanExpr(callee, f);
        return;
    }
    const auto& m = static_cast<const MemberExpr&>(callee);
    if (m.object && m.object->kind == NodeKind::Identifier) {
        scanReceiver(static_cast<const Identifier&>(*m.object).name, m.property, f);
        return;
    }
    if (m.object) scanExpr(*m.object, f);
}

void scanCallExpr(const CallExpr& c, Facts& f) {
    if (c.callee) scanCallee(*c.callee, f);
    scanExprList(c.arguments, f);
}

void scanNewExpr(const NewExpr& n, Facts& f) {
    if (n.callee) scanExpr(*n.callee, f);
    scanExprList(n.arguments, f);
}

void scanUnaryExpr(const UnaryExpr& u, Facts& f) {
    if (!u.operand) {
        f.complete = false;
        return;
    }
    if (u.op == UnaryOp::Delete) {
        writeTarget(*u.operand, f);
    } else {
        scanExpr(*u.operand, f);
    }
}

void scanUpdateExpr(const UpdateExpr& u, Facts& f) {
    if (!u.operand) {
        f.complete = false;
        return;
    }
    writeTarget(*u.operand, f);
}

void scanBinaryExpr(const BinaryExpr& b, Facts& f) {
    if (b.left) scanExpr(*b.left, f);
    if (b.right) scanExpr(*b.right, f);
}

void scanLogicalExpr(const LogicalExpr& l, Facts& f) {
    if (l.left) scanExpr(*l.left, f);
    if (l.right) scanExpr(*l.right, f);
}

void scanConditionalExpr(const ConditionalExpr& c, Facts& f) {
    if (c.condition) scanExpr(*c.condition, f);
    if (c.whenTrue) scanExpr(*c.whenTrue, f);
    if (c.whenFalse) scanExpr(*c.whenFalse, f);
}

void scanAssignmentExpr(const AssignmentExpr& a, Facts& f) {
    if (!a.target) {
        f.complete = false;
        return;
    }
    writeTarget(*a.target, f);
    if (a.value) scanExpr(*a.value, f);
}

// Literal leaves and single-child wrappers.
bool scanLeafOrWrapper(const Expr& e, Facts& f) {
    switch (e.kind) {
        case NodeKind::NumberLiteral:
        case NodeKind::StringLiteral:
        case NodeKind::BoolLiteral:
        case NodeKind::NullLiteral:
        case NodeKind::UndefinedLiteral:
        case NodeKind::RegExpLiteral:
        case NodeKind::ThisExpr:
            return true;
        case NodeKind::SpreadElement: {
            const auto& sp = static_cast<const SpreadElement&>(e);
            if (sp.argument) scanExpr(*sp.argument, f);
            return true;
        }
        case NodeKind::WeakExpr: {
            const auto& w = static_cast<const WeakExpr&>(e);
            if (w.value) scanExpr(*w.value, f);
            return true;
        }
        case NodeKind::AwaitExpr: {
            const auto& a = static_cast<const AwaitExpr&>(e);
            if (a.argument) scanExpr(*a.argument, f);
            return true;
        }
        case NodeKind::RangeExpr: {
            const auto& r = static_cast<const RangeExpr&>(e);
            if (r.start) scanExpr(*r.start, f);
            if (r.end) scanExpr(*r.end, f);
            return true;
        }
        default:
            return false;
    }
}

void scanExpr(const Expr& e, Facts& f) {
    if (scanLeafOrWrapper(e, f)) return;
    switch (e.kind) {
        case NodeKind::Identifier: {
            const std::string& name = static_cast<const Identifier&>(e).name;
            f.escaped.insert(name);
            noteRead(name, f);
            return;
        }
        case NodeKind::TemplateLiteral:
            scanExprList(static_cast<const TemplateLiteral&>(e).expressions, f);
            return;
        case NodeKind::ArrayLiteral:
            scanExprList(static_cast<const ArrayLiteral&>(e).elements, f);
            return;
        case NodeKind::ObjectLiteral:
            scanObjectLiteral(static_cast<const ObjectLiteral&>(e), f);
            return;
        case NodeKind::MemberExpr:
            scanMemberExpr(static_cast<const MemberExpr&>(e), f);
            return;
        case NodeKind::IndexExpr:
            scanIndexExpr(static_cast<const IndexExpr&>(e), f);
            return;
        case NodeKind::CallExpr:
            scanCallExpr(static_cast<const CallExpr&>(e), f);
            return;
        case NodeKind::NewExpr:
            scanNewExpr(static_cast<const NewExpr&>(e), f);
            return;
        case NodeKind::UnaryExpr:
            scanUnaryExpr(static_cast<const UnaryExpr&>(e), f);
            return;
        case NodeKind::UpdateExpr:
            scanUpdateExpr(static_cast<const UpdateExpr&>(e), f);
            return;
        case NodeKind::BinaryExpr:
            scanBinaryExpr(static_cast<const BinaryExpr&>(e), f);
            return;
        case NodeKind::LogicalExpr:
            scanLogicalExpr(static_cast<const LogicalExpr&>(e), f);
            return;
        case NodeKind::ConditionalExpr:
            scanConditionalExpr(static_cast<const ConditionalExpr&>(e), f);
            return;
        case NodeKind::AssignmentExpr:
            scanAssignmentExpr(static_cast<const AssignmentExpr&>(e), f);
            return;
        case NodeKind::ArrowFunction:
            scanArrow(static_cast<const ArrowFunction&>(e), f);
            return;
        default:
            // Class bodies, imports and any future node kind: the walk cannot
            // see the whole unit, so no candidate is accepted.
            f.complete = false;
            return;
    }
}

void scanBlock(const BlockStatement& b, Facts& f) {
    // Every block of the unit, with the frame it sits in (0 is the module
    // frame, 1 a function or arrow body). The frame decides whether a
    // binding can be captured by a nested frame.
    f.blocks.push_back(&b);
    f.blockFrames.push_back(f.depth);
    // A binding inside a block is not a namespace-scope global, whatever the
    // frame it sits in.
    ++f.blockDepth;
    for (const auto& child : b.body) {
        if (child) scanStmt(*child, f);
    }
    --f.blockDepth;
}

void scanVariableDeclaration(const VariableDeclaration& vd, Facts& f) {
    for (const auto& d : vd.declarations) {
        if (!d) {
            f.complete = false;
            continue;
        }
        if (d->id) scanPatternDecl(*d->id, f);
        if (d->id && d->id->kind == NodeKind::Identifier) {
            const std::string& name =
                static_cast<const Identifier&>(*d->id).name;
            f.declSites[name] = d.get();
            if (freshArrayInit(d->init.get())) {
                f.freshArray.insert(name);
            } else {
                f.freshArray.erase(name);
            }
        }
        if (d->init) scanExpr(*d->init, f);
    }
}

void scanEnumDeclaration(const EnumDeclaration& ed, Facts& f) {
    for (const auto& m : ed.members) {
        if (m && m->initializer) {
            f.complete = false;
            return;
        }
    }
}

void scanIfStatement(const IfStatement& is, Facts& f) {
    if (is.condition) scanExpr(*is.condition, f);
    if (is.thenBranch) scanStmt(*is.thenBranch, f);
    if (is.elseBranch) scanStmt(*is.elseBranch, f);
}

void scanWhileStatement(const WhileStatement& ws, Facts& f) {
    if (ws.condition) scanExpr(*ws.condition, f);
    if (ws.body) scanStmt(*ws.body, f);
}

void scanDoWhileStatement(const DoWhileStatement& dw, Facts& f) {
    if (dw.body) scanStmt(*dw.body, f);
    if (dw.condition) scanExpr(*dw.condition, f);
}

void scanForStatement(const ForStatement& fs, Facts& f) {
    if (fs.init) {
        if (fs.init->kind == NodeKind::VariableDeclaration) {
            scanStmt(static_cast<const Stmt&>(*fs.init), f);
        } else if (auto* init = dynamic_cast<const Expr*>(fs.init.get())) {
            scanExpr(*init, f);
        } else {
            f.complete = false;
        }
    }
    if (fs.condition) scanExpr(*fs.condition, f);
    if (fs.update) scanExpr(*fs.update, f);
    if (fs.body) scanStmt(*fs.body, f);
}

void scanForEachStatement(const Node* left, const Expr* right,
                          const Stmt* body, Facts& f) {
    if (right) scanExpr(*right, f);
    if (left) {
        if (left->kind == NodeKind::VariableDeclaration) {
            scanStmt(static_cast<const Stmt&>(*left), f);
        } else if (left->kind == NodeKind::Identifier) {
            markWritten(static_cast<const Identifier&>(*left).name, f);
        } else if (left->kind == NodeKind::ArrayPattern ||
                   left->kind == NodeKind::ObjectPattern ||
                   left->kind == NodeKind::AssignmentPattern ||
                   left->kind == NodeKind::RestElement) {
            scanPatternWrite(*left, f);
        } else if (auto* target = dynamic_cast<const Expr*>(left)) {
            writeTarget(*target, f);
        } else {
            f.complete = false;
        }
    }
    if (body) scanStmt(*body, f);
}

void scanSwitchStatement(const SwitchStatement& sw, Facts& f) {
    if (sw.discriminant) scanExpr(*sw.discriminant, f);
    for (const auto& c : sw.cases) {
        if (!c) continue;
        if (c->test) scanExpr(*c->test, f);
        for (const auto& child : c->statements) {
            if (child) scanStmt(*child, f);
        }
    }
}

void scanTryStatement(const TryStatement& ts, Facts& f) {
    if (ts.tryBlock) scanStmt(*ts.tryBlock, f);
    if (ts.handler) {
        if (ts.handler->param) {
            scanPatternDecl(*ts.handler->param, f);
        }
        if (ts.handler->body) scanStmt(*ts.handler->body, f);
    }
    if (ts.finallyBlock) scanStmt(*ts.finallyBlock, f);
}

void scanLabeledStatement(const LabeledStatement& ls, Facts& f) {
    if (ls.body) scanStmt(*ls.body, f);
}

void markExportedVariables(const VariableDeclaration& vd, Facts& f) {
    for (const auto& d : vd.declarations) {
        if (d && d->id && d->id->kind == NodeKind::Identifier) {
            f.escaped.insert(static_cast<const Identifier&>(*d->id).name);
        }
    }
}

void scanExportDeclaration(const ExportDeclaration& ed, Facts& f) {
    for (const auto& spec : ed.specifiers) {
        if (spec && !spec->local.empty()) {
            // Another module can mutate an exported array.
            f.escaped.insert(spec->local);
        }
    }
    if (ed.declaration) {
        if (ed.declaration->kind == NodeKind::VariableDeclaration) {
            markExportedVariables(
                static_cast<const VariableDeclaration&>(*ed.declaration), f);
        }
        scanStmt(*ed.declaration, f);
    }
    if (ed.defaultExpr) scanExpr(*ed.defaultExpr, f);
}

void scanForEachStmtNode(const Stmt& st, Facts& f) {
    const bool isOf = st.kind == NodeKind::ForOfStatement;
    const Node* left = isOf
        ? static_cast<const ForOfStatement&>(st).left.get()
        : static_cast<const ForInStatement&>(st).left.get();
    const Expr* right = isOf
        ? static_cast<const ForOfStatement&>(st).right.get()
        : static_cast<const ForInStatement&>(st).right.get();
    const Stmt* body = isOf
        ? static_cast<const ForOfStatement&>(st).body.get()
        : static_cast<const ForInStatement&>(st).body.get();
    scanForEachStatement(left, right, body, f);
}

void scanExpressionStatement(const ExpressionStatement& es, Facts& f) {
    if (es.expression) scanExpr(*es.expression, f);
}

void scanJumpArgument(const ExprPtr& arg, Facts& f) {
    if (arg) scanExpr(*arg, f);
}

void scanStmt(const Stmt& st, Facts& f) {
    switch (st.kind) {
        case NodeKind::EmptyStatement:
        case NodeKind::BreakStatement:
        case NodeKind::ContinueStatement:
            return;
        case NodeKind::ExpressionStatement:
            scanExpressionStatement(
                static_cast<const ExpressionStatement&>(st), f);
            return;
        case NodeKind::BlockStatement:
            scanBlock(static_cast<const BlockStatement&>(st), f);
            return;
        case NodeKind::VariableDeclaration:
            scanVariableDeclaration(static_cast<const VariableDeclaration&>(st), f);
            return;
        case NodeKind::FunctionDeclaration:
            scanFunction(static_cast<const FunctionDeclaration&>(st), f);
            return;
        case NodeKind::ClassDeclaration:
            // Methods can reach a unit local; class bodies are not walked.
            f.complete = false;
            return;
        case NodeKind::InterfaceDeclaration:
        case NodeKind::TypeAliasDeclaration:
            return;
        case NodeKind::EnumDeclaration:
            scanEnumDeclaration(static_cast<const EnumDeclaration&>(st), f);
            return;
        case NodeKind::IfStatement:
            scanIfStatement(static_cast<const IfStatement&>(st), f);
            return;
        case NodeKind::WhileStatement:
            scanWhileStatement(static_cast<const WhileStatement&>(st), f);
            return;
        case NodeKind::DoWhileStatement:
            scanDoWhileStatement(static_cast<const DoWhileStatement&>(st), f);
            return;
        case NodeKind::ForStatement:
            scanForStatement(static_cast<const ForStatement&>(st), f);
            return;
        case NodeKind::ForOfStatement:
        case NodeKind::ForInStatement:
            scanForEachStmtNode(st, f);
            return;
        case NodeKind::SwitchStatement:
            scanSwitchStatement(static_cast<const SwitchStatement&>(st), f);
            return;
        case NodeKind::ReturnStatement:
            scanJumpArgument(static_cast<const ReturnStatement&>(st).argument, f);
            return;
        case NodeKind::ThrowStatement:
            scanJumpArgument(static_cast<const ThrowStatement&>(st).argument, f);
            return;
        case NodeKind::TryStatement:
            scanTryStatement(static_cast<const TryStatement&>(st), f);
            return;
        case NodeKind::LabeledStatement:
            scanLabeledStatement(static_cast<const LabeledStatement&>(st), f);
            return;
        case NodeKind::ImportDeclaration:
            return;
        case NodeKind::ExportDeclaration:
            scanExportDeclaration(static_cast<const ExportDeclaration&>(st), f);
            return;
        default:
            f.complete = false;
            return;
    }
}

struct Candidate {
    const VariableDeclarator* d = nullptr;
    const IndexExpr* ix = nullptr;
    std::string arr;
    std::string name;
    // The frame the candidate is declared in.
    int depth = 0;
};

std::optional<Candidate> findCandidate(const BlockStatement& block,
                                       std::size_t index, const Facts& facts,
                                       int depth) {
    const Stmt* stmt = block.body[index].get();
    if (!stmt || stmt->kind != NodeKind::VariableDeclaration) {
        return std::nullopt;
    }
    const auto& vd = static_cast<const VariableDeclaration&>(*stmt);
    // Only a `const` binding becomes a reference. A `let` binding is a
    // mutable value in the ordinary lowering, and a `const E&` cannot be
    // passed to a `ref` parameter (`E&`), so a `let` candidate would turn a
    // program that compiles into one that does not. A `const E` value and a
    // `const E&` reference are interchangeable at every use site.
    if (vd.kind != VarKind::Const || vd.declarations.size() != 1) {
        return std::nullopt;
    }
    const auto& d = vd.declarations.front();
    if (!d || !d->id || d->id->kind != NodeKind::Identifier || !d->init ||
        d->init->kind != NodeKind::IndexExpr) {
        return std::nullopt;
    }
    // A declared type is left to the ordinary lowering: it carries the
    // type-error report for a type with no C++ spelling, and a wider
    // annotation (`string | undefined`) is a different C++ type than the
    // element. The reference spelling is only used for an inferred binding.
    if (d->type) return std::nullopt;
    const auto& ix = static_cast<const IndexExpr&>(*d->init);
    if (!ix.object || ix.object->kind != NodeKind::Identifier || !ix.index) {
        return std::nullopt;
    }
    const std::string& name = static_cast<const Identifier&>(*d->id).name;
    const std::string& arr = static_cast<const Identifier&>(*ix.object).name;
    if (name == arr) return std::nullopt;
    const auto decl = facts.decls.find(name);
    if (decl == facts.decls.end() || decl->second != 1) return std::nullopt;
    if (facts.written.count(name) != 0) return std::nullopt;
    if (facts.rootWritten.count(name) != 0) return std::nullopt;
    // The binding must be declared in this frame, and it must be read only
    // in this frame: a read in a deeper frame is a capture, and a capture can
    // outlive the array the reference points into.
    const auto declFrame = facts.declFrame.find(name);
    if (declFrame == facts.declFrame.end() || declFrame->second != depth) {
        return std::nullopt;
    }
    const auto reads = facts.reads.find(name);
    if (reads != facts.reads.end() &&
        (reads->second.min != depth || reads->second.max != depth)) {
        return std::nullopt;
    }
    Candidate c;
    c.d = d.get();
    c.ix = &ix;
    c.arr = arr;
    c.name = name;
    c.depth = depth;
    return c;
}

bool candidateFactsSafe(const Candidate& c, const Facts& facts) {
    const auto arrDecl = facts.decls.find(c.arr);
    if (arrDecl == facts.decls.end() || arrDecl->second != 1) return false;
    if (facts.freshArray.count(c.arr) == 0) return false;
    if (facts.mutated.count(c.arr) != 0) return false;
    if (facts.escaped.count(c.arr) != 0) return false;
    if (facts.written.count(c.arr) != 0) return false;
    // The array must outlive the reference. A namespace-scope global outlives
    // every frame in the unit; otherwise the array must live in the same frame
    // as the candidate, so that its scope encloses the candidate's scope. An
    // array in an outer frame could die while the candidate's frame lives on
    // (a nested function kept past the outer call), so that case is rejected.
    if (facts.globals.count(c.arr) == 0) {
        const auto arrFrame = facts.declFrame.find(c.arr);
        if (arrFrame == facts.declFrame.end() || arrFrame->second != c.depth) {
            return false;
        }
    }
    return true;
}

bool candidateTypeSafe(const Type& arrType) {
    if (arrType.kind != TypeKind::Array) return false;
    CppInfo ec;
    if (!arrayElementInfo(arrayElemOf(arrType), ec) || !ec.representable) {
        return false;
    }
    return isScalarElement(ec.shape);
}

}  // namespace

void Generator::analyzeArrayRefs(const Program& program) {
    arrayRefDecls_.clear();
    Facts facts;
    for (const auto& stmt : program.body) {
        if (stmt) scanStmt(*stmt, facts);
    }
    // Complete-or-nothing: a construct the walk does not model may mutate or
    // alias any array, so no candidate is safe.
    if (!facts.complete) return;

    auto arrayType = [this, &facts](const Candidate& c) {
        Type type = effectiveType(*c.ix->object);
        if (type.kind == TypeKind::Array) return type;
        const auto site = facts.declSites.find(c.arr);
        if (site == facts.declSites.end() || site->second == nullptr) {
            return type;
        }
        if (const auto declared = resolvedBindingType(*site->second)) {
            return *declared;
        }
        return type;
    };

    // Every block the walk saw, with the frame it sits in.
    for (std::size_t b = 0; b < facts.blocks.size(); ++b) {
        const BlockStatement* block = facts.blocks[b];
        for (std::size_t i = 0; i < block->body.size(); ++i) {
            const auto cand = findCandidate(*block, i, facts, facts.blockFrames[b]);
            if (!cand) continue;
            if (!candidateFactsSafe(*cand, facts)) continue;
            if (candidateTypeSafe(arrayType(*cand))) {
                arrayRefDecls_.insert(cand->d->nodeId);
            }
        }
    }
}

}  // namespace cp3
