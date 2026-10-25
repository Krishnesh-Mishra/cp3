// =============================================================================
// src/std/collections/codegen_map.cpp - the fused `Map` update lowering.
//
// A `const v = m.get(k) ?? d;` declaration immediately followed by
// `m.set(k, value)` costs two probes and two key evaluations. When the key,
// the receiver, the fallback and the value are pure reads, the pair lowers to
// one probe on a hit plus an in-place store. The proof, the shape check and
// the emitted form live here; the `Map`/`Set` surface itself is in
// src/std/collections/codegen.cpp.
// =============================================================================
#include "detail/generator/generator.h"

#include <cstddef>
#include <functional>
#include <string>

#include "detail/generator/cpp_writer.h"
#include "detail/generator/mangle.h"
#include "detail/generator/type_map.h"

namespace cp3 {

using namespace detail;

namespace {

// A type the template formatter can print without a user callback. A class,
// array, object or unknown type fails closed: `Maybe<Class>` and friends could
// reach a user `toString`, which would be an effect the fusion cannot prove.
bool formatSafe(const Type& t) {
    switch (t.kind) {
        case TypeKind::Number:
        case TypeKind::String:
        case TypeKind::Boolean:
        case TypeKind::Undefined:
        case TypeKind::Null:
        case TypeKind::Enum:
            return true;
        case TypeKind::Literal:
            return t.literalBase == TypeKind::Number ||
                   t.literalBase == TypeKind::String ||
                   t.literalBase == TypeKind::Boolean;
        case TypeKind::Nullable:
        case TypeKind::Union: {
            for (const Type& part : t.parts) {
                if (!formatSafe(part)) return false;
            }
            return true;
        }
        default:
            return false;
    }
}

// A pure read: evaluating its text cannot call user code, mutate state, or
// yield a different value on a second evaluation (calls, assignments, updates,
// `delete`, `await`, closures and object/array construction are out). This is
// what lets the fused update evaluate the key once instead of once per `get`
// and once per `set`, and what keeps the cached slot valid across the value.
//
// Member reads are the one construct that needs a proof beyond the syntax: a
// class accessor or `toString` could run user code between the probe and the
// store. Only the two properties codegen lowers to a plain runtime size read
// are accepted: `Map`/`Set` `.size` and `Array`/`string` `.length`. Every other
// member read fails closed. Index reads and template formatting lower to
// runtime helpers over primitive data; their operand types are checked here.
template <class TypeOf>
bool repeatable(const Expr& e, const TypeOf& typeOf) {
    switch (e.kind) {
        case NodeKind::NumberLiteral:
        case NodeKind::StringLiteral:
        case NodeKind::BoolLiteral:
        case NodeKind::NullLiteral:
        case NodeKind::UndefinedLiteral:
        case NodeKind::Identifier:
        case NodeKind::ThisExpr:
            return true;
        case NodeKind::TemplateLiteral: {
            const auto& tl = static_cast<const TemplateLiteral&>(e);
            for (const auto& part : tl.expressions) {
                if (!part || !repeatable(*part, typeOf) ||
                    !formatSafe(typeOf(*part))) {
                    return false;
                }
            }
            return true;
        }
        case NodeKind::MemberExpr: {
            const auto& m = static_cast<const MemberExpr&>(e);
            if (!m.object || !repeatable(*m.object, typeOf)) return false;
            Type object = typeOf(*m.object);
            if (object.isLiteral()) object = object.literalBaseType();
            if (m.property == "size") {
                return object.kind == TypeKind::Class &&
                       (object.name == "Map" || object.name == "Set");
            }
            if (m.property == "length") {
                return object.kind == TypeKind::Array ||
                       object.kind == TypeKind::String;
            }
            return false;
        }
        case NodeKind::IndexExpr: {
            const auto& ix = static_cast<const IndexExpr&>(e);
            return ix.object && ix.index && repeatable(*ix.object, typeOf) &&
                   repeatable(*ix.index, typeOf);
        }
        case NodeKind::UnaryExpr: {
            const auto& u = static_cast<const UnaryExpr&>(e);
            return u.op != UnaryOp::Delete && u.operand &&
                   repeatable(*u.operand, typeOf);
        }
        case NodeKind::BinaryExpr: {
            const auto& b = static_cast<const BinaryExpr&>(e);
            return b.left && b.right && repeatable(*b.left, typeOf) &&
                   repeatable(*b.right, typeOf);
        }
        case NodeKind::LogicalExpr: {
            const auto& l = static_cast<const LogicalExpr&>(e);
            return l.left && l.right && repeatable(*l.left, typeOf) &&
                   repeatable(*l.right, typeOf);
        }
        case NodeKind::ConditionalExpr: {
            const auto& c = static_cast<const ConditionalExpr&>(e);
            return c.condition && c.whenTrue && c.whenFalse &&
                   repeatable(*c.condition, typeOf) &&
                   repeatable(*c.whenTrue, typeOf) &&
                   repeatable(*c.whenFalse, typeOf);
        }
        default:
            return false;
    }
}

// Does the expression read the binding `name`?
bool mentionsName(const Expr& e, const std::string& name) {
    switch (e.kind) {
        case NodeKind::Identifier:
            return static_cast<const Identifier&>(e).name == name;
        case NodeKind::TemplateLiteral: {
            for (const auto& part :
                 static_cast<const TemplateLiteral&>(e).expressions) {
                if (part && mentionsName(*part, name)) return true;
            }
            return false;
        }
        case NodeKind::MemberExpr: {
            const auto& m = static_cast<const MemberExpr&>(e);
            return m.object && mentionsName(*m.object, name);
        }
        case NodeKind::IndexExpr: {
            const auto& ix = static_cast<const IndexExpr&>(e);
            return (ix.object && mentionsName(*ix.object, name)) ||
                   (ix.index && mentionsName(*ix.index, name));
        }
        case NodeKind::UnaryExpr: {
            const auto& u = static_cast<const UnaryExpr&>(e);
            return u.operand && mentionsName(*u.operand, name);
        }
        case NodeKind::BinaryExpr: {
            const auto& b = static_cast<const BinaryExpr&>(e);
            return (b.left && mentionsName(*b.left, name)) ||
                   (b.right && mentionsName(*b.right, name));
        }
        case NodeKind::LogicalExpr: {
            const auto& l = static_cast<const LogicalExpr&>(e);
            return (l.left && mentionsName(*l.left, name)) ||
                   (l.right && mentionsName(*l.right, name));
        }
        case NodeKind::ConditionalExpr: {
            const auto& c = static_cast<const ConditionalExpr&>(e);
            return (c.condition && mentionsName(*c.condition, name)) ||
                   (c.whenTrue && mentionsName(*c.whenTrue, name)) ||
                   (c.whenFalse && mentionsName(*c.whenFalse, name));
        }
        default:
            return false;
    }
}

// The `Map.get` call at the root of a fused pattern, or nullptr.
const CallExpr* mapGetCall(const Expr& e) {
    if (e.kind != NodeKind::CallExpr) return nullptr;
    const auto& call = static_cast<const CallExpr&>(e);
    if (call.arguments.size() != 1 || !call.arguments.front() || !call.callee ||
        call.callee->kind != NodeKind::MemberExpr) {
        return nullptr;
    }
    const auto& m = static_cast<const MemberExpr&>(*call.callee);
    return m.property == "get" ? &call : nullptr;
}

// The `Map.set` call at the root of the second statement, or nullptr.
const CallExpr* mapSetCall(const Expr& e) {
    if (e.kind != NodeKind::CallExpr) return nullptr;
    const auto& call = static_cast<const CallExpr&>(e);
    if (call.arguments.size() != 2 || !call.arguments[0] ||
        !call.arguments[1] || !call.callee ||
        call.callee->kind != NodeKind::MemberExpr) {
        return nullptr;
    }
    const auto& m = static_cast<const MemberExpr&>(*call.callee);
    return m.property == "set" ? &call : nullptr;
}

bool isMapReceiver(const Type& t) {
    return t.kind == TypeKind::Class && t.name == "Map";
}

// The generator queries the fused-map shape check needs.
struct FusedMapCtx {
    std::function<bool(const Expr&)> isPure;    // a pure read
    std::function<Type(const Expr&)> typeOf;    // the effective type
    std::function<CppInfo(const Expr&)> infoOf;  // the C++ shape
};

// The parts of a fusable `m.get(k) ?? d` / `m.set(k, value)` pair.
struct FusedMapShape {
    const VariableDeclaration* decl = nullptr;
    const VariableDeclarator* declarator = nullptr;
    const Expr* fallback = nullptr;
    const Expr* value = nullptr;
    const Expr* receiver = nullptr;  // the set's receiver
    const Expr* key = nullptr;       // the set's key
    const Expr* getReceiver = nullptr;
    const Expr* getKey = nullptr;
    std::string declName;
};

// True when `body[index]` and `body[index + 1]` are a fusable pair: a
// `v = m.get(k) ?? d;` declaration immediately followed by `m.set(k, value)`
// whose value mentions `v`, with the receiver, key, fallback and value all
// pure reads and both receivers a `Map`. The caller checks the parts that
// need the generator (the emitted texts and the binding's C++ type).
bool fusedMapShape(const StmtList& body, std::size_t index,
                   const FusedMapCtx& ctx, FusedMapShape& shape) {
    if (index + 1 >= body.size()) return false;
    const Stmt* first = body[index].get();
    const Stmt* second = body[index + 1].get();
    if (!first || !second || first->kind != NodeKind::VariableDeclaration ||
        second->kind != NodeKind::ExpressionStatement) {
        return false;
    }
    const auto& vd = static_cast<const VariableDeclaration&>(*first);
    if (vd.declarations.size() != 1) return false;
    const auto& d = vd.declarations.front();
    if (!d || !d->id || d->id->kind != NodeKind::Identifier || !d->init) {
        return false;
    }
    if (d->init->kind != NodeKind::LogicalExpr) return false;
    const auto& nullish = static_cast<const LogicalExpr&>(*d->init);
    if (nullish.op != LogicalOp::Nullish || !nullish.left || !nullish.right ||
        !ctx.isPure(*nullish.right)) {
        return false;
    }
    const CallExpr* get = mapGetCall(*nullish.left);
    if (get == nullptr || !ctx.isPure(*get->arguments.front())) return false;
    const auto& getMember = static_cast<const MemberExpr&>(*get->callee);
    if (!getMember.object || !ctx.isPure(*getMember.object) ||
        !isMapReceiver(ctx.typeOf(*getMember.object))) {
        return false;
    }
    if (ctx.infoOf(*nullish.left).shape != Shape::Optional) return false;
    const auto& es = static_cast<const ExpressionStatement&>(*second);
    if (!es.expression) return false;
    const CallExpr* set = mapSetCall(*es.expression);
    if (set == nullptr || !ctx.isPure(*set->arguments[0])) return false;
    const auto& setMember = static_cast<const MemberExpr&>(*set->callee);
    if (!setMember.object || !ctx.isPure(*setMember.object) ||
        !isMapReceiver(ctx.typeOf(*setMember.object))) {
        return false;
    }
    const Expr& value = *set->arguments[1];
    const std::string& declName = static_cast<const Identifier&>(*d->id).name;
    if (!ctx.isPure(value) || !mentionsName(value, declName)) return false;
    shape.decl = &vd;
    shape.declarator = d.get();
    shape.fallback = nullish.right.get();
    shape.value = &value;
    shape.receiver = setMember.object.get();
    shape.key = set->arguments[0].get();
    shape.getReceiver = getMember.object.get();
    shape.getKey = get->arguments.front().get();
    shape.declName = declName;
    return true;
}

// A suffix for the fused form's generated names that none of the fused
// expressions mention. The lambda opens a new C++ scope, so a user binding
// that happens to share a generated name is shadowed, not redeclared; the
// suffix keeps that shadowing unobservable.
std::string uniqueFusedSuffix(const std::string& texts, int& counter) {
    for (;;) {
        const std::string suffix = std::to_string(counter++);
        if (texts.find("cp3_map" + suffix) == std::string::npos &&
            texts.find("cp3_key" + suffix) == std::string::npos &&
            texts.find("cp3_slot" + suffix) == std::string::npos &&
            texts.find("cp3_value" + suffix) == std::string::npos) {
            return suffix;
        }
    }
}

}  // namespace

// WS2.8: the fused update. A `const v = m.get(k) ?? d;` declaration directly
// followed by `m.set(k, <value mentioning v>)` costs two probes and two key
// evaluations. When the key, the receiver, the fallback and the value are pure
// (see `repeatable`), the pair lowers to one probe on a hit plus an in-place
// store:
//
//   const V v = ([&](auto&& cp3_mapN) -> V {
//       const auto cp3_keyN = (k);             // key, evaluated once
//       auto* cp3_slotN = cp3_mapN->findRef(cp3_keyN);
//       const V v = cp3_slotN ? *cp3_slotN : (d);  // d only on a miss
//       const V cp3_valueN = (value);          // value before any mutation
//       if (cp3_slotN == nullptr)
//           cp3_slotN = &cp3_mapN->getOrInsert(cp3_keyN, v);
//       *cp3_slotN = cp3_valueN;
//       return v;
//   })(m);
//
// The key is materialized as a value, not a reference. A key expression such
// as a chained `std::string("").append(...)` returns a reference to a
// temporary; binding that result to `const auto&` would not extend the
// temporary's lifetime, and the key would dangle before `findRef` uses it.
//
// The order is the unfused order: receiver, key, probe, fallback on a miss,
// value, then the store. `m.get(k) ?? d` short-circuits, so the fallback must
// not run on a hit. The original evaluates the value while the map is still in
// its pre-set state, so no insertion may happen before the value. The copy
// keeps every later read of `v` on the old value, exactly like the unfused
// form.
//
// The proof, in order: the receiver and key are pure reads, so evaluating each
// once yields what the original's two evaluations yield (and nothing can change
// the binding between them). The probe has no user code. The fallback is a pure
// read and runs only on a miss, exactly where `??` runs it. The value is a pure
// read, so it cannot insert, delete, clear, alias or re-enter the map; the
// cached slot therefore cannot be invalidated, and the map is still in its
// pre-set state while the value runs. On a miss the insertion happens at the
// set point with the same key and the fallback-derived `v`, and the store
// follows; on a hit no insertion happens and the store is the same in-place
// update `set` performs. Only a copyable primitive value can take this path
// (Map<K, V> classifies V as a primitive), otherwise the pair is left unfused.
// The immediately invoked lambda inlines to the same order. It opens a new C++
// scope, so a user binding with a generated name is shadowed, not redeclared;
// the suffix is chosen so none of the receiver, key, fallback or value mentions
// that name. The receiver is bound by reference, not copied (a per-iteration
// shared-handle copy measured on the word-frequency hit path). The fused form
// copies the value twice (the outer `v` and the value temporary), so a
// non-copyable value stays unfused.
// Returns true and consumes both statements when it emitted the fused form.
bool Generator::emitFusedMapUpdate(const StmtList& body, std::size_t& index,
                                   std::string& out, int indent) {
    if (index + 1 >= body.size()) return false;
    // The int64 fast path owns the number lowering inside its own region: a
    // fused `double` binding would contradict the region's int64 assumption
    // for the same name, so the region keeps the ordinary lowering.
    if (intOn()) return false;
    // The int64 fast path (support/top_int64.cpp) rewrites whole statements
    // and marks them so the ordinary walker skips them. The fused form
    // REPLACES the pair it consumes, so it must refuse a pair that touches
    // an int64-owned statement: emitting it would drop the int64 lowering.
    if (int64SkipStmts_.count(body[index]->nodeId) != 0) return false;
    if (int64SkipStmts_.count(body[index + 1]->nodeId) != 0) return false;
    FusedMapCtx ctx;
    const auto typeOf = [this](const Expr& e) { return effectiveType(e); };
    ctx.typeOf = typeOf;
    ctx.infoOf = [this](const Expr& e) { return info(e); };
    ctx.isPure = [this, &typeOf](const Expr& e) {
        return repeatable(e, typeOf);
    };
    FusedMapShape shape;
    if (!fusedMapShape(body, index, ctx, shape)) return false;
    // A captured-and-reassigned local has the shared `Cell<T>` representation
    // (the frame reads it through `get()` and writes it through `set()`); the
    // fused form declares a plain value, so the cell keeps the ordinary
    // lowering.
    if (isCell(shape.declName)) return false;
    const std::string obj = expr(*shape.receiver);
    const std::string key = expr(*shape.key);
    if (obj != expr(*shape.getReceiver) || key != expr(*shape.getKey)) {
        return false;
    }
    const CppInfo ci = bindingCpp(*shape.declarator);
    if (!ci.representable || !isPrimitiveShape(ci.shape)) return false;
    needsCollections_ = true;
    const std::string binding = mapIdent(shape.declName);
    const std::string fallback = expr(*shape.fallback);
    const std::string valueText = expr(*shape.value);
    // The lambda opens a new C++ scope, so a user binding that happens to
    // share a generated name is shadowed, not redeclared. Pick a suffix none
    // of the expressions mention, so the shadowing cannot be observed.
    const std::string texts = obj + "\n" + key + "\n" + fallback + "\n" + valueText;
    const std::string suffix = uniqueFusedSuffix(texts, uniqueCounter_);
    const std::string mapVar = "cp3_map" + suffix;
    const std::string keyVar = "cp3_key" + suffix;
    const std::string slotVar = "cp3_slot" + suffix;
    const std::string valueVar = "cp3_value" + suffix;
    const std::string cv = shape.decl->kind == VarKind::Const ? "const " : "";
    emitLineDirective(*body[index], out);
    line(out, indent, cv + ci.cpp + " " + binding + " = ([&](auto&& " + mapVar +
                          ") -> " + ci.cpp + " {");
    line(out, indent + 1, "const auto " + keyVar + " = (" + key + ");");
    line(out, indent + 1, "auto* " + slotVar + " = " + mapVar + "->findRef(" +
                              keyVar + ");");
    line(out, indent + 1, cv + ci.cpp + " " + binding + " = " + slotVar +
                              " ? *" + slotVar + " : (" + fallback + ");");
    emitLineDirective(*body[index + 1], out);
    line(out, indent + 1,
         "const " + ci.cpp + " " + valueVar + " = (" + valueText + ");");
    line(out, indent + 1, "if (" + slotVar + " == nullptr) " + slotVar +
                              " = &" + mapVar + "->getOrInsert(" + keyVar + ", " +
                              binding + ");");
    line(out, indent + 1, "*" + slotVar + " = " + valueVar + ";");
    line(out, indent + 1, "return " + binding + ";");
    line(out, indent, "})(" + obj + ");");
    declareBinding(*shape.declarator);
    ++index;
    return true;
}

}  // namespace cp3
