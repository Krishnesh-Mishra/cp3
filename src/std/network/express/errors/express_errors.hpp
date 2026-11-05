// =============================================================================
// src/std/network/express/errors/express_errors.hpp - Express error handling
// + static/sendFile options (ADDITIVE layer, owned by this directory).
//
// OWNERSHIP: src/std/network/express/errors/** only. This header does NOT
// modify cp3/std/network.hpp, sema.cpp, codegen*.cpp or the dispatcher; it
// builds on their public surface (HttpRequest/HttpResponse/Next/Thrown,
// MiddlewareFn, detail::ErrorFn) so the lead can wire it in with the lines
// in README.md. Nothing here is included by generated code until wiring lands.
//
// EXPRESS MAPPING
//   * 4-arg error middleware: (err, req, res, next) -> ErrorHandlerFn
//   * next(err): NextError::operator()(Thrown|HttpError) diverts to the
//     error-handler list; next() continues the normal list.
//   * err.status / err.expose: HttpError (status 4xx => expose by default,
//     5xx => hidden message in production).
//   * Default handler: HTML page with status text; stack only in development.
//   * sendFile(path, options): root/dotfiles/etag/maxAge/headers/fallthrough.
//   * static(dir, options): fallthrough/index/redirect + ETag 304.
//
// ENV: development iff getenv("NODE_ENV")=="development" or
//      getenv("CP3_ENV")=="development" (checked per-request, not cached).
// =============================================================================
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "cp3/std/network.hpp"

namespace cp3::runtime::network::express::errors {

// ---------------------------------------------------------------------------
// HttpError: err.status / err.expose / err.message / err.stack
// ---------------------------------------------------------------------------
struct HttpError {
    int status = 500;
    bool expose = false;  // true => message safe to show to the client
    std::string message;
    std::string code;     // optional machine code, e.g. "ENOENT"
    std::string stack;    // captured at construction (dev only rendered)

    HttpError() = default;
    HttpError(int status_, std::string message_, bool expose_ = false,
              std::string code_ = {})
        : status(status_), expose(expose_), message(std::move(message_)),
          code(std::move(code_)) {}
    // Express spelling helpers.
    int statusCode() const { return status; }
    bool exposed() const { return expose; }

    Thrown toThrown() const { return Thrown::make(*this); }
    static bool is(Thrown const& t) { return t.get<HttpError>() != nullptr; }
    static HttpError fromThrown(Thrown const& t, int fallback = 500);
};

/// Convenience factories (each sets expose = status < 500).
HttpError httpError(int status, std::string message, std::string code = {});
HttpError badRequest(std::string message = "Bad Request");
HttpError unauthorized(std::string message = "Unauthorized");
HttpError forbidden(std::string message = "Forbidden");
HttpError notFound(std::string message = "Not Found");
HttpError methodNotAllowed(std::string message = "Method Not Allowed");
HttpError payloadTooLarge(std::string message = "Payload Too Large");
HttpError internalError(std::string message = "Internal Server Error");

// ---------------------------------------------------------------------------
// Status text / HTML rendering / environment
// ---------------------------------------------------------------------------
std::string statusText(int status);
std::string escapeHtml(const std::string& s);
bool isDevelopment();
bool isProduction();

struct ErrorHandlerOptions {
    bool development = false;  // true => show stack + real message
    bool forceDevelopment = false;
    bool forceProduction = false;
};

/// The default HTML error page. Production: status text only, no stack,
/// no err.message unless err.expose. Development: message + <pre>stack</pre>.
std::string renderErrorHtml(HttpError const& err, bool development);
std::string renderErrorHtml(Thrown const& err, bool development);

/// Default 4-arg handler: writes renderErrorHtml as text/html. Terminal.
using ErrorHandlerFn = std::function<Promise<void>(HttpError, HttpRequest&,
                                                     HttpResponse&, class NextError&)>;
ErrorHandlerFn defaultErrorHandler(ErrorHandlerOptions opts = {});

// ---------------------------------------------------------------------------
// NextError: next() continues, next(err) diverts to error handlers.
// ---------------------------------------------------------------------------
class NextError {
public:
    NextError() = default;
    explicit NextError(std::function<Promise<void>()> cont,
                       std::function<Promise<void>(Thrown)> errCont)
        : cont_(std::move(cont)), errCont_(std::move(errCont)) {}
    Promise<void> operator()() {
        if (cont_) return cont_();
        return Promise<void>::resolve();
    }
    Promise<void> operator()(Thrown err) {
        if (errCont_) return errCont_(std::move(err));
        // No error handler downstream: reject so the outer dispatcher 500s.
        return Promise<void>::reject(std::move(err));
    }
    Promise<void> operator()(HttpError err) { return (*this)(err.toThrown()); }
    // Express also allows next(null)/next(undefined) == next().
    Promise<void> operator()(std::nullptr_t) { return (*this)(); }

private:
    std::function<Promise<void>()> cont_;
    std::function<Promise<void>(Thrown)> errCont_;
};

/// Normal (3-arg) handler spelling used inside an ErrorChain.
using RouteHandlerFn = std::function<Promise<void>(HttpRequest&, HttpResponse&,
                                                    NextError&)>;
/// Per-route + global error handler (4-arg).
using ErrorMiddlewareFn = ErrorHandlerFn;

// ---------------------------------------------------------------------------
// ErrorChain: ordered normal handlers + ordered error handlers.
//   * runChain: next() -> next normal; next(err)/throw/reject -> error list.
//   * runErrorChain: next() -> next error handler (same err);
//     next(newErr) -> next error handler (new err); exhausted -> default.
//   * If res.sent() at any point, the chain stops (Express terminal rule).
// Usable directly, or as ONE app.use() middleware via asMiddleware().
// ---------------------------------------------------------------------------
class ErrorChain {
public:
    void use(RouteHandlerFn h) { handlers_.push_back(std::move(h)); }
    void useError(ErrorMiddlewareFn h) { errors_.push_back(std::move(h)); }
    /// Keep registration order across both lists.
    void useErrorFirst(ErrorMiddlewareFn h) { errors_.insert(errors_.begin(), std::move(h)); }
    std::size_t size() const { return handlers_.size(); }
    std::size_t errorSize() const { return errors_.size(); }

    Promise<void> run(HttpRequest& req, HttpResponse& res);
    Promise<void> run(HttpRequest& req, HttpResponse& res, ErrorHandlerOptions opts);

private:
    std::vector<RouteHandlerFn> handlers_;
    std::vector<ErrorMiddlewareFn> errors_;
};

/// Wrap a chain as a single MiddlewareFn for the existing app.use().
MiddlewareFn asMiddleware(ErrorChain chain, ErrorHandlerOptions opts = {});
/// Wrap a 4-arg handler as the existing 3-arg app.onError() handler
/// (next is a no-op that resolves; use ErrorChain for real next(err) flow).
detail::ErrorFn asErrorHandler(ErrorMiddlewareFn h);
/// Template-friendly adapters mirroring detail::makeRoute/makeMiddleware.
template <class F>
RouteHandlerFn makeRouteHandler(F fn) {
    return [fn = std::move(fn)](HttpRequest& req, HttpResponse& res,
                                 NextError& next) mutable -> Promise<void> {
        return detail::callAsPromise(fn, req, res, next);
    };
}
template <class F>
ErrorMiddlewareFn makeErrorMiddleware(F fn) {
    return [fn = std::move(fn)](HttpError err, HttpRequest& req, HttpResponse& res,
                                 NextError& next) mutable -> Promise<void> {
        return detail::callAsPromise(fn, err, req, res, next);
    };
}

// ---------------------------------------------------------------------------
// sendFile options (Express res.sendFile(path, options[, callback]))
// ---------------------------------------------------------------------------
struct SendFileOptions {
    std::string root;                 // join + confine; "" = path as given
    std::string dotfiles = "ignore";  // "allow" | "deny" | "ignore"
    bool etag = true;
    bool lastModified = true;
    double maxAge = 0.0;              // seconds; 0 = no Cache-Control
    StringMap headers;                // extra headers (override defaults)
    bool fallthrough = true;          // true: missing => return false (next())
                                      // false: missing => throw 404
    bool acceptRanges = true;         // Range is NOT implemented; header only
};

/// Serve one file. Returns true when a response was written (incl. 304),
/// false when fallthrough applies (caller should call next()). Throws
/// HttpError(403/404) when fallthrough==false. HEAD: headers only.
bool sendFileWithOptions(HttpResponse& res, HttpRequest const& req,
                         std::string const& path, SendFileOptions const& opts);

/// Convenience: res.sendFile(path, options) as a terminal call.
/// Returns false only in fallthrough-miss (no response written).
bool sendFile(HttpResponse& res, HttpRequest const& req, std::string const& path,
              SendFileOptions const& opts = {});

// ---------------------------------------------------------------------------
// static options (express.static(root, options))
// ---------------------------------------------------------------------------
struct StaticOptions {
    std::string root;                 // required (or pass dir separately)
    std::string dotfiles = "ignore";  // "allow" | "deny" | "ignore"
    bool etag = true;
    bool lastModified = true;
    double maxAge = 0.0;
    bool fallthrough = true;          // miss => next(), else 404
    bool index = true;                // serve index.html in directories
    std::string indexName = "index.html";
    bool redirect = true;             // dir without "/" => 301 to path+"/"
    StringMap headers;
    bool acceptRanges = true;
};

/// express.static middleware. GET/HEAD only; other methods fall through.
MiddlewareFn makeStatic(StaticOptions options);
/// Overload taking the directory positionally (Express spelling).
MiddlewareFn makeStatic(std::string dir, StaticOptions options = {});

}  // namespace cp3::runtime::network::express::errors
