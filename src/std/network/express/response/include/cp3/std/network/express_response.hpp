// =============================================================================
// cp3/std/network/express_response.hpp - Express response helper declarations.
//
// OWNERSHIP: the std/network family, Express response scope ONLY
// (src/std/network/express/response/**). This header is NOT on the generated-
// code include path yet; the lead wires §1 into
// src/std/network/include/cp3/std/network.hpp (inside class HttpResponse) and
// the implementation in ../runtime/runtime_express_response.cpp defines them.
// Until then the file is documentation + a paste source, never #included by
// generated code. It adds NO new include dir (the existing
// src/std/network/include root already serves it when wired).
//
// Export surface (all members of cp3::runtime::network::HttpResponse):
//   HttpResponse& set(name, value)          - replace a header
//   HttpResponse& set(pairs)                - replace many (object form)
//   HttpResponse& set(StringMap)            - replace many (Map form)
//   HttpResponse& append(name, value)       - append/comma-join a header
//   std::string get(name)                   - read a header ("" if absent)
//   HttpResponse& type(type)                - set Content-Type (mime or ext)
//   void format(formats)                    - negotiate + invoke handler
//   void render(view) / render(view, locals)- template render (terminal)
//   void download(path, filename="")        - attach + sendFile (terminal)
//   HttpResponse& attach(filename)          - Content-Disposition only
//   HttpResponse& links(pairs)              - append Link header
//   HttpResponse& location(url)             - set Location header
//   HttpResponse& vary(field) / vary(fields)- append Vary token(s)
//   bool headersSent()                      - true once head started/finished
//   (res.status chaining already exists: status/header/cookie return
//    HttpResponse&, and the codegen chain set below covers them.)
//
// View-layer sink (namespace cp3::runtime::network; the app-scope agent owns
// the `app.set('views', …)` / `app.engine(…)` spelling and calls these):
//   void setExpressResponseViewDir(string)
//   void setExpressResponseViewRenderer(function<string(string, string)>)
// =============================================================================
#pragma once

#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "cp3/std/network.hpp"
#include "cp3/std/json.hpp"

namespace cp3::runtime::network {

// --- Paste §1: insert directly after `HttpResponse& cookie(…)` in network.hpp
// (all names below are members of class HttpResponse; return types preserve
// Express chaining for the header helpers and void for the terminal helpers).
//
//   // -- Express response helpers (express/response) -------------------------
//   HttpResponse& set(const std::string& name, const std::string& value);
//   HttpResponse& set(const std::vector<std::pair<std::string, std::string>>& headers);
//   HttpResponse& set(const StringMap& headers);
//   HttpResponse& append(const std::string& name, const std::string& value);
//   std::string get(const std::string& name) const;
//   HttpResponse& type(const std::string& type);
//   void format(const std::vector<std::pair<std::string, std::function<void()>>>& formats);
//   void render(const std::string& view);
//   void render(const std::string& view, const ::cp3::runtime::json::Value& locals);
//   void download(const std::string& path, const std::string& filename = "");
//   HttpResponse& attach(const std::string& filename);
//   HttpResponse& links(const std::vector<std::pair<std::string, std::string>>& links);
//   HttpResponse& location(const std::string& url);
//   HttpResponse& vary(const std::string& field);
//   HttpResponse& vary(const std::vector<std::string>& fields);
//   bool headersSent() const;
//   void varyImpl(const std::string& field);  // internal helper (codegen never calls it)

// View-layer configuration (app-scope wiring, see above).
void setExpressResponseViewDir(std::string dir);
void setExpressResponseViewRenderer(
    std::function<std::string(const std::string& view, const std::string& localsJson)> renderer);

}  // namespace cp3::runtime::network
