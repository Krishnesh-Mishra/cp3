// =============================================================================
// src/std/network/express/response/codegen/sema_response.cpp - Express
// response helper typing (PASTE INTO src/std/network/codegen/sema.cpp).
//
// OWNERSHIP: the std/network family, Express response scope ONLY
// (src/std/network/express/response/**). This TU is a paste source, not a
// compiled unit: it carries the exact field rows, member docs and read-types
// the lead inserts into the shared sema (wiring §3/§4 in WIRING.md). It is
// NOT listed in src/sema/CMakeLists.txt and must never be added there as-is
// (it would duplicate the shared sema symbols). All names below live in the
// shared sema's anonymous namespace (kHttpResponse, stringMap(), Field,
// Type::function, withOptionalTail/withOptionalTail2) and reuse them.
//
// Conventions (matched to the shared sema's existing rows):
//   * chainable header helpers return networkValue(kHttpResponse),
//   * terminal helpers return Type::voidType(),
//   * `headersSent` is a boolean read, `get` a string read ("" if absent,
//     mirroring Headers::get), `set(obj)`/`format(map)`/`links(obj)` take
//     the object/Map as ambient (codegen knows the exact shape).
// =============================================================================

#if 0  // paste source: each block names its insertion point

// --- §3a Field rows: append to `httpResponseFields(TypeSystem ts)` after the
// `end` field (before the closing `};`). `status` is in scope there.

        Field{"set",
              withOptionalTail(Type::function(
                  {Type::unknown(), Type::unknown()},
                  networkValue(kHttpResponse))),
              false},
        Field{"append",
              Type::function({Type::string(), Type::string()},
                             networkValue(kHttpResponse)),
              false},
        Field{"get",
              Type::function({Type::string()}, Type::string()),
              false},
        Field{"type",
              Type::function({Type::string()}, networkValue(kHttpResponse)),
              false},
        Field{"format",
              Type::function({Type::unknown()}, Type::voidType()),
              false},
        Field{"render",
              withOptionalTail(Type::function(
                  {Type::string(), Type::unknown()}, Type::voidType())),
              false},
        Field{"download",
              withOptionalTail(Type::function(
                  {Type::string(), Type::string()}, Type::voidType())),
              false},
        Field{"attach",
              Type::function({Type::string()}, networkValue(kHttpResponse)),
              false},
        Field{"links",
              Type::function({Type::unknown()}, networkValue(kHttpResponse)),
              false},
        Field{"location",
              Type::function({Type::string()}, networkValue(kHttpResponse)),
              false},
        Field{"vary",
              Type::function({Type::unknown()}, networkValue(kHttpResponse)),
              false},
        Field{"headersSent", Type::boolean(), false},

// --- §4a Member docs: append inside `networkObjectMemberDoc` under
// `if (type == "HttpResponse") { … }` after the `end` member.

        if (member == "set") return {"set(name: string, value: string): HttpResponse | set(headers: object | Map<string, string>): HttpResponse", "Sets one or more response headers, replacing any existing values. Returns the response for chaining."};
        if (member == "append") return {"append(name: string, value: string): HttpResponse", "Appends a header value (comma-joins when the header already exists). Returns the response for chaining."};
        if (member == "get") return {"get(name: string): string", "Returns the response header value, or \"\" when missing."};
        if (member == "type") return {"type(type: string): HttpResponse", "Sets the Content-Type header (a mime string or a short extension such as \"html\"/\"json\"). Returns the response for chaining."};
        if (member == "format") return {"format(formats: { [mediaType: string]: () => void }): void", "Invokes the handler for the negotiated media type (the first entry, or a `default` entry) and sets its Content-Type when unset. Terminal in the sense the chosen handler answers."};
        if (member == "render") return {"render(view: string, locals?: object): void", "Renders a template view with optional locals and sends it as text/html. Terminal."};
        if (member == "download") return {"download(path: string, filename?: string): void", "Sends the file at `path` as an attachment (Content-Disposition). Terminal."};
        if (member == "attach") return {"attach(filename: string): HttpResponse", "Sets Content-Disposition attachment for `filename` without sending the body. Returns the response for chaining."};
        if (member == "links") return {"links(links: { [rel: string]: string }): HttpResponse", "Appends Link header entries from `rel -> href` pairs. Returns the response for chaining."};
        if (member == "location") return {"location(url: string): HttpResponse", "Sets the Location header. Returns the response for chaining."};
        if (member == "vary") return {"vary(field: string): HttpResponse", "Appends a field to the Vary header (duplicates ignored; \"*\" pins it to \"*\"). Returns the response for chaining."};
        if (member == "headersSent") return {"headersSent: boolean", "Whether the response head has been written (a terminal write happened)."};

#endif
