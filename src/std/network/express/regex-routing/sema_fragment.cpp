// =============================================================================
// src/std/network/express/regex-routing/sema_fragment.cpp
//
// SEMA FRAGMENT for `src/std/network/codegen/sema.cpp` — not a TU.
// Paste the "=== PASTE INTO sema.cpp ===" blocks below into that file.
// The file itself is excluded from the build; it is the reviewable
// fragment the integrator applies (see README.md WIRING).
//
// Why a fragment and not a TU: the type information lives in the
// Analyzer (sema.cpp) and the emission in the Generator
// (codegen_methods.cpp); neither exposes a registration API a side TU
// could call. The fragment keeps each hunk next to the code it extends.
//
// What it types:
//   * verb overloads:  get/post/put/delete/patch/head/options/any take
//     `(string | RegExp | (string | RegExp)[], handler)`; the options
//     form `(path, { caseSensitive?, strict? }, handler)` is accepted by
//     the member-call hook below (only trailing optionality exists in
//     `Type::function`, so a middle-optional parameter needs a hook).
//   * app settings:    `set` / `enable` / `disable` / `get` / `enabled` /
//     `disabled` on the HttpServer marker for the two routing keys.
// The RegExp arm is the nominal class "RegExp"
// (src/sema/src/checks/type/regex_check.cpp); the array arm reuses
// `Type::array`, like every other collection path.
// ============================================================================

// === PASTE INTO sema.cpp (anonymous namespace, near httpRouteHandler) ======

/// `string | RegExp | (string | RegExp)[]`: the route-path argument type.
Type regexRoutingPathType() {
    Type regexp = Type::named(TypeKind::Class, "RegExp");
    Type elem = Type::unionOf({Type::string(), regexp});
    return Type::unionOf({Type::string(), regexp, Type::array(elem)});
}

/// `{ caseSensitive?: boolean, strict?: boolean }` route options shape.
Type regexRoutingOptionsType() {
    Type t(TypeKind::Class, "Object");
    t.name = "$network:RouteOptions";
    return t;
}

bool isRegexRoutingVerb(const std::string& property) {
    return property == "get" || property == "post" || property == "put" ||
           property == "delete" || property == "patch" || property == "head" ||
           property == "options" || property == "any";
}

// === In httpServerFields(): widen the verb parameter type + settings =======
    // Replace `Type::string()` with `regexRoutingPathType()` in the verb
    // loop (the route handler parameter is unchanged):
    //   fields.push_back(Field{verb, Type::function({regexRoutingPathType(), route}, Type::voidType()), false});
    // App routing settings (Express `app.set/enable/disable/get/...`).
    fields.push_back(Field{"set",
                           Type::function({Type::string(), Type::boolean()},
                                          networkValue(kHttpServer)),
                           false});
    fields.push_back(Field{"enable",
                           Type::function({Type::string()}, networkValue(kHttpServer)),
                           false});
    fields.push_back(Field{"disable",
                           Type::function({Type::string()}, networkValue(kHttpServer)),
                           false});
    fields.push_back(Field{"get", Type::function({Type::string()}, Type::boolean()), false});
    fields.push_back(Field{"enabled", Type::function({Type::string()}, Type::boolean()), false});
    fields.push_back(Field{"disabled", Type::function({Type::string()}, Type::boolean()), false});

// === In tryInferNetworkMemberCall(): the (path, options, handler) form =====
    // Paste at the top of the function body (before the `json` tail):
    //
    //   if (m.property is a route verb && receiver is $network:HttpServer &&
    //       call.arguments.size() == 3) {
    //       inferArgs(call);
    //       // Type the path, options and handler against a dedicated
    //       // signature with the middle parameter optional, then return
    //       // the void result. The codegen (codegen_fragment.cpp) emits
    //       // `addRouteWithOptions(...)` for the 3-argument spelling.
    //       FuncSig sig;
    //       sig.name = m.property;
    //       sig.params = {regexRoutingPathType(), regexRoutingOptionsType(),
    //                     httpRouteHandler()};
    //       sig.optional = {false, true, false};
    //       sig.ret = Type::voidType();
    //       checkArgs(sig, call);
    //       // Validate the options literal: only `caseSensitive` / `strict`
    //       // boolean keys (same style as checkCreateServerOptions).
    //       if (auto* opts = call.arguments[1].get();
    //           opts && opts->kind == NodeKind::ObjectLiteral) {
    //           for (auto& prop : static_cast<const ObjectLiteral&>(*opts).properties) {
    //               if (!prop || !prop->value) continue;
    //               const std::string key = keyName(prop->key.get());
    //               if (key != "caseSensitive" && key != "strict") {
    //                   report(diagcode::kTypeMismatch, anchorSpan(prop->span),
    //                          "unknown route option '" + key +
    //                              "' (only 'caseSensitive' and 'strict' are supported)");
    //               } else if (typeOfExpr(*prop->value).kind != TypeKind::Boolean &&
    //                          typeOfExpr(*prop->value).kind != TypeKind::Unknown) {
    //                   report(diagcode::kTypeMismatch, anchorSpan(prop->value->span),
    //                          "route option '" + key + "' must be a boolean");
    //               }
    //           }
    //       } else if (opts) {
    //           report(diagcode::kTypeMismatch, anchorSpan(opts->span),
    //                  "route options must be an object literal: " +
    //                      "{ caseSensitive?: boolean, strict?: boolean }");
    //       }
    //       out = Type::voidType();
    //       return true;
    //   }

// === In networkObjectMemberDoc(): doc swap (optional) =====================
// HttpServer verb line becomes:
//   member + "(path: string | RegExp | (string | RegExp)[], handler: " +
//       "(req: HttpRequest, res: HttpResponse) => void): void"
// Add settings docs:
//   if (member == "set") return {"set(key: string, value: boolean): HttpServer",
//       "Sets an app setting. The routing settings are \"case sensitive routing\" and \"strict routing\" (Express names); unknown keys are ignored."};
//   if (member == "enable" || member == "disable")
//       return {member + "(key: string): HttpServer", "Alias for set(key, true/false), e.g. app.enable(\"strict routing\")."};
//   if (member == "get" || member == "enabled" || member == "disabled")
//       return {member + "(key: string): boolean", "Reads an app setting (the routing defaults are false)."};
// Route-options doc (verb entry):
//   "... An optional options object `{ caseSensitive?: boolean, strict?: boolean }` may sit between the path and the handler; it overrides the app settings for this route only."
