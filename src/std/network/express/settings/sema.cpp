// =============================================================================
// src/std/network/express/settings/sema.cpp - Express settings + view-engine
// member typing and documentation.
//
// OWNERSHIP: the std/network/express/settings family
// (src/std/network/express/settings/**). The member tables below are the
// settings half of the Express surface:
//
//   * `$express:ExpressApp` - `app.enable/disable/enabled`, `app.set/get`,
//     `app.engine`, `app.locals`, `app.settings`, `app.render`.
//   * `$express:Locals` - `app.locals` / `res.locals`: OPEN (any property
//     reads/writes as ambient, like a plain JS object).
//   * `$network:HttpResponse` (appended rows) - `res.render`, `res.locals`.
//   * `$network:HttpRequest` (appended rows) - `req.protocol`.
//
// The marker names are shared with the codegen unit
// (src/std/network/express/settings/codegen.cpp), which uses them to decide
// which receivers it lowers. The integrator's dispatch lines live in
// WIRING.md (member_check.cpp tries `expressObjectMember` before the
// network table, so `res.render` is not reported as a network miss).
//
// `app.get(name)` returns ambient (unknown): Express returns `any`, and the
// CP3 caller narrows by assignment (`const dir: string = app.get("views")`)
// or by the runtime's scalar conversions. `app.set(name, value)` takes
// ambient for the same reason; codegen restricts the value to the scalar /
// string-list shapes `SettingValue` can carry and reports anything else.
// =============================================================================
#include "detail/analyzer.h"

#include <optional>
#include <string>
#include <vector>

namespace cp3 {

namespace {

constexpr const char* kExpressApp = "$express:ExpressApp";
constexpr const char* kExpressLocals = "$express:Locals";
constexpr const char* kHttpRequest = "$network:HttpRequest";
constexpr const char* kHttpResponse = "$network:HttpResponse";

Type expressValue(const char* name) { return Type(TypeKind::Object, name); }

/// `(file, options) => string`: the view-engine function shape. The
/// options parameter is ambient (a locals-shaped bag at runtime).
Type viewEngineHandler() {
    return Type::function({Type::string(), ambient()}, Type::string());
}

std::vector<Field> expressAppFields() {
    const Type app = expressValue(kExpressApp);
    return {
        Field{"enable",
              Type::function({Type::string()}, app), false},
        Field{"disable",
              Type::function({Type::string()}, app), false},
        Field{"enabled",
              Type::function({Type::string()}, Type::boolean()), false},
        // `app.get(name)` / `app.set(name, value)`: ambient both ways (see
        // the file comment). `set` chains like Express.
        Field{"get",
              Type::function({Type::string()}, ambient()), false},
        Field{"set",
              Type::function({Type::string(), ambient()}, app), false},
        Field{"engine",
              Type::function({Type::string(), viewEngineHandler()}, app),
              false},
        Field{"locals", expressValue(kExpressLocals), false},
        Field{"settings", ambient(), false},
        // `app.render(view, options?)`: the HTML string, synchronously
        // (Express' callback form is not offered; see the runtime header).
        Field{"render",
              withOptionalTail(
                  Type::function({Type::string(), ambient()}, Type::string())),
              false},
    };
}

/// The appended rows of the shared network markers. Kept separate from
/// the network family's own tables (which this family may not edit):
/// `expressObjectMember` answers these, and the integrator tries it
/// before the network table.
std::vector<Field> expressResponseFields() {
    return {
        Field{"render",
              withOptionalTail(
                  Type::function({Type::string(), ambient()},
                                 Type::voidType())),
              false},
        Field{"locals", expressValue(kExpressLocals), false},
    };
}

std::vector<Field> expressRequestFields() {
    return {
        Field{"protocol", Type::string(), false},
    };
}

std::vector<Field> expressFieldsFor(const std::string& name) {
    if (name == kExpressApp) return expressAppFields();
    if (name == kHttpResponse) return expressResponseFields();
    if (name == kHttpRequest) return expressRequestFields();
    return {};
}

}  // namespace

Type Analyzer::expressObjectType(const std::string& name) {
    if (name == kExpressApp || name == kExpressLocals) {
        return Type(TypeKind::Object, name);
    }
    return Type::unknown();
}

std::optional<Type> Analyzer::expressObjectMember(const Type& recv,
                                                  const std::string& prop) {
    if (recv.kind != TypeKind::Object) return std::nullopt;
    // `app.locals` / `res.locals` is an open bag: any property read or
    // write is ambient, like a plain JS object. (The codegen unit lowers
    // the member/index syntax onto Locals::set/getOr.)
    if (recv.name == kExpressLocals) return ambient();
    for (const Field& field : expressFieldsFor(recv.name)) {
        if (field.name == prop) return field.type;
    }
    return std::nullopt;
}

std::vector<std::string> expressObjectMemberNames(
    const std::string& objectTypeName) {
    std::vector<std::string> names;
    for (const Field& field : expressFieldsFor(objectTypeName)) {
        names.push_back(field.name);
    }
    return names;
}

StdMemberDoc expressObjectMemberDoc(const std::string& objectTypeName,
                                    const std::string& member, TypeSystem) {
    const std::string type = objectTypeName.rfind("$express:", 0) == 0
                                 ? objectTypeName.substr(9)
                                 : objectTypeName.rfind("$network:", 0) == 0
                                       ? objectTypeName.substr(9)
                                       : objectTypeName;
    if (type == "ExpressApp") {
        if (member == "enable")
            return {"enable(name: string): ExpressApp",
                    "Sets the setting `name` to `true` (exactly `set(name, true)`). Chainable."};
        if (member == "disable")
            return {"disable(name: string): ExpressApp",
                    "Sets the setting `name` to `false` (exactly `set(name, false)`). Chainable."};
        if (member == "enabled")
            return {"enabled(name: string): boolean",
                    "The truthiness of the setting `name` (false when unset)."};
        if (member == "get")
            return {"get(name: string): unknown",
                    "Reads the setting `name`, or `null` when unset. Assign to a typed binding to narrow: `const dir: string = app.get(\"views\")`."};
        if (member == "set")
            return {"set(name: string, value: string | number | boolean | null | string[]): ExpressApp",
                    "Stores `value` under `name`. Chainable. `\"views\"`, `\"view engine\"` and `\"trust proxy\"` are consumed by `res.render` and the dispatcher; other names are stored verbatim."};
        if (member == "engine")
            return {"engine(ext: string, handler: (file: string, options: unknown) => string): ExpressApp",
                    "Registers the view engine for `ext` (`\"html\"` and `\".html\"` name the same engine). The handler receives the resolved template path and the merged locals and returns the HTML; throw to fail the render. Chainable."};
        if (member == "locals")
            return {"locals: object",
                    "The application locals bag: `app.locals.title = \"Hello\"` stores a value visible to every render. An open object - any property reads/writes."};
        if (member == "settings")
            return {"settings: unknown",
                    "The settings table itself (the same entries `app.get` / `app.set` address by name)."};
        if (member == "render")
            return {"render(view: string, options?: object): string",
                    "Renders `view` and returns the HTML string (a synchronous spelling of Express' callback `app.render`). Throws when the template cannot be found."};
    }
    if (type == "HttpResponse") {
        if (member == "render")
            return {"render(view: string, options?: object): void",
                    "Renders `view` and answers `text/html`. Options merge over `res.locals` over `app.locals`. Terminal: the first terminal write of the chain wins."};
        if (member == "locals")
            return {"locals: object",
                    "The per-request locals bag (fresh for every request): `res.locals.user = ...` is visible to this request's renders only."};
    }
    if (type == "HttpRequest") {
        if (member == "protocol")
            return {"protocol: string",
                    "\"http\" or \"https\": from `X-Forwarded-Proto` when the \"trust proxy\" setting trusts the proxy chain, else \"http\"."};
    }
    return {};
}

}  // namespace cp3
