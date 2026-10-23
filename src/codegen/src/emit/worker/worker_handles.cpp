// =============================================================================
// src/codegen/src/emit/worker_handles.cpp - the worker handle member surface
// (D80, spec 13).
//
// The main side's `w` and the worker side's `thread` are typed channel handles
// (detail/worker.h). This unit lowers their surface:
//
//     w.postMessage(v)     thread.postMessage(v)   deep copy out/in
//     w.on("message", l)   thread.on("message", l) payload listener
//     w.on("error", l)     thread.on("error", l)   catchable handler errors
//     await w.request(v)                           next Out message
//     w.terminate()                                stop the worker loop
//
// A listener is an inline arrow whose parameter type comes from the channel
// direction (a void direction gets a nullary lambda: the dead parameter is
// declared inside the body so references keep compiling). Sending checks the
// destination direction (CP3906 when void) and the payload (CP3907/CP3908);
// the payload spelling is the same one the worker TU's signature uses.
// =============================================================================
#include "detail/generator/generator.h"

#include <string>
#include <vector>

#include "cp3/diag/diagnostic_codes.hpp"
#include "detail/generator/cpp_writer.h"
#include "detail/generator/mangle.h"
#include "detail/generator/type_map.h"
#include "detail/worker/worker.h"

namespace cp3 {

using namespace detail;

// -----------------------------------------------------------------------------
// The handle member surface: `w.*` / `thread.*`
// -----------------------------------------------------------------------------

void Generator::reportWorkerCode(const Node& node, const std::string& code,
                                 const std::string& message) {
    if (suppressDiagnostics_) return;
    if (node.nodeId != 0 && !reported_.insert(node.nodeId).second) return;
    sink_.error(code, node.span, message);
}

std::string Generator::emitWorkerHandleCall(const CallExpr& c,
                                            const MemberExpr& m) {
    if (!m.object) return std::string();
    const Type recv = effectiveType(*m.object);
    Type in;
    Type out;
    if (!channelHandleParts(recv, in, out)) return std::string();
    const bool workerSide = isThreadHandleType(recv);
    const std::string recvText = expr(*m.object);
    const std::string ownerNS = channelOwnerNS(recv);
    if (!c.typeArgs.empty()) {
        reportWorkerCode(c, diagcode::kWorkerHandleMember,
                         "worker handle calls do not take type arguments");
        return "0.0";
    }
    if (m.property == "on") {
        return workerOnText(c, recvText, workerSide ? in : out, ownerNS,
                            workerSide);
    }
    if (m.property == "postMessage") {
        const WorkerSendSurface surface =
            workerSide ? WorkerSendSurface::WorkerPost
                       : WorkerSendSurface::MainPost;
        return workerSendText(c, recvText, WorkerChannel{in, out}, ownerNS,
                              surface);
    }
    if (m.property == "request" && !workerSide) {
        return workerSendText(c, recvText, WorkerChannel{in, out}, ownerNS,
                              WorkerSendSurface::MainRequest);
    }
    if (m.property == "terminate" && !workerSide) {
        return workerTerminateText(c, recvText);
    }
    reportWorkerCode(c, diagcode::kWorkerHandleMember,
                     "this member is not part of the worker handle surface "
                     "(main: postMessage / on / request / terminate; worker: "
                     "on / postMessage)");
    return "0.0";
}

std::string Generator::workerOnText(const CallExpr& c, const std::string& recv,
                                    const Type& direction,
                                    const std::string& ownerNS,
                                    bool workerSide) {
    (void)workerSide;
    if (c.arguments.size() != 2 || !c.arguments[0] || !c.arguments[1] ||
        c.arguments[0]->kind != NodeKind::StringLiteral ||
        c.arguments[1]->kind != NodeKind::ArrowFunction) {
        reportWorkerCode(c, diagcode::kWorkerHandleMember,
                         "on(event, listener) takes a literal \"message\" or "
                         "\"error\" and an inline arrow listener");
        return "0.0";
    }
    const std::string event = static_cast<const StringLiteral&>(*c.arguments[0]).value;
    if (event != "message" && event != "error") {
        reportWorkerCode(c, diagcode::kWorkerHandleMember,
                         "the worker event must be \"message\" or \"error\"");
        return "0.0";
    }
    const auto& arrow = static_cast<const ArrowFunction&>(*c.arguments[1]);
    const std::string listener =
        workerListenerText(arrow, direction, ownerNS, event == "error");
    return "(" + recv + ")." + (event == "error" ? "onError" : "onMessage") +
           "(" + listener + ")";
}

std::string Generator::workerSendText(const CallExpr& c, const std::string& recv,
                                      const WorkerChannel& channel,
                                      const std::string& ownerNS,
                                      WorkerSendSurface surface) {
    const bool workerSide = surface == WorkerSendSurface::WorkerPost;
    const Type& direction = workerSide ? channel.out : channel.in;
    const Type& opposite = workerSide ? channel.in : channel.out;
    const std::string method =
        surface == WorkerSendSurface::MainRequest ? "request" : "postMessage";
    // `request` needs a reply: a `Worker<In, void>` has no Out channel to carry
    // one, so the member is a compile error even though the In direction is a
    // valid payload.
    if (surface == WorkerSendSurface::MainRequest && opposite.isVoid()) {
        reportWorkerCode(c, diagcode::kWorkerVoidSend,
                         "this worker's Out direction is void: the main side "
                         "cannot request a reply");
        return "0.0";
    }
    if (c.arguments.size() != 1 || !c.arguments.front() ||
        c.arguments.front()->kind == NodeKind::SpreadElement) {
        reportWorkerCode(c, diagcode::kWorkerHandleMember,
                         method + "(...) takes exactly one message value");
        return "0.0";
    }
    if (direction.isVoid()) {
        reportWorkerCode(c, diagcode::kWorkerVoidSend,
                         workerSide
                             ? "this worker's Out direction is void: the worker "
                               "cannot send messages"
                             : "this worker's In direction is void: the main "
                               "side cannot send messages");
        return "0.0";
    }
    const Expr& arg = *c.arguments.front();
    const std::optional<Type> actualType = workerSendArgumentType(arg);
    const std::string actual =
        actualType ? workerPayloadText(*actualType, namesp_) : std::string();
    if (!actualType || actual.empty()) {
        reportWorkerCode(arg, diagcode::kWorkerPayloadNotCopyable,
                         "worker messages are deep-copied: payloads must be "
                         "copyable value types (numbers, strings, booleans, "
                         "arrays, records, enums, or unions/options of those)");
        return "0.0";
    }
    const std::string expected = workerPayloadText(direction, ownerNS);
    if (!expected.empty() &&
        !workerPayloadAccepts(direction, *actualType, ownerNS)) {
        reportWorkerCode(arg, diagcode::kWorkerPayloadMismatch,
                         "this message's type does not match the channel's " +
                             std::string(workerSide ? "Out" : "In") +
                             " type (the Thread<In, Out> annotation)");
        return "0.0";
    }
    std::string text;
    Type expectedObject;
    Type actualObject;
    bool expectedOptional = false;
    bool actualOptional = false;
    if (expected == workerPayloadText(*actualType, namesp_)) {
        text = expr(arg);
    } else if (objectReceiver(direction, expectedObject, expectedOptional) &&
               objectReceiver(*actualType, actualObject, actualOptional) &&
               expectedOptional == actualOptional) {
        // A record literal (or a variable of a narrower shape) becomes the
        // expected struct; fields that are unions/options are converted in
        // workerObjectConversion.
        text = workerObjectConversion(direction, *actualType, expr(arg), ownerNS);
        if (text.empty()) text = expr(arg);
    } else {
        text = workerPayloadConversion(direction, expected, *actualType, expr(arg));
    }
    return "(" + recv + ")." + method + "(" + text + ")";
}

std::string Generator::workerTerminateText(const CallExpr& c,
                                           const std::string& recv) {
    if (!c.arguments.empty()) {
        reportWorkerCode(c, diagcode::kWorkerHandleMember,
                         "terminate() takes no arguments");
        return "0.0";
    }
    return "(" + recv + ").terminate()";
}

// -----------------------------------------------------------------------------
// Payloads: the C++ spelling of one channel direction, when it is copyable
// -----------------------------------------------------------------------------

std::string Generator::workerPayloadText(const Type& raw,
                                         const std::string& ownerNS) const {
    Type t = raw;
    if (t.isLiteral()) t = t.literalBaseType();
    if (t.isVoid()) return "void";
    if (t.kind == TypeKind::Enum) {
        if (t.name.empty()) return std::string();
        const std::string& ns = ownerNS.empty() ? namesp_ : ownerNS;
        return "::" + ns + "::" + mapIdent(t.name);
    }
    if (isChannelHandleType(t) || isPromiseType(t) || isEventEmitterType(t)) {
        return std::string();
    }
    if (t.kind == TypeKind::Function) return std::string();
    // A structural record is a payload only when every field is one too: a
    // record carrying a function (or another non-copyable value) is not.
    if (t.kind == TypeKind::Object) {
        std::string structName;
        std::vector<ObjectField> fields;
        if (!objectShapeOf(t, structName, fields)) return std::string();
        for (const ObjectField& field : fields) {
            if (workerPayloadText(field.type, ownerNS).empty()) {
                return std::string();
            }
        }
    }
    // Arrays, unions and options recurse: a non-copyable element/part makes
    // the whole value non-copyable.
    if (t.kind == TypeKind::Array) {
        if (!t.parts.empty() && workerPayloadText(t.parts.front(), ownerNS).empty()) {
            return std::string();
        }
    }
    if (t.kind == TypeKind::Union || t.kind == TypeKind::Nullable) {
        for (const Type& part : t.parts) {
            if (part.isNullish()) continue;
            if (workerPayloadText(part, ownerNS).empty()) return std::string();
        }
    }
    const CppInfo ci = classify(t);
    if (!ci.representable) return std::string();
    switch (ci.shape) {
        case Shape::Number:
        case Shape::String:
        case Shape::Bool:
        case Shape::Array:
        case Shape::Object:
        case Shape::Union:
        case Shape::Optional:
        case Shape::Maybe:
            return ci.cpp;
        default:
            return std::string();
    }
}

std::optional<Type> Generator::workerSendArgumentType(const Expr& arg) const {
    const Type direct = effectiveType(arg);
    if (!workerPayloadText(direct, namesp_).empty()) return direct;
    // A member read over a record shape: sema types reads through the worker
    // channel's unannotated parameters as ambient, so the shape the channel
    // fixes is what names the field. Its cp3 type is the payload type.
    if (arg.kind != NodeKind::MemberExpr) return std::nullopt;
    const auto& m = static_cast<const MemberExpr&>(arg);
    if (!m.object || m.property.empty()) return std::nullopt;
    Type object;
    bool optional = false;
    if (!objectReceiver(effectiveType(*m.object), object, optional)) {
        return std::nullopt;
    }
    std::string structName;
    std::vector<ObjectField> fields;
    if (!objectShapeOf(object, structName, fields)) return std::nullopt;
    for (const ObjectField& field : fields) {
        if (field.name == m.property) return field.type;
    }
    return std::nullopt;
}

bool Generator::workerPayloadAccepts(const Type& expected, const Type& actual,
                                     const std::string& ownerNS) const {
    const Type actualNorm = actual.isLiteral() ? actual.literalBaseType() : actual;
    const std::string expectedCpp = workerPayloadText(expected, ownerNS);
    const std::string actualCpp = workerPayloadText(actual, namesp_);
    if (expectedCpp.empty() || actualCpp.empty()) return false;
    if (expectedCpp == actualCpp) return true;
    // An enum is a structural payload: the entry and a worker unit each declare
    // their own `enum class` (they are separate TUs), but the message carries
    // member values. Same-named enums therefore match, as do the enum members
    // of a union/option.
    if (expected.kind == TypeKind::Enum && actualNorm.kind == TypeKind::Enum &&
        expected.name == actualNorm.name) {
        return true;
    }
    // A member value is a valid payload for a union/option: the C++
    // variant/optional constructor selects the case (the conversion helper
    // spells that construction when the C++ types differ).
    if (expected.isUnion() || expected.isNullable()) {
        for (const Type& part : expected.parts) {
            if (part.isNullish()) continue;
            if (part == actualNorm) return true;
        }
    }
    // Records are structural too: a literal with a narrower field type (`{ v: 1
    // }` against `{ v: number | string }`) conforms when every expected field
    // accepts the corresponding actual field. `workerObjectConversion` spells
    // the struct-to-struct construction at the send site.
    Type expectedObject;
    Type actualObject;
    bool expectedOptional = false;
    bool actualOptional = false;
    if (objectReceiver(expected, expectedObject, expectedOptional) &&
        objectReceiver(actualNorm, actualObject, actualOptional) &&
        expectedOptional == actualOptional) {
        std::string expectedName;
        std::string actualName;
        std::vector<ObjectField> expectedFields;
        std::vector<ObjectField> actualFields;
        if (!objectShapeOf(expectedObject, expectedName, expectedFields) ||
            !objectShapeOf(actualObject, actualName, actualFields)) {
            return false;
        }
        for (const ObjectField& expectedField : expectedFields) {
            const ObjectField* actualField = nullptr;
            for (const ObjectField& field : actualFields) {
                if (field.name == expectedField.name) actualField = &field;
            }
            if (actualField == nullptr ||
                !workerPayloadAccepts(expectedField.type, actualField->type,
                                      ownerNS)) {
                return false;
            }
        }
        return true;
    }
    return false;
}

std::string Generator::workerPayloadConversion(const Type& expected,
                                               const std::string& expectedCpp,
                                               const Type& actual,
                                               const std::string& text) const {
    const std::string actualCpp = workerPayloadText(actual, namesp_);
    if (actualCpp.empty() || actualCpp == expectedCpp) return text;
    if (expected.kind == TypeKind::Enum && actual.kind == TypeKind::Enum &&
        expected.name == actual.name) {
        return "static_cast<" + expectedCpp + ">(" + text + ")";
    }
    if ((expected.isUnion() || expected.isNullable()) && !expectedCpp.empty()) {
        return expectedCpp + "(" + text + ")";
    }
    return text;
}

std::string Generator::workerObjectConversion(const Type& expected,
                                              const Type& actual,
                                              const std::string& text,
                                              const std::string& ownerNS) const {
    Type expectedObject;
    Type actualObject;
    bool expectedOptional = false;
    bool actualOptional = false;
    if (!objectReceiver(expected, expectedObject, expectedOptional) ||
        !objectReceiver(actual, actualObject, actualOptional) ||
        expectedOptional != actualOptional) {
        return std::string();
    }
    std::string expectedName;
    std::string actualName;
    std::vector<ObjectField> expectedFields;
    std::vector<ObjectField> actualFields;
    if (!objectShapeOf(expectedObject, expectedName, expectedFields) ||
        !objectShapeOf(actualObject, actualName, actualFields)) {
        return std::string();
    }
    // Plain objects are reference handles (`Ref<Struct>`): the fields are read
    // through the pointee, and an optional source is opened once so the
    // conversion keeps the empty case.
    const std::string source = expectedOptional ? "(*cp3_o)" : text;
    std::vector<std::string> values;
    for (const ObjectField& expectedField : expectedFields) {
        const ObjectField* actualField = nullptr;
        for (const ObjectField& field : actualFields) {
            if (field.name == expectedField.name) actualField = &field;
        }
        if (actualField == nullptr) return std::string();
        const std::string fieldText =
            source + "->" + mapIdent(actualField->name);
        if (!workerPayloadAccepts(expectedField.type, actualField->type,
                                  ownerNS)) {
            return std::string();
        }
        if (workerPayloadText(expectedField.type, ownerNS) ==
            workerPayloadText(actualField->type, namesp_)) {
            values.push_back(fieldText);
            continue;
        }
        if (expectedField.type.kind == TypeKind::Object &&
            actualField->type.kind == TypeKind::Object) {
            const std::string nested = workerObjectConversion(
                expectedField.type, actualField->type, fieldText, ownerNS);
            if (nested.empty()) return std::string();
            values.push_back(nested);
            continue;
        }
        values.push_back(workerPayloadConversion(
            expectedField.type, expectedField.cpp, actualField->type,
            fieldText));
    }
    std::string out = "::cp3::runtime::makeRef<" + expectedName + ">(" +
                      expectedName + "{";
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i != 0) out += ", ";
        out += values[i];
    }
    out += "})";
    if (!expectedOptional) return out;
    return "([&]{ auto cp3_o = (" + text +
           "); return cp3_o.has_value() ? std::optional<::cp3::runtime::Ref<" +
           expectedName + ">>(" + out + ") : std::nullopt; }())";
}

}  // namespace cp3
