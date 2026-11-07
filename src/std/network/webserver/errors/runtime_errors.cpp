// =============================================================================
// src/std/network/webserver/errors/runtime_errors.cpp - implementation.
// Builds only on cp3/std/network.hpp public surface + std::filesystem.
// No dispatcher/server internals are touched.
// =============================================================================
#include "webserver_errors.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace cp3::runtime::network::webserver::errors {
namespace {

std::string lowerAscii(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string headerGet(HttpRequest const& req, const std::string& name) {
    if (!req.headers) return {};
    auto v = req.headers->get(lowerAscii(name));
    return v ? *v : std::string{};
}

std::string b64ish(std::uint64_t v) {
    // Weak ETag body: hex size + hex mtime (webserver uses size-mtime).
    std::ostringstream o;
    o << std::hex << v;
    return o.str();
}

std::string httpDate(std::int64_t epochSec) {
    static const char* kDays[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    static const char* kMonths[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    std::int64_t days = epochSec / 86400;
    std::int64_t rem = epochSec % 86400;
    if (rem < 0) { rem += 86400; --days; }
    int wday = static_cast<int>((days + 4) % 7);  // 1970-01-01 Thu
    // Civil-from-days (Howard Hinnant algorithm).
    std::int64_t z = days + 719468;
    std::int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    std::uint64_t doe = static_cast<std::uint64_t>(z - era * 146097);
    std::uint64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    std::int64_t y = static_cast<std::int64_t>(yoe) + era * 400;
    std::uint64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    std::uint64_t mp = (5 * doy + 2) / 153;
    std::uint64_t d = doy - (153 * mp + 2) / 5 + 1;
    std::uint64_t m = mp < 10 ? mp + 3 : mp - 9;
    y += (m <= 2);
    (void)y;
    std::ostringstream o;
    o << kDays[wday] << ", " << (d < 10 ? "0" : "") << d << " " << kMonths[m - 1]
      << " " << y << " " << (rem / 3600 < 10 ? "0" : "") << rem / 3600 << ":"
      << ((rem / 60) % 60 < 10 ? "0" : "") << (rem / 60) % 60 << ":"
      << (rem % 60 < 10 ? "0" : "") << rem % 60 << " GMT";
    return o.str();
}

std::int64_t parseHttpDate(const std::string& s) {
    // Minimal IMF-fixdate parse: find year/month/day after first comma.
    // Returns -1 when unparseable. Only used for If-Modified-Since.
    static const char* kMonths[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    int mon = -1;
    for (int i = 0; i < 12; ++i) if (s.find(kMonths[i]) != std::string::npos) mon = i;
    if (mon < 0) return -1;
    // Extract all numbers in order: day, year, hh, mm, ss.
    std::vector<long> nums;
    for (std::size_t i = 0; i < s.size();) {
        if (std::isdigit(static_cast<unsigned char>(s[i]))) {
            long v = 0;
            while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i])))
                v = v * 10 + (s[i++] - '0');
            nums.push_back(v);
        } else ++i;
    }
    if (nums.size() < 6) return -1;
    long day = nums[0], year = nums[1], hh = nums[2], mm = nums[3], ss = nums[4];
    // days-from-civil
    long y = year - (mon < 2 ? 1 : 0);
    long era = (y >= 0 ? y : y - 399) / 400;
    long yoe = y - era * 400;
    long doy = (153 * (mon + (mon > 1 ? -3 : 9)) + 2) / 5 + day - 1;
    long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    long days = era * 146097 + doe - 719468;
    return days * 86400 + hh * 3600 + mm * 60 + ss;
}

bool pathHasDotSegment(const std::filesystem::path& p) {
    for (auto const& part : p) {
        std::string s = part.string();
        if (!s.empty() && s[0] == '.') return true;
    }
    return false;
}

std::string fileContentType(const std::string& path) {
    const std::size_t dot = path.find_last_of('.');
    if (dot == std::string::npos) return "application/octet-stream";
    std::string ext = lowerAscii(path.substr(dot));
    if (ext == ".html" || ext == ".htm") return "text/html; charset=utf-8";
    if (ext == ".css") return "text/css; charset=utf-8";
    if (ext == ".js" || ext == ".mjs") return "text/javascript; charset=utf-8";
    if (ext == ".json") return "application/json; charset=utf-8";
    if (ext == ".txt") return "text/plain; charset=utf-8";
    if (ext == ".svg") return "image/svg+xml";
    if (ext == ".png") return "image/png";
    if (ext == ".jpg" || ext == ".jpeg") return "image/jpeg";
    if (ext == ".gif") return "image/gif";
    if (ext == ".ico") return "image/x-icon";
    if (ext == ".xml") return "application/xml; charset=utf-8";
    if (ext == ".wasm") return "application/wasm";
    if (ext == ".woff2") return "font/woff2";
    return "application/octet-stream";
}

void applyExtraHeaders(HttpResponse& res, StringMap const& headers) {
    if (!headers) return;
    // `entries()` returns an owning `Array` (a `Ref`); keep it in a named
    // local so the vector outlives the loop (no deref of a prvalue Ref).
    const auto entries = headers->entries();
    for (auto const& [k, v] : *entries) res.header(k, v);
}

}  // namespace

// --- HttpError ---------------------------------------------------------------
HttpError HttpError::fromThrown(Thrown const& t, int fallback) {
    if (auto* e = t.get<HttpError>()) return *e;
    if (auto err = t.asError()) {
        HttpError out;
        out.status = fallback;
        out.message = err->message;
        out.stack = err->stack;
        out.expose = fallback < 500;
        return out;
    }
    HttpError out;
    out.status = fallback;
    out.message = t.toString();
    out.expose = fallback < 500;
    return out;
}

HttpError httpError(int status, std::string message, std::string code) {
    HttpError e(status, std::move(message), status < 500, std::move(code));
    return e;
}
HttpError badRequest(std::string m) { return httpError(400, std::move(m)); }
HttpError unauthorized(std::string m) { return httpError(401, std::move(m)); }
HttpError forbidden(std::string m) { return httpError(403, std::move(m)); }
HttpError notFound(std::string m) { return httpError(404, std::move(m)); }
HttpError methodNotAllowed(std::string m) { return httpError(405, std::move(m)); }
HttpError payloadTooLarge(std::string m) { return httpError(413, std::move(m)); }
HttpError internalError(std::string m) { return httpError(500, std::move(m), "EINTERNAL"); }

// --- status text / env -------------------------------------------------------
std::string statusText(int status) {
    switch (status) {
        case 200: return "OK";
        case 201: return "Created";
        case 202: return "Accepted";
        case 204: return "No Content";
        case 301: return "Moved Permanently";
        case 302: return "Found";
        case 304: return "Not Modified";
        case 400: return "Bad Request";
        case 401: return "Unauthorized";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 409: return "Conflict";
        case 413: return "Payload Too Large";
        case 422: return "Unprocessable Entity";
        case 429: return "Too Many Requests";
        case 500: return "Internal Server Error";
        case 502: return "Bad Gateway";
        case 503: return "Service Unavailable";
        default: return "Error";
    }
}

std::string escapeHtml(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            case '\'': out += "&#39;"; break;
            default: out.push_back(c);
        }
    }
    return out;
}

bool isDevelopment() {
    const char* a = std::getenv("NODE_ENV");
    const char* b = std::getenv("CP3_ENV");
    return (a && std::string(a) == "development") || (b && std::string(b) == "development");
}
bool isProduction() { return !isDevelopment(); }

std::string renderErrorHtml(HttpError const& err, bool development) {
    const int status = err.status >= 100 && err.status < 600 ? err.status : 500;
    const std::string phrase = statusText(status);
    std::ostringstream o;
    o << "<!DOCTYPE html>\n<html lang=\"en\">\n<head>\n<meta charset=\"utf-8\">\n<title>"
      << status << " " << escapeHtml(phrase) << "</title>\n</head>\n<body>\n<h1>"
      << status << " " << escapeHtml(phrase) << "</h1>\n";
    if (development) {
        if (!err.message.empty())
            o << "<p>" << escapeHtml(err.message) << "</p>\n";
        if (!err.stack.empty())
            o << "<pre>" << escapeHtml(err.stack) << "</pre>\n";
    } else if (err.expose && !err.message.empty()) {
        o << "<p>" << escapeHtml(err.message) << "</p>\n";
    }
    o << "</body>\n</html>\n";
    return o.str();
}
std::string renderErrorHtml(Thrown const& err, bool development) {
    return renderErrorHtml(HttpError::fromThrown(err), development);
}

ErrorHandlerFn defaultErrorHandler(ErrorHandlerOptions opts) {
    return [opts](HttpError err, HttpRequest& req, HttpResponse& res,
                  NextError&) mutable -> Promise<void> {
        if (res.sent()) return Promise<void>::resolve();
        bool dev = opts.forceDevelopment ? true
                   : opts.forceProduction ? false : (opts.development || isDevelopment());
        int status = err.status >= 100 && err.status < 600 ? err.status : 500;
        (void)req;
        res.status(static_cast<double>(status))
            .header("Content-Type", "text/html; charset=utf-8")
            .header("X-Content-Type-Options", "nosniff");
        res.text(renderErrorHtml(err, dev));
        return Promise<void>::resolve();
    };
}

// --- ErrorChain ---------------------------------------------------------------
namespace {
struct ChainState {
    std::vector<RouteHandlerFn> handlers;
    std::vector<ErrorMiddlewareFn> errors;
    ErrorHandlerOptions opts;
};

Promise<void> runErrorAt(std::shared_ptr<ChainState> st, std::size_t ei,
                         HttpError err, HttpRequest& req, HttpResponse& res);
Promise<void> runNormalAt(std::shared_ptr<ChainState> st, std::size_t hi,
                          HttpRequest& req, HttpResponse& res);

Promise<void> runErrorAt(std::shared_ptr<ChainState> st, std::size_t ei,
                         HttpError err, HttpRequest& req, HttpResponse& res) {
    if (res.sent()) return Promise<void>::resolve();
    if (ei >= st->errors.size()) {
        return defaultErrorHandler(st->opts)(err, req, res, NextError{});
    }
    auto handler = st->errors[ei];
    NextError next(
        [st, ei, err, &req, &res]() mutable -> Promise<void> {
            return runErrorAt(st, ei + 1, err, req, res);
        },
        [st, ei, &req, &res](Thrown t) mutable -> Promise<void> {
            return runErrorAt(st, ei + 1, HttpError::fromThrown(t), req, res);
        });
    // Capture state by value in continuations (async handlers may outlive
    // the synchronous stack frame); req/res are owned by the dispatcher.
    try {
        Promise<void> p = handler(err, req, res, next);
        if (!p.settled()) {
            return p.then(
                []() {},
                [st, ei, &req, &res](Thrown t) mutable -> Promise<void> {
                    return runErrorAt(st, ei + 1, HttpError::fromThrown(t), req, res);
                });
        }
        if (p.rejected()) throw p.reason();
        return Promise<void>::resolve();
    } catch (...) {
        return runErrorAt(st, ei + 1, HttpError::fromThrown(Thrown::fromCurrentException()), req, res);
    }
}

Promise<void> runNormalAt(std::shared_ptr<ChainState> st, std::size_t hi,
                          HttpRequest& req, HttpResponse& res) {
    if (res.sent() || hi >= st->handlers.size()) return Promise<void>::resolve();
    auto handler = st->handlers[hi];
    auto advanced = std::make_shared<bool>(false);
    NextError next(
        [st, hi, advanced, &req, &res]() mutable -> Promise<void> {
            *advanced = true;
            return runNormalAt(st, hi + 1, req, res);
        },
        [st, &req, &res](Thrown t) mutable -> Promise<void> {
            return runErrorAt(st, 0, HttpError::fromThrown(t), req, res);
        });
    try {
        Promise<void> p = handler(req, res, next);
        if (!p.settled()) {
            // Async handler: its rejection diverts to the error list.
            return p.then(
                []() {},
                [st, &req, &res](Thrown t) mutable -> Promise<void> {
                    return runErrorAt(st, 0, HttpError::fromThrown(t), req, res);
                });
        }
        if (p.rejected()) return runErrorAt(st, 0, HttpError::fromThrown(p.reason()), req, res);
        return Promise<void>::resolve();
    } catch (...) {
        return runErrorAt(st, 0, HttpError::fromThrown(Thrown::fromCurrentException()), req, res);
    }
}
}  // namespace

Promise<void> ErrorChain::run(HttpRequest& req, HttpResponse& res) {
    return run(req, res, ErrorHandlerOptions{});
}
Promise<void> ErrorChain::run(HttpRequest& req, HttpResponse& res, ErrorHandlerOptions opts) {
    auto st = std::make_shared<ChainState>();
    st->handlers = handlers_;
    st->errors = errors_;
    st->opts = opts;
    return runNormalAt(st, 0, req, res);
}

MiddlewareFn asMiddleware(ErrorChain chain, ErrorHandlerOptions opts) {
    return [chain = std::move(chain), opts](HttpRequest& req, HttpResponse& res,
                                             Next& outerNext) mutable -> Promise<void> {
        // Run the inner chain; on exhaustion without a response, hand back
        // to the outer chain via next() (fallthrough, webserver semantics).
        Promise<void> p = chain.run(req, res, opts);
        if (!p.settled()) {
            return p.then([&res, outerNext]() mutable {
                if (!res.sent()) outerNext();
            });
        }
        if (p.rejected()) throw p.reason();
        if (!res.sent()) outerNext();
        return Promise<void>::resolve();
    };
}

detail::ErrorFn asErrorHandler(ErrorMiddlewareFn h) {
    return [h = std::move(h)](Thrown err, HttpRequest& req, HttpResponse& res) mutable -> Promise<void> {
        if (res.sent()) return Promise<void>::resolve();
        NextError next;
        return h(HttpError::fromThrown(err), req, res, next);
    };
}

// --- sendFile -----------------------------------------------------------------
bool sendFileWithOptions(HttpResponse& res, HttpRequest const& req,
                         std::string const& path, SendFileOptions const& opts) {
    namespace fs = std::filesystem;
    if (res.sent()) return true;
    std::error_code ec;
    fs::path file = fs::path(path);
    if (!opts.root.empty()) {
        fs::path base = fs::weakly_canonical(fs::path(opts.root), ec);
        if (ec) { if (opts.fallthrough) return false; throw notFound(); }
        fs::path joined = fs::weakly_canonical(base / file.lexically_normal(), ec);
        if (ec) { if (opts.fallthrough) return false; throw notFound(); }
        std::string b = base.string(), c = joined.string();
        bool inside = c == b || (c.size() > b.size() && c.compare(0, b.size(), b) == 0 &&
                                 (c[b.size()] == '/' || c[b.size()] == '\\'));
        if (!inside) {
            if (opts.fallthrough) return false;
            throw forbidden("Forbidden");
        }
        file = joined;
    }
    // Dotfiles: check the final path relative to root (or the path itself).
    if (opts.dotfiles != "allow" && pathHasDotSegment(file.filename().string().empty() ? file : file)) {
        // Only the requested segments count, not the root prefix.
        fs::path rel = opts.root.empty() ? file : fs::relative(file, fs::path(opts.root), ec);
        if (!ec && pathHasDotSegment(rel)) {
            if (opts.dotfiles == "deny") {
                if (!opts.fallthrough) throw forbidden("Forbidden");
                return false;
            }
            return false;  // "ignore": pretend missing
        }
    }
    if (!fs::is_regular_file(file, ec)) {
        if (opts.fallthrough) return false;
        throw notFound();
    }

    std::uintmax_t size = fs::file_size(file, ec);
    auto mtime = fs::last_write_time(file, ec);
    std::int64_t mtimeSec = ec ? 0 : static_cast<std::int64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(mtime.time_since_epoch()).count());
    std::string etag;
    if (opts.etag) etag = "W/\"" + b64ish(static_cast<std::uint64_t>(size)) + "-" + b64ish(static_cast<std::uint64_t>(mtimeSec)) + "\"";

    // Conditional requests -> 304 (webserver/serve-static behaviour).
    std::string inm = headerGet(req, "if-none-match");
    if (opts.etag && !etag.empty() && !inm.empty() &&
        (inm == etag || inm == "*" || inm.find(etag) != std::string::npos)) {
        if (!etag.empty()) res.header("ETag", etag);
        if (opts.maxAge > 0) res.header("Cache-Control", "public, max-age=" + std::to_string(static_cast<long long>(opts.maxAge)));
        if (opts.lastModified && mtimeSec > 0) res.header("Last-Modified", httpDate(mtimeSec));
        applyExtraHeaders(res, opts.headers);
        res.status(304).send();
        return true;
    }
    std::string ims = headerGet(req, "if-modified-since");
    if (opts.lastModified && mtimeSec > 0 && !ims.empty()) {
        std::int64_t since = parseHttpDate(ims);
        if (since >= 0 && mtimeSec <= since) {
            if (!etag.empty()) res.header("ETag", etag);
            res.header("Last-Modified", httpDate(mtimeSec));
            if (opts.maxAge > 0) res.header("Cache-Control", "public, max-age=" + std::to_string(static_cast<long long>(opts.maxAge)));
            applyExtraHeaders(res, opts.headers);
            res.status(304).send();
            return true;
        }
    }

    if (!etag.empty()) res.header("ETag", etag);
    if (opts.lastModified && mtimeSec > 0) res.header("Last-Modified", httpDate(mtimeSec));
    if (opts.maxAge > 0) res.header("Cache-Control", "public, max-age=" + std::to_string(static_cast<long long>(opts.maxAge)));
    if (opts.acceptRanges) res.header("Accept-Ranges", "bytes");
    res.header("Content-Type", fileContentType(file.string()));
    applyExtraHeaders(res, opts.headers);

    if (req.method == "HEAD") { res.send(); return true; }
    // Delegate bytes to the existing sendFile (preserves our headers).
    res.sendFile(file.string());
    return true;
}

bool sendFile(HttpResponse& res, HttpRequest const& req, std::string const& path,
              SendFileOptions const& opts) {
    return sendFileWithOptions(res, req, path, opts);
}

// --- static -------------------------------------------------------------------
MiddlewareFn makeStatic(StaticOptions options) {
    return [options = std::move(options)](HttpRequest& req, HttpResponse& res,
                                           Next& next) mutable -> Promise<void> {
        if (req.method != "GET" && req.method != "HEAD") { next(); return Promise<void>::resolve(); }
        namespace fs = std::filesystem;
        std::error_code ec;
        if (options.root.empty()) { next(); return Promise<void>::resolve(); }
        fs::path base = fs::weakly_canonical(fs::path(options.root), ec);
        if (ec) { next(); return Promise<void>::resolve(); }
        // req.path is URL-decoded by the app layer in webserver; here it is raw.
        fs::path candidate = fs::weakly_canonical(base / fs::path(req.path).lexically_normal(), ec);
        if (ec) { next(); return Promise<void>::resolve(); }
        std::string b = base.string(), c = candidate.string();
        bool inside = c == b || (c.size() > b.size() && c.compare(0, b.size(), b) == 0 &&
                                 (c[b.size()] == '/' || c[b.size()] == '\\'));
        if (!inside) { next(); return Promise<void>::resolve(); }

        SendFileOptions sf;
        sf.root = options.root; sf.dotfiles = options.dotfiles; sf.etag = options.etag;
        sf.lastModified = options.lastModified; sf.maxAge = options.maxAge;
        sf.headers = options.headers; sf.fallthrough = options.fallthrough;
        sf.acceptRanges = options.acceptRanges;

        if (fs::is_directory(candidate, ec)) {
            bool trailingSlash = !req.path.empty() && req.path.back() == '/';
            if (!trailingSlash && options.redirect) {
                res.status(301).header("Location", req.path + "/").send();
                return Promise<void>::resolve();
            }
            if (options.index) {
                fs::path indexFile = candidate / options.indexName;
                if (fs::is_regular_file(indexFile, ec)) {
                    sendFileWithOptions(res, req, indexFile.string(), sf);
                    return Promise<void>::resolve();
                }
            }
            next();
            return Promise<void>::resolve();
        }
        if (fs::is_regular_file(candidate, ec)) {
            // sendFileWithOptions re-derives from root; pass the relative path.
            std::string rel = fs::relative(candidate, base, ec).string();
            sendFileWithOptions(res, req, rel, sf);
            return Promise<void>::resolve();
        }
        if (!options.fallthrough) throw notFound();
        next();
        return Promise<void>::resolve();
    };
}
MiddlewareFn makeStatic(std::string dir, StaticOptions options) {
    options.root = std::move(dir);
    return makeStatic(std::move(options));
}

}  // namespace cp3::runtime::network::webserver::errors
