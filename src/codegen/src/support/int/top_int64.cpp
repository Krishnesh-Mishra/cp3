// =============================================================================
// src/codegen/src/support/top_int64.cpp - int64 locals in the entry body.
//
// cp3 `number` is a double, so a top-level counter or LCG state pays for
// double-domain semantics even when every value it takes is an integer a
// double represents exactly. This unit proves two local shapes over the entry
// module (which is emitted inside `main` when no declared function, class,
// export or worker can name the binding) and records them for the emitter:
//
//   R1 loop counter: `let c = c0; while (c < N) { ...; c = c + K; }` where c
//      has no other write, c0/N/K are integral constants, K > 0, c0 <= N and
//      the value after the last iteration stays within 2^53. The counter is
//      emitted as std::int64_t, the loop becomes a `for` whose header advances
//      it, and every read converts to double (exact, because the value is an
//      integer of magnitude <= 2^53). A `continue` anywhere in a loop's subtree
//      would skip the increment in the `while` form but not in the `for` form,
//      so it rejects that loop.
//   R2 bounded recurrence: `x = (x * A) % K` is x's only write, x0 is in
//      [0, K), A > 0 and A * K <= 2^53. x stays in [0, K) forever, so the
//      int64 product and remainder are the exact double results and every
//      read is exactly representable. Negative zero starts are rejected
//      because the int64 rewrite cannot preserve the sign of zero.
//
// Both proofs are whole-unit: a second declaration of the name (a shadowing
// local, an arrow parameter, a catch binding, an import), a write inside an
// arrow/function, a reference before the declaration, or any other write
// rejects the binding, so the name-keyed rewrite cannot touch a different
// variable. A construct the unit walk does not model rejects every candidate
// in the unit (complete-or-nothing, like int_reads.cpp). The analysis only
// runs when `moduleNeedsGlobals_` is false: then the module bindings are
// `main` locals and the emitted reads are all in this unit's entry body.
//
// The unit walk itself lives in support/top_int64_scan.cpp; its facts are
// declared in detail/top_int64_scan.h.
// =============================================================================
#include "detail/generator/generator.h"

#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "detail/int/top_int64_scan.h"

namespace cp3 {

using namespace detail;

namespace {

// 2^53: the largest magnitude at which every integer is a double (the same
// bound cp3/core/arith.hpp calls kIntLimit).
constexpr double kLimit = 9007199254740992.0;

bool integralInRange(double v) {
    // Reject negative zero: int64 has one zero, and `1 / -0` is observable.
    if (v == 0.0 && std::signbit(v)) return false;
    return std::isfinite(v) && v >= -kLimit && v <= kLimit &&
           std::floor(v) == v;
}

// True only when `a` ends before `b` starts in the same file. An unknown
// location fails closed: no rewrite without an order proof.
bool spanBefore(const Node& a, const Node& b) {
    if (!a.span.valid() || !b.span.valid() || a.span.file != b.span.file) {
        return false;
    }
    return a.span.begin.offset < b.span.begin.offset;
}

// The unit-wide facts the two proofs read.
struct ProofInput {
    const TopInt64Scan* scan = nullptr;
    const std::unordered_map<std::string, double>* consts = nullptr;
    const std::string* name = nullptr;
    const Node* decl = nullptr;
    std::int64_t c0 = 0;
};

// A compile-time number: a literal or an webserverion over module `const`
// bindings that already have one. Fractions are fine here; the callers demand
// an integral result.
std::optional<double> constValue(
    const Expr& e, const std::unordered_map<std::string, double>& consts) {
    switch (e.kind) {
        case NodeKind::NumberLiteral:
            return static_cast<const NumberLiteral&>(e).value;
        case NodeKind::Identifier: {
            const auto it = consts.find(static_cast<const Identifier&>(e).name);
            if (it == consts.end()) return std::nullopt;
            return it->second;
        }
        case NodeKind::UnaryExpr: {
            const auto& u = static_cast<const UnaryExpr&>(e);
            if (!u.operand) return std::nullopt;
            const auto v = constValue(*u.operand, consts);
            if (!v) return std::nullopt;
            if (u.op == UnaryOp::Negate) return -*v;
            if (u.op == UnaryOp::Plus) return *v;
            return std::nullopt;
        }
        case NodeKind::BinaryExpr: {
            const auto& b = static_cast<const BinaryExpr&>(e);
            if (!b.left || !b.right) return std::nullopt;
            const auto l = constValue(*b.left, consts);
            const auto r = constValue(*b.right, consts);
            if (!l || !r) return std::nullopt;
            double v = 0.0;
            switch (b.op) {
                case BinaryOp::Add: v = *l + *r; break;
                case BinaryOp::Subtract: v = *l - *r; break;
                case BinaryOp::Multiply: v = *l * *r; break;
                case BinaryOp::Divide:
                    if (*r == 0.0) return std::nullopt;
                    v = *l / *r;
                    break;
                default:
                    return std::nullopt;
            }
            if (!std::isfinite(v)) return std::nullopt;
            return v;
        }
        default:
            return std::nullopt;
    }
}

// `c = c + K` / `c = K + c` / `c += K`: returns K when it is a positive
// integral constant.
std::optional<std::int64_t> counterStep(
    const AssignmentExpr& a, const std::string& name,
    const std::unordered_map<std::string, double>& consts) {
    const Expr* value = a.value.get();
    if (value == nullptr) return std::nullopt;
    if (a.op == AssignOp::Add) {
        const auto k = constValue(*value, consts);
        if (k && integralInRange(*k) && *k > 0) {
            return static_cast<std::int64_t>(*k);
        }
        return std::nullopt;
    }
    if (a.op != AssignOp::Assign || value->kind != NodeKind::BinaryExpr) {
        return std::nullopt;
    }
    const auto& b = static_cast<const BinaryExpr&>(*value);
    if (b.op != BinaryOp::Add || !b.left || !b.right) return std::nullopt;
    const Identifier* id = nullptr;
    const Expr* kexpr = nullptr;
    if (b.left->kind == NodeKind::Identifier) {
        id = &static_cast<const Identifier&>(*b.left);
        kexpr = b.right.get();
    } else if (b.right->kind == NodeKind::Identifier) {
        id = &static_cast<const Identifier&>(*b.right);
        kexpr = b.left.get();
    }
    if (id == nullptr || id->name != name) return std::nullopt;
    const auto k = constValue(*kexpr, consts);
    if (!k || !integralInRange(*k) || *k <= 0) return std::nullopt;
    return static_cast<std::int64_t>(*k);
}

// `x = (x * A) % K`: returns {A, K}.
std::optional<std::pair<std::int64_t, std::int64_t>> recurrence(
    const AssignmentExpr& a, const std::string& name,
    const std::unordered_map<std::string, double>& consts) {
    if (a.op != AssignOp::Assign || !a.value ||
        a.value->kind != NodeKind::BinaryExpr) {
        return std::nullopt;
    }
    const auto& mod = static_cast<const BinaryExpr&>(*a.value);
    if (mod.op != BinaryOp::Modulo || !mod.left || !mod.right) {
        return std::nullopt;
    }
    const auto k = constValue(*mod.right, consts);
    if (!k || !integralInRange(*k) || *k <= 0) return std::nullopt;
    if (mod.left->kind != NodeKind::BinaryExpr) return std::nullopt;
    const auto& mul = static_cast<const BinaryExpr&>(*mod.left);
    if (mul.op != BinaryOp::Multiply || !mul.left || !mul.right) {
        return std::nullopt;
    }
    const Identifier* id = nullptr;
    const Expr* aexpr = nullptr;
    if (mul.left->kind == NodeKind::Identifier) {
        id = &static_cast<const Identifier&>(*mul.left);
        aexpr = mul.right.get();
    } else if (mul.right->kind == NodeKind::Identifier) {
        id = &static_cast<const Identifier&>(*mul.right);
        aexpr = mul.left.get();
    }
    if (id == nullptr || id->name != name) return std::nullopt;
    const auto av = constValue(*aexpr, consts);
    if (!av || !integralInRange(*av) || *av <= 0) return std::nullopt;
    return std::make_pair(static_cast<std::int64_t>(*av),
                          static_cast<std::int64_t>(*k));
}

// The while condition `c < N` / `c <= N` (or the mirrored forms).
struct CounterLoop {
    std::int64_t bound = 0;
    bool inclusive = false;
};

std::optional<CounterLoop> counterLoop(
    const WhileStatement& ws, const std::string& name,
    const std::unordered_map<std::string, double>& consts) {
    if (!ws.condition || ws.condition->kind != NodeKind::BinaryExpr) {
        return std::nullopt;
    }
    const auto& b = static_cast<const BinaryExpr&>(*ws.condition);
    if (!b.left || !b.right) return std::nullopt;
    const Expr* rhs = nullptr;
    CounterLoop out;
    const bool leftIsName =
        b.left->kind == NodeKind::Identifier &&
        static_cast<const Identifier&>(*b.left).name == name;
    const bool rightIsName =
        b.right->kind == NodeKind::Identifier &&
        static_cast<const Identifier&>(*b.right).name == name;
    if (leftIsName && (b.op == BinaryOp::LessThan ||
                       b.op == BinaryOp::LessThanEqual)) {
        rhs = b.right.get();
        out.inclusive = b.op == BinaryOp::LessThanEqual;
    } else if (rightIsName && (b.op == BinaryOp::GreaterThan ||
                               b.op == BinaryOp::GreaterThanEqual)) {
        rhs = b.left.get();
        out.inclusive = b.op == BinaryOp::GreaterThanEqual;
    } else {
        return std::nullopt;
    }
    const auto n = constValue(*rhs, consts);
    if (!n || !integralInRange(*n)) return std::nullopt;
    out.bound = static_cast<std::int64_t>(*n);
    return out;
}

// Module declarations in source order; `consts` maps each module `const`
// number to its compile-time value (a later one may use an earlier one).
void collectModuleDecls(const Program& program,
                        std::vector<const VariableDeclaration*>& moduleDecls,
                        std::unordered_map<std::string, double>& consts) {
    for (const auto& stmt : program.body) {
        if (!stmt || stmt->kind != NodeKind::VariableDeclaration) continue;
        const auto& vd = static_cast<const VariableDeclaration&>(*stmt);
        moduleDecls.push_back(&vd);
        if (vd.kind != VarKind::Const || vd.declarations.size() != 1) continue;
        const auto& d = vd.declarations.front();
        if (!d || !d->id || d->id->kind != NodeKind::Identifier || !d->init) {
            continue;
        }
        if (const auto v = constValue(*d->init, consts)) {
            consts[static_cast<const Identifier&>(*d->id).name] = *v;
        }
    }
}

// The binding's one and only write, or null when it is written twice, has an
// update form, is written inside a deferred frame, or is never written.
const AssignmentExpr* soleWrite(const TopInt64Scan& scan,
                                 const std::string& name) {
    const AssignmentExpr* write = nullptr;
    for (const AssignmentExpr* w : scan.writes) {
        if (!w->target || w->target->kind != NodeKind::Identifier) continue;
        if (static_cast<const Identifier&>(*w->target).name != name) continue;
        if (scan.frameWrites.count(w) != 0) return nullptr;
        if (write != nullptr) return nullptr;
        write = w;
    }
    for (const UpdateExpr* u : scan.updates) {
        if (u->operand && u->operand->kind == NodeKind::Identifier &&
            static_cast<const Identifier&>(*u->operand).name == name) {
            return nullptr;
        }
    }
    return write;
}

// R2: `x = (x * A) % K` with x0 in [0, K) and A * K <= 2^53.
bool tryRecurrence(const ProofInput& in, const AssignmentExpr& write,
                   TopInt64Binding& rec) {
    if (!spanBefore(*in.decl, write)) return false;
    const auto r = recurrence(write, *in.name, *in.consts);
    if (!r) return false;
    const std::int64_t a = r->first;
    const std::int64_t k = r->second;
    if (in.c0 < 0 || in.c0 >= k ||
        a > static_cast<std::int64_t>(kLimit) / k) {
        return false;
    }
    rec.mul = a;
    rec.mod = k;
    rec.writeNode = write.nodeId;
    return true;
}

// R1: the sole write is the last statement of a bounded while loop.
bool tryCounter(const ProofInput& in, const AssignmentExpr& write,
                TopInt64Binding& rec) {
    const auto step = counterStep(write, *in.name, *in.consts);
    if (!step) return false;
    for (const WhileStatement* ws : in.scan->whiles) {
        if (!spanBefore(*in.decl, *ws)) continue;
        if (!ws->body || ws->body->kind != NodeKind::BlockStatement) {
            continue;
        }
        const auto& body = static_cast<const BlockStatement&>(*ws->body);
        if (body.body.size() < 2) continue;
        const Stmt* last = body.body.back().get();
        if (!last || last->kind != NodeKind::webserverionStatement ||
            !static_cast<const webserverionStatement&>(*last).webserverion ||
            static_cast<const webserverionStatement&>(*last)
                    .webserverion->nodeId != write.nodeId) {
            continue;
        }
        if (in.scan->continued.count(ws) != 0) continue;
        const auto loop = counterLoop(*ws, *in.name, *in.consts);
        if (!loop || in.c0 > loop->bound) continue;
        // Every value the counter takes inside the loop, and the one it holds
        // on exit (first value past the bound), stays exact.
        const std::int64_t lastValue =
            loop->inclusive ? loop->bound : loop->bound - 1;
        if (lastValue + *step > static_cast<std::int64_t>(kLimit)) {
            continue;
        }
        rec.counter = true;
        rec.inclusive = loop->inclusive;
        rec.bound = loop->bound;
        rec.step = *step;
        rec.loopNode = ws->nodeId;
        rec.incrementNode = last->nodeId;
        return true;
    }
    return false;
}

// The earliest top-level declaration offset of every identifier binding.
// `complete` turns false on a declaration this walk cannot place: a
// destructuring pattern (its bound names are not modelled here) or a missing
// span. The caller then keeps the namespace-scope lowering.
std::unordered_map<std::string, std::uint32_t> moduleDeclOffsets(
    const Program& program, bool& complete) {
    std::unordered_map<std::string, std::uint32_t> offsets;
    complete = true;
    for (const auto& stmt : program.body) {
        if (!stmt || stmt->kind != NodeKind::VariableDeclaration) continue;
        const auto& vd = static_cast<const VariableDeclaration&>(*stmt);
        for (const auto& d : vd.declarations) {
            if (!d || !d->id || d->id->kind != NodeKind::Identifier ||
                !d->span.valid()) {
                complete = false;
                continue;
            }
            const std::string& name =
                static_cast<const Identifier&>(*d->id).name;
            const std::uint32_t offset = d->span.begin.offset;
            const auto it = offsets.find(name);
            if (it == offsets.end() || offset < it->second) {
                offsets[name] = offset;
            }
        }
    }
    return offsets;
}

// The alias of a proven binding: a fresh name that no source identifier uses.
std::string freshAlias(std::size_t& counter,
                       const std::unordered_set<std::string>& idents) {
    std::string name;
    do {
        name = "cp3_i64v_" + std::to_string(counter++);
    } while (idents.count(name) != 0);
    return name;
}

// One module declarator: the proof for its binding, if it has one.
bool proveTopInt64Binding(const ProofInput& in, const VariableDeclarator& d,
                          std::size_t& aliasCounter, TopInt64Binding& rec) {
    const std::string& name = static_cast<const Identifier&>(*d.id).name;
    // A name declared twice anywhere (a shadow, a parameter, a catch
    // binding) makes the name-keyed rewrite unsound.
    const auto bind = in.scan->bindings.find(name);
    if (bind == in.scan->bindings.end() || bind->second != 1) return false;
    // A reference before the declaration means the local spelling would not
    // be visible there.
    const auto ref = in.scan->firstRef.find(name);
    if (ref != in.scan->firstRef.end() && d.span.valid() &&
        ref->second < d.span.begin.offset) {
        return false;
    }
    const auto start = constValue(*d.init, *in.consts);
    if (!start || !integralInRange(*start)) return false;
    const AssignmentExpr* write = soleWrite(*in.scan, name);
    if (write == nullptr) return false;

    rec.alias = freshAlias(aliasCounter, in.scan->idents);
    rec.declNode = d.nodeId;
    rec.start = static_cast<std::int64_t>(*start);
    ProofInput local = in;
    local.c0 = rec.start;
    // R2 first: it needs no loop at all.
    return tryRecurrence(local, *write, rec) || tryCounter(local, *write, rec);
}

}  // namespace

// True when a top-level module binding is referenced before its declaration:
// then the unit keeps the namespace-scope lowering (where the declaration is
// hoisted out of source order), instead of producing a `main`-local that C++
// cannot see yet.
bool Generator::moduleUsesBeforeDecl(const Program& program) const {
    bool complete = true;
    const auto offsets = moduleDeclOffsets(program, complete);
    if (!complete) return true;  // fail closed: cannot place every binding
    if (offsets.empty()) return false;
    TopInt64Scan scan;
    scanTopInt64Unit(program, scan);
    if (!scan.complete) return true;  // fail closed
    for (const auto& [name, offset] : offsets) {
        const auto ref = scan.firstRef.find(name);
        if (ref != scan.firstRef.end() && ref->second < offset) return true;
    }
    return false;
}

// The entry-body int64 proof: it records the bindings it can prove and the
// statements the emitter must consume.
void Generator::analyzeTopInt64(const Program& program) {
    if (moduleNeedsGlobals_ || programMode_ || scaleMode_) return;

    std::unordered_map<std::string, double> consts;
    std::vector<const VariableDeclaration*> moduleDecls;
    collectModuleDecls(program, moduleDecls, consts);

    TopInt64Scan scan;
    scanTopInt64Unit(program, scan);
    if (!scan.complete) return;

    // A module const whose name is bound more than once anywhere in the unit
    // (a block shadow, a parameter, a function) cannot be resolved by name at
    // an arbitrary use site. Fail closed on those.
    for (auto it = consts.begin(); it != consts.end();) {
        const auto bind = scan.bindings.find(it->first);
        if (bind == scan.bindings.end() || bind->second != 1) {
            it = consts.erase(it);
        } else {
            ++it;
        }
    }

    std::size_t aliasCounter = 0;
    for (const VariableDeclaration* vd : moduleDecls) {
        if (vd->kind == VarKind::Const) continue;
        for (const auto& d : vd->declarations) {
            if (!d || !d->id || d->id->kind != NodeKind::Identifier ||
                !d->init) {
                continue;
            }
            TopInt64Binding rec;
            ProofInput in;
            in.scan = &scan;
            in.consts = &consts;
            in.name = &static_cast<const Identifier&>(*d->id).name;
            in.decl = d.get();
            if (proveTopInt64Binding(in, *d, aliasCounter, rec)) {
                topInt64Bindings_[static_cast<const Identifier&>(*d->id).name] =
                    rec;
            }
        }
    }

    for (const auto& [name, rec] : topInt64Bindings_) {
        (void)name;
        if (rec.counter) {
            int64Loops_[rec.loopNode] = &rec;
            int64SkipStmts_.insert(rec.incrementNode);
        } else {
            int64Writes_[rec.writeNode] = &rec;
        }
    }
}

}  // namespace cp3
