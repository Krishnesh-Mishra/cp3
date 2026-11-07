// =============================================================================
// src/std/network/webserver/response/runtime/runtime_webserver_response.cpp -
// webserver response helpers (`res.set`/`append`/`get`/`type`/`format`/`render`/
// `download`/`attach`/`links`/`location`/`vary`/`headersSent`).
//
// OWNERSHIP: the std/network family, webserver response scope ONLY
// (src/std/network/webserver/response/**). Defines the HttpResponse members
// declared by the wiring snippet in include/cp3/std/network/webserver_response.hpp
// (paste §1 below into src/std/network/include/cp3/std/network.hpp). No new
// backend: headers live in detail::ResponseImpl (runtime_server_internal.hpp),
// file bytes go through the existing HttpResponse::sendFile/text, and the
// view layer is a pluggable renderer (default: serve views/<view>.html).
//
// LOCK DISCIPLINE / LIFETIME: members run on the runtime loop thread like the
// rest of the HttpResponse surface; impl_ is null/unbound-safe (every method
// is a no-op or empty value, mirroring status/header/text).
// =============================================================================
#include "cp3/std/network.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "cp3/std/json.hpp"

#include "../../../server/runtime_server_internal.hpp"

namespace cp3::runtime::network {

namespace {

using ResponseImpl = detail::ResponseImpl;

std::string lowerAscii(const std::string& text) {
    std::string out = text;
    for (char& c : out) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return out;
}

void trimInPlace(std::string& text) {
    const std::size_t first = text.find_first_not_of(" \t");
    if (first == std::string::npos) { text.clear(); return; }
    const std::size_t last = text.find_last_not_of(" \t");
    text = text.substr(first, last - first + 1);
}

bool hasHeader(const ResponseImpl& impl, const char* name) {
    return std::any_of(impl.headers.begin(), impl.headers.end(),
        [&](const auto& p) { return lowerAscii(p.first) == name; });
}

std::string getHeader(const ResponseImpl& impl, const std::string& name) {
    const std::string want = lowerAscii(name);
    for (const auto& p : impl.headers) {
        if (lowerAscii(p.first) == want) return p.second;
    }
    return {};
}

void setHeader(ResponseImpl& impl, const std::string& name, const std::string& value) {
    // Replace semantics: exactly one line remains (webserver res.set).
    const std::string want = lowerAscii(name);
    bool found = false;
    for (std::size_t i = 0; i < impl.headers.size();) {
        if (lowerAscii(impl.headers[i].first) == want) {
            if (!found) { impl.headers[i].second = value; found = true; ++i; }
            else impl.headers.erase(impl.headers.begin() + static_cast<long>(i));
        } else ++i;
    }
    if (!found) impl.headers.emplace_back(name, value);
}

void appendHeader(ResponseImpl& impl, const std::string& name, const std::string& value) {
    // webserver res.append: comma-join when the field already exists.
    const std::string existing = getHeader(impl, name);
    if (existing.empty()) {
        impl.headers.emplace_back(name, value);
        return;
    }
    setHeader(impl, name, existing + ", " + value);
}

std::string baseName(const std::string& path) {
    namespace fs = std::filesystem;
    return fs::path(path).filename().string();
}

std::string mimeForType(const std::string& type) {
    if (type.find('/') != std::string::npos) return type;
    const std::string ext = lowerAscii(type);
    if (ext == "html" || ext == ".html") return "text/html; charset=utf-8";
    if (ext == "json" || ext == ".json") return "application/json; charset=utf-8";
    if (ext == "text" || ext == "txt" || ext == ".txt") return "text/plain; charset=utf-8";
    if (ext == "xml" || ext == ".xml") return "application/xml; charset=utf-8";
    if (ext == "js" || ext == ".js") return "text/javascript; charset=utf-8";
    if (ext == "css" || ext == ".css") return "text/css; charset=utf-8";
    if (ext == "png" || ext == ".png") return "image/png";
    if (ext == "jpg" || ext == "jpeg" || ext == ".jpg" || ext == ".jpeg") return "image/jpeg";
    if (ext == "gif" || ext == ".gif") return "image/gif";
    if (ext == "svg" || ext == ".svg") return "image/svg+xml";
    if (ext == "ico" || ext == ".ico") return "image/x-icon";
    if (ext == "pdf" || ext == ".pdf") return "application/pdf";
    return "application/" + ext;
}

std::string quotedFilename(const std::string& filename) {
    std::string out;
    for (char c : filename) {
        if (c == '"' || c == '\\') out.push_back('\\');
        out.push_back(c);
    }
    return out;
}

std::string contentDispositionAttachment(const std::string& filename) {
    return "attachment; filename=\"" + quotedFilename(filename) + "\"";
}

bool isTerminal(const ResponseImpl* impl) {
    return impl == nullptr || impl->finished;
}

// ---------------------------------------------------------------------------
// View layer state (webserver `app.set('views', dir)` / `app.engine()` seam).
// The app-scope subagent owns the `app.set`/`app.engine` surface; this is the
// render-side sink it calls into. Thread: written at setup, read on the loop
// thread; a plain static is enough (no handler runs before configure).
// ---------------------------------------------------------------------------
// (Setters are defined in the enclosing cp3::runtime::network scope below:
// the anonymous-namespace statics are visible there through the implicit
// using-directive the unnamed namespace grants its enclosing scope.)
std::string g_viewDir = "views";
std::function<std::string(const std::string&, const std::string&)> g_viewRenderer;

}  // namespace

void setwebserverResponseViewDir(std::string dir) {
    if (!dir.empty()) g_viewDir = std::move(dir);
}

void setwebserverResponseViewRenderer(
    std::function<std::string(const std::string&, const std::string&)> renderer) {
    g_viewRenderer = std::move(renderer);
}

// --- Headers -----------------------------------------------------------------
HttpResponse& HttpResponse::set(const std::string& name, const std::string& value) {
    if (impl_ && !impl_->finished) setHeader(*impl_, name, value);
    return *this;
}

HttpResponse& HttpResponse::set(
    const std::vector<std::pair<std::string, std::string>>& headers) {
    if (impl_ && !impl_->finished) {
        for (const auto& [k, v] : headers) setHeader(*impl_, k, v);
    }
    return *this;
}

HttpResponse& HttpResponse::set(const StringMap& headers) {
    if (impl_ && !impl_->finished && headers) {
        // `entries()` returns an owning `Array` (a `Ref`); keep it in a named
        // local so the vector outlives the loop (no deref of a prvalue Ref).
        const auto entries = headers->entries();
        for (const auto& [k, v] : *entries) setHeader(*impl_, k, v);
    }
    return *this;
}

HttpResponse& HttpResponse::append(const std::string& name, const std::string& value) {
    if (impl_ && !impl_->finished) appendHeader(*impl_, name, value);
    return *this;
}

std::string HttpResponse::get(const std::string& name) const {
    if (!impl_) return {};
    return getHeader(*impl_, name);
}

// --- Content-Type ------------------------------------------------------------
HttpResponse& HttpResponse::type(const std::string& type) {
    if (impl_ && !impl_->finished) setHeader(*impl_, "Content-Type", mimeForType(type));
    return *this;
}

// --- Content negotiation -----------------------------------------------------
void HttpResponse::format(
    const std::vector<std::pair<std::string, std::function<void()>>>& formats) {
    if (!impl_ || impl_->finished || formats.empty()) return;
    // webserver negotiates against the request Accept header. The response
    // object alone carries no request, so the documented fallback applies:
    // the first entry wins unless a `default` entry exists. Codegen passes
    // only handler lambdas; the media-type key also sets Content-Type when
    // the handler leaves it unset (webserver behaviour).
    const auto itDefault =
        std::find_if(formats.begin(), formats.end(),
                     [](const auto& p) { return p.first == "default"; });
    const auto& chosen = (itDefault != formats.end() && formats.size() > 1)
        ? *itDefault : formats.front();
    if (chosen.first != "default" && chosen.first.find('/') != std::string::npos) {
        if (!hasHeader(*impl_, "content-type")) {
            setHeader(*impl_, "Content-Type", chosen.first);
        }
    }
    if (chosen.second) chosen.second();
}

// --- Template rendering ------------------------------------------------------
void HttpResponse::render(const std::string& view) {
    render(view, ::cp3::runtime::json::Value{});
}

void HttpResponse::render(const std::string& view,
                          const ::cp3::runtime::json::Value& locals) {
    if (!impl_ || impl_->finished) return;
    const std::string localsJson = ::cp3::runtime::json::stringify(locals);
    std::string html;
    if (g_viewRenderer) {
        html = g_viewRenderer(view, localsJson);
    } else {
        namespace fs = std::filesystem;
        std::error_code ec;
        fs::path candidate = fs::path(g_viewDir) / fs::path(view);
        if (!candidate.has_extension()) candidate += ".html";
        std::ifstream in(candidate, std::ios::binary);
        if (in) {
            html.assign(std::istreambuf_iterator<char>(in),
                        std::istreambuf_iterator<char>());
        } else {
            impl_->status = 500.0;
            impl_->body = "Failed to render view '" + view + "'";
            // Direct buffered send (sendBuffered is internal to
            // runtime_server.cpp): mark finished so the dispatcher stops.
            impl_->headers.emplace_back("Content-Type", "text/plain; charset=utf-8");
            impl_->headStarted = true;
            impl_->finished = true;
            if (auto* native = static_cast<detail::NativeResponse*>(impl_->native)) {
                native->writeStatus(std::to_string(static_cast<long long>(impl_->status)) + " Internal Server Error");
                for (const auto& [n, v] : impl_->headers) {
                    if (lowerAscii(n) == "content-length") continue;
                    native->writeHeader(n, v);
                }
                native->end(impl_->body);
            }
            return;
        }
    }
    setHeader(*impl_, "Content-Type", "text/html; charset=utf-8");
    // Deliver through text(): it reuses the buffered-send path and honours
    // the Content-Type set above. (text() would otherwise force plain.)
    text(html);
}

// --- File attachment ----------------------------------------------------------
HttpResponse& HttpResponse::attach(const std::string& filename) {
    if (impl_ && !impl_->finished) {
        setHeader(*impl_, "Content-Disposition",
                  contentDispositionAttachment(baseName(filename).empty() ? filename : baseName(filename)));
    }
    return *this;
}

void HttpResponse::download(const std::string& path, const std::string& filename) {
    if (!impl_ || impl_->finished) return;
    const std::string name = filename.empty() ? baseName(path) : filename;
    setHeader(*impl_, "Content-Disposition", contentDispositionAttachment(name));
    // Content-Type is inferred by sendFile (extension-based) when unset.
    sendFile(path);
}

// --- Link / Location / Vary ---------------------------------------------------
HttpResponse& HttpResponse::links(
    const std::vector<std::pair<std::string, std::string>>& links) {
    if (impl_ && !impl_->finished) {
        for (const auto& [rel, href] : links) {
            appendHeader(*impl_, "Link", "<" + href + ">; rel=\"" + rel + "\"");
        }
    }
    return *this;
}

HttpResponse& HttpResponse::location(const std::string& url) {
    if (impl_ && !impl_->finished) setHeader(*impl_, "Location", url);
    return *this;
}

void HttpResponse::varyImpl(const std::string& field) {
    if (!impl_ || impl_->finished) return;
    std::string value = field;
    trimInPlace(value);
    if (value.empty()) return;
    const std::string existing = getHeader(*impl_, "Vary");
    if (existing == "*") return;
    if (value == "*") { setHeader(*impl_, "Vary", "*"); return; }
    if (!existing.empty()) {
        // Tokenise the existing header and skip duplicates (case-insensitive,
        // webserver `vary` semantics).
        std::string rest = existing;
        while (!rest.empty()) {
            const std::size_t comma = rest.find(',');
            std::string token = (comma == std::string::npos) ? rest : rest.substr(0, comma);
            trimInPlace(token);
            if (lowerAscii(token) == lowerAscii(value)) return;
            if (comma == std::string::npos) break;
            rest = rest.substr(comma + 1);
        }
        setHeader(*impl_, "Vary", existing + ", " + value);
        return;
    }
    setHeader(*impl_, "Vary", value);
}

HttpResponse& HttpResponse::vary(const std::string& field) {
    varyImpl(field);
    return *this;
}

HttpResponse& HttpResponse::vary(const std::vector<std::string>& fields) {
    for (const auto& f : fields) varyImpl(f);
    return *this;
}

// --- State --------------------------------------------------------------------
bool HttpResponse::headersSent() const {
    if (!impl_) return false;
    return impl_->headStarted || impl_->finished;
}

}  // namespace cp3::runtime::network
