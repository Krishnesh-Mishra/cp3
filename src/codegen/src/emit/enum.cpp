// =============================================================================
// src/codegen/src/emit/enum.cpp - enum declaration + member lowering (spec 04).
//
// Three concerns:
//   * `collectEnums`: resolves every top-level (including `export`-wrapped)
//     enum declaration into an `EnumCppInfo` (detail/enums.h), builds the
//     `enum class <name> : long long { ... }` text - explicit numbers,
//     auto-increment for an initializer-less numeric member, first-occurrence
//     indices for string-backed members (TS identity) - plus the string-backed
//     `cp3_enum_<name>_toString` helper, and prepends it to the decls section;
//   * `emitEnumMember`: `Color.Green` -> `Color::Green`, and the gated object
//     field read over the tuple an enum-carrying object literal lowered to;
//   * `lowerEnumSwitch`: a switch whose discriminant and every case test are
//     the same enum lowers to a plain C++ switch (any other switch is lowered
//     by emit/control.cpp);
//   * `classifyEnum` + `typeOfNode`: the enum type mapping and the Class->Enum
//     rewrite of a type annotation (the enum arms type_map.cpp delegates here).
// =============================================================================
#include "detail/enums.h"
#include "detail/generator.h"

#include <cmath>
#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "cp3/diagnostic_codes.hpp"
#include "detail/cpp_writer.h"
#include "detail/mangle.h"
#include "detail/type_map.h"

namespace cp3 {

using namespace detail;

namespace {

// The compile-time value of an enum member initializer: a number or a string
// (spec 04). The sema accepts exactly the initializers this evaluator can
// evaluate (src/sema/src/checks/enum_check.cpp keeps the two in step: it
// rejects a non-constant initializer before codegen runs), so a nullopt here
// means the initializer slipped through and is reported as an error.
struct EnumConst {
    bool isNumber = false;
    double number = 0.0;
    std::string text;
};

// The compile-time value of an enum member initializer, or nullopt when the
// initializer is not a constant expression.
std::optional<EnumConst> evalEnumInit(const Expr* init,
                                      const std::vector<EnumCppMember>& known,
                                      const std::string& enumName);

// The member of `known` with this name, or nullptr. A linked member
// initializer (`B = A`) reads the value of an earlier member.
const EnumCppMember* linkedMember(const std::vector<EnumCppMember>& known,
                                   const std::string& name) {
    for (const EnumCppMember& m : known) {
        if (m.name == name) return &m;
    }
    return nullptr;
}

// The linked member an initializer names, or nullptr: a bare name (`A`) or the
// enum's own name with a member (`Color.A`).
const EnumCppMember* linkedTarget(const Expr* e,
                                  const std::vector<EnumCppMember>& known,
                                  const std::string& enumName) {
    if (e == nullptr) return nullptr;
    if (e->kind == NodeKind::Identifier) {
        return linkedMember(known, static_cast<const Identifier&>(*e).name);
    }
    if (e->kind != NodeKind::MemberExpr) return nullptr;
    const auto& m = static_cast<const MemberExpr&>(*e);
    if (!m.object || m.object->kind != NodeKind::Identifier) return nullptr;
    if (static_cast<const Identifier&>(*m.object).name != enumName) return nullptr;
    return linkedMember(known, m.property);
}

// A unary sign or bit-not over a number constant.
std::optional<EnumConst> evalEnumUnary(
    const UnaryExpr& u, const std::vector<EnumCppMember>& known,
    const std::string& enumName) {
    if (u.op != UnaryOp::Negate && u.op != UnaryOp::Plus &&
        u.op != UnaryOp::BitNot) {
        return std::nullopt;
    }
    const std::optional<EnumConst> operand =
        evalEnumInit(u.operand.get(), known, enumName);
    if (!operand || !operand->isNumber) return std::nullopt;
    EnumConst value;
    value.isNumber = true;
    if (u.op == UnaryOp::Negate) {
        value.number = -operand->number;
    } else if (u.op == UnaryOp::BitNot) {
        value.number =
            static_cast<double>(~static_cast<long long>(operand->number));
    } else {
        value.number = operand->number;
    }
    return value;
}

// A binary operator over two constants: `+` adds numbers or concatenates
// strings, the arithmetic operators work on numbers.
std::optional<EnumConst> evalEnumBinary(
    const BinaryExpr& b, const std::vector<EnumCppMember>& known,
    const std::string& enumName) {
    const std::optional<EnumConst> left =
        evalEnumInit(b.left.get(), known, enumName);
    const std::optional<EnumConst> right =
        evalEnumInit(b.right.get(), known, enumName);
    if (!left || !right) return std::nullopt;
    EnumConst value;
    if (b.op == BinaryOp::Add) {
        if (left->isNumber && right->isNumber) {
            value.isNumber = true;
            value.number = left->number + right->number;
            return value;
        }
        if (!left->isNumber && !right->isNumber) {
            value.text = left->text + right->text;
            return value;
        }
        return std::nullopt;
    }
    if (!left->isNumber || !right->isNumber) return std::nullopt;
    value.isNumber = true;
    switch (b.op) {
        case BinaryOp::Subtract:
            value.number = left->number - right->number;
            break;
        case BinaryOp::Multiply:
            value.number = left->number * right->number;
            break;
        case BinaryOp::Divide:
            // A division by zero has no value: the initializer is not a
            // constant the enum can carry.
            if (right->number == 0.0) return std::nullopt;
            value.number = left->number / right->number;
            break;
        case BinaryOp::Modulo:
            if (right->number == 0.0) return std::nullopt;
            value.number = std::fmod(left->number, right->number);
            break;
        default:
            return std::nullopt;
    }
    return value;
}

// The compile-time value of an enum member initializer, or nullopt when the
// initializer is not a constant expression.
std::optional<EnumConst> evalEnumInit(const Expr* init,
                                      const std::vector<EnumCppMember>& known,
                                      const std::string& enumName) {
    if (init == nullptr) return std::nullopt;
    switch (init->kind) {
        case NodeKind::NumberLiteral: {
            EnumConst value;
            value.isNumber = true;
            value.number = static_cast<const NumberLiteral&>(*init).value;
            return value;
        }
        case NodeKind::StringLiteral: {
            EnumConst value;
            value.text = static_cast<const StringLiteral&>(*init).value;
            return value;
        }
        case NodeKind::UnaryExpr:
            return evalEnumUnary(static_cast<const UnaryExpr&>(*init), known,
                                 enumName);
        case NodeKind::BinaryExpr:
            return evalEnumBinary(static_cast<const BinaryExpr&>(*init), known,
                                  enumName);
        default:
            break;
    }
    // A linked member (`B = A`): the value of the member it names.
    const EnumCppMember* target = linkedTarget(init, known, enumName);
    if (target == nullptr) return std::nullopt;
    EnumConst value;
    if (target->isString) {
        value.text = target->stringValue;
        return value;
    }
    value.isNumber = true;
    value.number = static_cast<double>(target->value);
    return value;
}

// Resolves the declaration's members in order: a string member takes the index
// of the first member with the same string (TS identity), a number member its
// constant value, and an initializer-less member the previous value + 1.
// `failed` collects the members whose initializer is not a constant the
// evaluator can read - the sema rejects those first, so a non-empty list
// means the two disagree.
EnumCppInfo buildEnumInfo(const EnumDeclaration& e,
                           std::vector<const EnumMember*>& failed) {
    EnumCppInfo info;
    info.name = e.name;
    info.cpp = mapIdent(e.name);
    info.stringBacked = e.stringBacked;

    bool previousString = false;
    long long nextNumber = 0;       // the value the next implicit member takes
    long long distinctStrings = 0;  // the ordinal of the next new string value
    for (const auto& memberPtr : e.members) {
        const EnumMember* member = memberPtr.get();
        if (member == nullptr) continue;
        EnumCppMember out;
        out.name = member->name;
        out.text = mapIdent(member->name);

        const std::optional<EnumConst> value =
            evalEnumInit(member->initializer.get(), info.members, e.name);
        if (value && value->isNumber) {
            // The backing type is `long long`; enum values are integral (a
            // fractional value has no documented value and truncates here).
            out.value = static_cast<long long>(value->number);
            nextNumber = out.value + 1;
            previousString = false;
        } else if (value) {
            // A string member takes the index of the first member with the
            // same string (TS identity).
            const EnumCppMember* twin = nullptr;
            for (const EnumCppMember& prev : info.members) {
                if (prev.isString && prev.stringValue == value->text) {
                    twin = &prev;
                }
            }
            out.isString = true;
            out.stringValue = value->text;
            if (twin != nullptr) {
                out.value = twin->value;
            } else {
                out.value = distinctStrings++;
            }
            previousString = true;
        } else if (member->initializer == nullptr) {
            // Initializer-less: the previous value + 1 for a numeric enum; a
            // string-backed member's successor is a sema error already, so the
            // distinct-string ordinal keeps the generated C++ valid.
            if (previousString) {
                out.value = distinctStrings++;
            } else {
                out.value = nextNumber;
                nextNumber = out.value + 1;
            }
        } else {
            // Not a constant the evaluator can read: the sema rejected it, so
            // this is only the fail-closed fallback.
            failed.push_back(member);
        }
        info.members.push_back(std::move(out));
    }
    return info;
}

// The `toString` helper of a string-backed enum (D19): one case per distinct
// value; the first member of a shared string names the case.
std::string enumToStringText(const EnumCppInfo& info) {
    std::string out = "inline std::string cp3_enum_" + info.cpp + "_toString(" +
                      info.cpp + " v) {\n    switch (v) {";
    std::vector<long long> seen;
    for (const EnumCppMember& m : info.members) {
        if (!m.isString) continue;
        bool duplicate = false;
        for (long long value : seen) {
            if (value == m.value) duplicate = true;
        }
        if (duplicate) continue;
        seen.push_back(m.value);
        out += "\n        case " + info.cpp + "::" + m.text + ": return " +
               cppStringLiteral(m.stringValue) + ";";
    }
    out += "\n    }\n    return std::string();\n}\n";
    return out;
}

// The full C++ text of one enum declaration at namespace scope.
std::string enumDeclText(const EnumCppInfo& info) {
    std::string out = "enum class " + info.cpp + " : long long {";
    for (const EnumCppMember& m : info.members) {
        out += "\n    " + m.text + " = " + std::to_string(m.value) + ",";
    }
    out += "\n};\n";
    if (info.stringBacked) out += enumToStringText(info);
    return out;
}

// The enum declaration a top-level statement (possibly `export`-wrapped) is.
const EnumDeclaration* topLevelEnum(const Stmt& s) {
    if (s.kind == NodeKind::EnumDeclaration) {
        return &static_cast<const EnumDeclaration&>(s);
    }
    if (s.kind == NodeKind::ExportDeclaration) {
        const auto& ed = static_cast<const ExportDeclaration&>(s);
        if (ed.declaration && ed.declaration->kind == NodeKind::EnumDeclaration) {
            return static_cast<const EnumDeclaration*>(ed.declaration.get());
        }
    }
    return nullptr;
}

}  // namespace

// =============================================================================
// Declaration collection
// =============================================================================
void Generator::collectEnums(const Program& program) {
    const auto collect = [this](const EnumDeclaration* e) {
        if (e == nullptr || e->name.empty() || enums_.count(e->name) != 0) return;
        std::vector<const EnumMember*> failed;
        EnumCppInfo info = buildEnumInfo(*e, failed);
        // A member whose initializer is not a constant the evaluator can read
        // is an error the sema reports first; the codegen repeats it so a
        // unit that reached codegen without the sema gate still fails.
        for (const EnumMember* member : failed) {
            sink_.error(diagcode::kTypeMismatch, member->span,
                        "type mismatch: enum member '" + member->name +
                            "' must be initialized with a constant number or "
                            "string expression");
        }
        enumDecls_ += enumDeclText(info) + "\n";
        enums_.emplace(e->name, std::move(info));
    };
    for (const auto& stmtPtr : program.body) {
        if (!stmtPtr) continue;
        collect(topLevelEnum(*stmtPtr));
    }
    // Imported local enums are compile-time declarations: each unit re-emits
    // them in its own namespace so a payload annotation can name the enum and
    // the main side can spell a member value. Same-named enums on both sides of
    // a worker channel are structurally compatible (workerPayloadAccepts).
    for (const EnumDeclaration* e : options_.importedEnums) collect(e);
}

// =============================================================================
// Member access
// =============================================================================
std::string Generator::emitEnumMember(const MemberExpr& m) {
    if (!m.object) return std::string();
    // `Color.Green`: the receiver is the enum's name (not a value binding in
    // scope; a local shadowing the name keeps its own lowering).
    if (m.object->kind == NodeKind::Identifier) {
        const std::string& name = static_cast<const Identifier&>(*m.object).name;
        if (!lookup(name)) {
            const auto found = enums_.find(name);
            if (found != enums_.end()) {
                const EnumCppMember* member = found->second.member(m.property);
                // An unknown member is a sema error (CP3305); the generic
                // property-access report keeps the unit a skip.
                if (member == nullptr) return std::string();
                return found->second.cpp + "::" + member->text;
            }
        }
    }
    // The gated object-field read: `o.c` over an object literal that lowered to
    // a tuple because one of its fields is an enum (emitEnumObjectLiteral).
    const Type recv = effectiveType(*m.object);
    if (recv.kind != TypeKind::Object) return std::string();
    const auto fields = enumObjectFields_.find(recv.name);
    if (fields == enumObjectFields_.end()) return std::string();
    for (std::size_t i = 0; i < fields->second.size(); ++i) {
        if (fields->second[i] == m.property) {
            return "std::get<" + std::to_string(i) + ">(" + expr(*m.object) + ")";
        }
    }
    return std::string();
}

// =============================================================================
// Object literals carrying enum fields
// =============================================================================
// A plain object literal with at least one enum-typed field lowers to a
// `std::tuple` (the sema label keeps the field order); a read of a field is
// `std::get<i>(...)`. The gate is deliberately narrow: all keys are identifiers
// or string literals, no computed/method/spread property, every field type is
// representable, and no duplicate names. Anything else keeps the old CP3002.
std::string Generator::emitEnumObjectLiteral(const ObjectLiteral& ol) {
    std::vector<std::string> names;
    std::vector<const Expr*> values;
    bool sawEnum = false;
    for (const auto& propertyPtr : ol.properties) {
        const ObjectProperty* p = propertyPtr.get();
        if (p == nullptr || p->computed || p->isMethod || !p->key || !p->value ||
            p->value->kind == NodeKind::SpreadElement) {
            return std::string();
        }
        std::string name;
        if (p->key->kind == NodeKind::Identifier) {
            name = static_cast<const Identifier&>(*p->key).name;
        } else if (p->key->kind == NodeKind::StringLiteral) {
            name = static_cast<const StringLiteral&>(*p->key).value;
        } else {
            return std::string();
        }
        const CppInfo field = info(*p->value);
        if (!field.representable || field.shape == Shape::Void) {
            return std::string();
        }
        if (field.shape == Shape::Enum) sawEnum = true;
        for (const std::string& existing : names) {
            if (existing == name) return std::string();
        }
        names.push_back(std::move(name));
        values.push_back(p->value.get());
    }
    if (!sawEnum) return std::string();

    std::string out = "std::tuple<";
    for (std::size_t i = 0; i < values.size(); ++i) {
        out += (i == 0 ? "" : ", ") + info(*values[i]).cpp;
    }
    out += ">{";
    for (std::size_t i = 0; i < values.size(); ++i) {
        out += (i == 0 ? "" : ", ") + expr(*values[i]);
    }
    out += "}";

    // The sema object label keys the field read once the binding is typed.
    const Type objType = effectiveType(ol);
    if (objType.kind == TypeKind::Object && !objType.name.empty()) {
        enumObjectFields_[objType.name] = std::move(names);
    }
    needsTuple_ = true;
    return out;
}

// =============================================================================
// Switch statements
// =============================================================================
// A switch statement: the same-enum lowering when it applies, otherwise the
// generic CP3002 statement stub (the path `stmt`'s fallback uses).
// Lowers `sw` when its discriminant and every case test are one enum; returns
// false (emitting nothing) for any other switch shape.
bool Generator::lowerEnumSwitch(const SwitchStatement& sw, std::string& out,
                                int indent) {
    if (!sw.discriminant) return false;
    const Type disc = effectiveType(*sw.discriminant);
    if (disc.kind != TypeKind::Enum) return false;
    const auto found = enums_.find(disc.name);
    if (found == enums_.end()) return false;
    const EnumCppInfo& info = found->second;

    // Every case test must be a known member of the same enum; any other shape
    // keeps the generic CP3002 path.
    std::vector<const EnumCppMember*> tests(sw.cases.size(), nullptr);
    for (std::size_t i = 0; i < sw.cases.size(); ++i) {
        const SwitchCase* c = sw.cases[i].get();
        if (c == nullptr || c->isDefault) continue;
        if (!c->test || c->test->kind != NodeKind::MemberExpr) return false;
        const auto& m = static_cast<const MemberExpr&>(*c->test);
        if (!m.object || m.object->kind != NodeKind::Identifier) return false;
        const std::string& recv = static_cast<const Identifier&>(*m.object).name;
        if (recv != info.name || lookup(recv)) return false;
        const EnumCppMember* member = info.member(m.property);
        if (member == nullptr) return false;
        tests[i] = member;
    }
    // The JS fall-through is the C++ fall-through; each case group is braced so
    // a declaration in one group cannot be jumped over by a later label.
    //
    // Two case tests may share one backing value (`case Color.Red` and
    // `case Color.Crimson` when both are 1). C++ allows only one label per
    // value, but JS has no labels at all: the switch runs the *first* matching
    // test and then falls through. So the second test is never selected - its
    // statements run only when the fall-through reaches them. The lowering
    // therefore keeps the first label and emits every later duplicate as an
    // unlabelled block, which is exactly that reachability.
    std::vector<long long> labelled;
    line(out, indent, "switch (" + expr(*sw.discriminant) + ") {");
    for (std::size_t i = 0; i < sw.cases.size(); ++i) {
        const SwitchCase* c = sw.cases[i].get();
        if (c == nullptr) continue;
        if (c->isDefault) {
            line(out, indent + 1, "default: {");
        } else {
            const EnumCppMember* member = tests[i];
            bool duplicate = false;
            for (long long value : labelled) {
                if (value == member->value) duplicate = true;
            }
            if (duplicate) {
                line(out, indent + 1, "{");
            } else {
                labelled.push_back(member->value);
                line(out, indent + 1,
                     "case " + info.cpp + "::" + member->text + ": {");
            }
        }
        for (const auto& statement : c->statements) {
            if (statement) stmt(*statement, out, indent + 2);
        }
        line(out, indent + 1, "}");
    }
    line(out, indent, "}");
    return true;
}

// =============================================================================
// Type annotation mapping (the enum arms of type_map.cpp)
// =============================================================================
namespace detail {

// The enum arm of `classify`: a named enum lowers to the
// `enum class <name> : long long` that collectEnums emits.
CppInfo classifyEnum(const Type& t) {
    CppInfo info;
    info.type = t;
    info.shape = Shape::Enum;
    info.representable = true;
    info.cpp = mapIdent(t.name);
    return info;
}

}  // namespace detail

// A type annotation -> a cp3 Type, with every Class part named like a known
// enum rewritten to the enum kind. Sema's resolved type wins when it published
// one (D1): it carries generic arguments (`Thread<In, Out>`), the object shape
// of a structural record, and enum kinds the structural re-parse cannot see.
// `collectEnums` runs before the emit walk, so `enums_` is complete here.
std::optional<Type> Generator::typeOfNode(const TypeNode& tn) const {
    std::optional<Type> mapped;
    if (const Type* published = sema_.typeOf(tn.nodeId)) {
        if (!published->isUnknown() && !published->isError()) mapped = *published;
    }
    if (!mapped) mapped = typeFromTypeNode(tn, options_.typeSystem);
    if (!mapped) return std::nullopt;
    rewriteEnumTypes(*mapped);
    return mapped;
}

// Rewrites every Class part named like a known enum to TypeKind::Enum; the
// rewrite recurses through `parts` (array elements, union members, ...).
void Generator::rewriteEnumTypes(Type& t) const {
    if (t.kind == TypeKind::Class && enums_.count(t.name) != 0) {
        t.kind = TypeKind::Enum;
    }
    for (Type& part : t.parts) {
        rewriteEnumTypes(part);
    }
}

}  // namespace cp3
