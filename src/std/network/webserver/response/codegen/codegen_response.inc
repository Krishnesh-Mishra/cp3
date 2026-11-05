// =============================================================================
// src/std/network/webserver/response/codegen/codegen_response.cpp - webserver
// response helper lowering (PASTE INTO src/std/network/codegen/{codegen.cpp,
// codegen_methods.cpp}).
//
// OWNERSHIP: the std/network family, webserver response scope ONLY
// (src/std/network/webserver/response/**). Paste source, NOT a compiled unit:
// never add to src/codegen/CMakeLists.txt as-is. Each block names its
// insertion point (wiring §5/§6 in WIRING.md). All helpers live in
// `cp3::Generator` scope alongside the shared network lowering and reuse its
// `expr(...)` / `networkArgs(...)` / `networkJsonValueText(...)` helpers.
//
// Lowering rules:
//   * Plain forwarders (append/get/type/attach/location, download with
//     1 arg) need NO new code: the generic `networkArgs` path already spells
//     `(res).member(args)`. Only the specialised shapes below need code.
//   * `set(obj)` / `links(obj)` / `format(map)` take object literals whose
//     values are not C++ pairs; codegen spells the vector the runtime
//     overloads take, exactly like the cookie() block it precedes.
//   * `render(view, locals)` reuses networkJsonValueText for locals.
//   * `vary(field | fields)` dispatches on the argument shape (array literal
//     -> the vector overload, otherwise the string overload).
//   * `res.headersSent` (property read) spells the runtime headersSent()
//     method; `res.get`/`res.set`/`res.type` etc. as reads are NOT handled
//     here (they are calls).
// =============================================================================

#if 0  // paste source

// --- §5a Receiver recognition: in codegen.cpp `Generator::isNetworkObject`,
// extend the chainable-member check (currently `status`/`header`) to:
//
//     if (inner.property != "status" && inner.property != "header") return false;
// ->
//     if (inner.property != "status" && inner.property != "header" &&
//         inner.property != "set" && inner.property != "append" &&
//         inner.property != "type" && inner.property != "links" &&
//         inner.property != "location" && inner.property != "vary" &&
//         inner.property != "attach" && inner.property != "cookie") return false;
//
// (cookie already returns HttpResponse& and chains; it belongs in this list
// the same way. All listed members return HttpResponse& at runtime.)

// --- §5b Member-call dispatch: in codegen_methods.cpp
// `Generator::emitNetworkMemberCall`, insert BEFORE the
// `if (m.property == "cookie" …)` block (i.e. right after the json block),
// verbatim:
//
//     if (effectiveType(*m.object).name == kHttpResponse) {
//         if (std::string special = emitwebserverResponseCall(c, m); !special.empty()) return special;
//     }
//
// with the helper below added in the same anonymous namespace as the cookie
// block (needs `expr`, `networkArgs`, `networkJsonValueText`, `effectiveType`
// — all already available in codegen_methods.cpp). Return "" to fall through
// to the generic path for every property this helper does not claim.

std::string Generator::emitwebserverResponseCall(const CallExpr& c, const MemberExpr& m) {
    const std::string recv = "(" + expr(*m.object) + ")";
    // res.set(obj) / res.links(obj): an object literal { k: v } (values are
    // string-shaped webserverions) -> the vector-of-pairs overload.
    if ((m.property == "set" || m.property == "links") && c.arguments.size() == 1 &&
        c.arguments.front() && c.arguments.front()->kind == NodeKind::ObjectLiteral) {
        const auto& lit = static_cast<const ObjectLiteral&>(*c.arguments.front());
        std::string pairs;
        for (const auto& p : lit.properties) {
            if (!p || !p->value || p->isMethod) continue;
            std::string key;
            if (p->key && p->key->kind == NodeKind::Identifier &&
                !p->computed) key = static_cast<const Identifier&>(*p->key).name;
            else if (p->key && p->key->kind == NodeKind::StringLiteral && !p->computed)
                key = static_cast<const StringLiteral&>(*p->key).value;
            else if (p->key) key = expr(*p->key);  // computed string key
            else continue;
            // String literal keys spell quoted; computed keys are webserverions.
            const bool isLiteral = (p->key && !p->computed &&
                (p->key->kind == NodeKind::Identifier || p->key->kind == NodeKind::StringLiteral));
            const std::string keyText = isLiteral ? "\"" + key + "\"" : key;
            if (!pairs.empty()) pairs += ", ";
            pairs += "{\"" + key + "\", " + expr(*p->value) + "}";
            (void)keyText;
        }
        // res.set({ "X": "v" }) -> .set(vector<pair<string,string>>{{...}})
        // res.links({ next: "/p2" }) -> .links(vector pairs with [rel, href])
        // For links the runtime expects (rel, href) pairs; the object shape
        // is already exactly that, so the same vector construction serves.
        return recv + "." + m.property +
            "(std::vector<std::pair<std::string, std::string>>{" + pairs + "})";
    }
    // res.set(map) where map is a Map<string,string> binding: the StringMap
    // overload (no object-literal wrapping). Detected by the Map receiver
    // type; an object literal was handled above and a string name+value call
    // falls through to the generic two-argument path.
    if (m.property == "set" && c.arguments.size() == 1 && c.arguments.front()) {
        const Type argType = effectiveType(*c.arguments.front());
        if (argType.name == "Map" || argType.kind == TypeKind::Class) {
            needsCollections_ = true;
            return recv + ".set(" + expr(*c.arguments.front()) + ")";
        }
    }
    // res.format({ "text/html": () => …, default: () => … }): the map values
    // are arrow functions -> vector<pair<string, function<void()>>>.
    if (m.property == "format" && c.arguments.size() == 1 && c.arguments.front() &&
        c.arguments.front()->kind == NodeKind::ObjectLiteral) {
        const auto& lit = static_cast<const ObjectLiteral&>(*c.arguments.front());
        std::string entries;
        for (const auto& p : lit.properties) {
            if (!p || !p->value || p->isMethod) continue;
            std::string key;
            if (p->key && p->key->kind == NodeKind::Identifier && !p->computed)
                key = static_cast<const Identifier&>(*p->key).name;
            else if (p->key && p->key->kind == NodeKind::StringLiteral && !p->computed)
                key = static_cast<const StringLiteral&>(*p->key).value;
            else continue;
            if (!entries.empty()) entries += ", ";
            entries += "{\"" + key + "\", " + expr(*p->value) + "}";
        }
        return recv + ".format(std::vector<std::pair<std::string, std::function<void()>>>{" +
            entries + "})";
    }
    // res.render(view, locals?): locals is a json value tree (networkJsonValue
    // Text); the runtime stringifies it for the view renderer.
    if (m.property == "render" && c.arguments.size() >= 1 && c.arguments.front()) {
        if (c.arguments.size() == 1) {
            return recv + ".render(" + expr(*c.arguments.front()) + ")";
        }
        if (c.arguments.size() >= 2 && c.arguments[1]) {
            const std::string locals = networkJsonValueText(*c.arguments[1]);
            if (locals.empty()) {
                reportTypeError(*c.arguments[1],
                    "type mismatch: res.render(view, locals) needs a serialisable locals object");
                return "0.0";
            }
            needsJson_ = true;
            return recv + ".render(" + expr(*c.arguments.front()) + ", " + locals + ")";
        }
    }
    // res.vary(field | fields): an array literal spells the vector overload;
    // a single string spells the string overload (generic path would also
    // work for the string form, but the array form needs this dispatch).
    if (m.property == "vary" && c.arguments.size() == 1 && c.arguments.front() &&
        c.arguments.front()->kind == NodeKind::ArrayLiteral) {
        const auto& arr = static_cast<const ArrayLiteral&>(*c.arguments.front());
        std::string items;
        for (const auto& e : arr.elements) {
            if (!e) continue;
            if (!items.empty()) items += ", ";
            items += expr(*e);
        }
        return recv + ".vary(std::vector<std::string>{" + items + "})";
    }
    // res.download(path) with one argument: the runtime default filename arg
    // covers it through the generic path; nothing to claim here.
    return std::string();
}

// --- §5c Property read: in codegen_methods.cpp
// `Generator::emitNetworkMemberRead`, add to the kHttpResponse arm the read
// `headersSent` (currently there is no kHttpResponse arm at all; add one
// before `return std::string();`):
//
//     if (name == kHttpResponse && m.property == "headersSent") {
//         needsNetwork_ = true;
//         return "(" + expr(*m.object) + ").headersSent()";
//     }
//
// and in `networkFieldType` add, under the kHttpRequest block:
//
//     if (recv == kHttpResponse && m.property == "headersSent") {
//         return Type::boolean();
//     }
//
// (networkFieldType's kHttpResponse case is new; the boolean type feeds
// `if (res.headersSent) …` condition classification the same way
// `res.ok`/`res.status` reads do on FetchResponse.)

#endif
