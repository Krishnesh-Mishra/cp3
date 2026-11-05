// =============================================================================
// src/std/network/express/body-parse/runtime_body_parse.cpp - Express
// body-parser equivalents: the json / urlencoded / text / raw middleware.
//
// OWNERSHIP: src/std/network/express/body-parse/** ONLY. Implements
// body_parse.hpp on top of the public HttpRequest/HttpResponse surface:
// the server buffers the body before dispatch, so a parser is a pure
// transform of that buffer (req.text()/bodySize()/headers) plus a setBody()
// write-back of the canonical result. No shared file is touched; the TU is
// selected by the marker row in README.md (WIRING).
//
// Reuse: JSON parsing/serialising is std/json (json::Parser via parseText,
// json::stringify); form decoding reuses std/url percentDecode
// (cp3/std/url.hpp, `::cp3::runtime::percentDecode`).
// =============================================================================
#include "cp3/std/network/express/body_parse.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <string_view>

#include "cp3/std/json.hpp"
#include "cp3/std/url.hpp"

namespace cp3::runtime::network::express::body_parse {

namespace {

using json::Value;

// ---------------------------------------------------------------------------
// Small string utilities
// ---------------------------------------------------------------------------

std::string lowerAscii(std::string text) {
    for (char& c : text) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return text;
}

std::string trimAscii(const std::string& text) {
    std::size_t begin = 0;
    while (begin < text.size() &&
           std::isspace(static_cast<unsigned char>(text[begin]))) {
        ++begin;
    }
    std::size_t end = text.size();
    while (end > begin &&
           std::isspace(static_cast<unsigned char>(text[end - 1]))) {
        --end;
    }
    return text.substr(begin, end - begin);
}

bool endsWith(std::string_view text, std::string_view suffix) {
    return text.size() >= suffix.size() &&
           text.substr(text.size() - suffix.size()) == suffix;
}

// ---------------------------------------------------------------------------
// Form decoding (std/url percentDecode + the urlencoded `+` rule)
// ---------------------------------------------------------------------------

std::string formDecode(const std::string& input) {
    std::string spaced = input;
    for (char& c : spaced) {
        if (c == '+') c = ' ';
    }
    return ::cp3::runtime::percentDecode(spaced);
}

// ---------------------------------------------------------------------------
// qs-style extended assignment over the json::Value tree
// ---------------------------------------------------------------------------

bool isArrayIndex(const std::string& key) {
    if (key.empty() || key.size() > 9) return false;
    for (char c : key) {
        if (c < '0' || c > '9') return false;
    }
    return true;
}

// qs arrayLimit (20): a numeric bracket above the limit is an object key,
// not an array index.
inline constexpr std::size_t kArrayLimit = 20;

bool wantsArray(const std::string& next) {
    return next.empty() ||
           (isArrayIndex(next) &&
            std::strtoul(next.c_str(), nullptr, 10) <= kArrayLimit);
}

Value makeContainer(bool array) {
    Value out;
    out.kind = array ? Value::Kind::Array : Value::Kind::Object;
    return out;
}

/// Coerces `slot` into the container the next segment needs. A scalar that
/// must become an object is dropped (qs keeps its own object semantics
/// here); a scalar that must become an array is wrapped as its first item.
void ensureContainer(Value& slot, bool array) {
    if (slot.kind == (array ? Value::Kind::Array : Value::Kind::Object)) return;
    if (slot.kind == Value::Kind::Null ||
        slot.kind == Value::Kind::Undefined) {
        slot = makeContainer(array);
        return;
    }
    if (array) {
        Value wrapped = makeContainer(true);
        wrapped.items.push_back(std::move(slot));
        slot = std::move(wrapped);
        return;
    }
    if (slot.kind == Value::Kind::Array) {
        // Index-keyed array -> object with numeric keys.
        Value obj = makeContainer(false);
        for (std::size_t i = 0; i < slot.items.size(); ++i) {
            obj.fields.emplace_back(std::to_string(i),
                                    std::move(slot.items[i]));
        }
        slot = std::move(obj);
        return;
    }
    slot = makeContainer(false);
}

Value* findField(Value& object, const std::string& key) {
    for (auto& field : object.fields) {
        if (field.key == key) return &field.value;
    }
    return nullptr;
}

/// Inserts `text` at `slot` following segments[pos..] (qs bracket path).
/// `pos == segments.size()` is the leaf: a repeated scalar key becomes an
/// array, matching qs/querystring duplicate handling.
void insertSegments(Value& slot, const std::vector<std::string>& segments,
                    std::size_t pos, const std::string& text) {
    if (pos >= segments.size()) {
        if (slot.kind == Value::Kind::Null ||
            slot.kind == Value::Kind::Undefined) {
            slot = json::value(text);
            return;
        }
        if (slot.kind == Value::Kind::Array) {
            slot.items.push_back(json::value(text));
            return;
        }
        // Repeated scalar key -> array of both (qs duplicate rule).
        Value both = makeContainer(true);
        both.items.push_back(std::move(slot));
        both.items.push_back(json::value(text));
        slot = std::move(both);
        return;
    }
    const std::string& seg = segments[pos];
    ensureContainer(slot, wantsArray(seg));
    if (slot.kind == Value::Kind::Array) {
        if (seg.empty()) {
            if (pos + 1 >= segments.size()) {
                slot.items.push_back(json::value(text));
            } else {
                Value child;
                insertSegments(child, segments, pos + 1, text);
                slot.items.push_back(std::move(child));
            }
            return;
        }
        // Numeric index (validated by wantsArray for arrays).
        const std::size_t index = static_cast<std::size_t>(
            std::strtoul(seg.c_str(), nullptr, 10));
        if (slot.items.size() <= index) slot.items.resize(index + 1);
        Value& child = slot.items[index];
        insertSegments(child, segments, pos + 1, text);
        return;
    }
    // Object slot.
    Value* child = findField(slot, seg);
    if (child == nullptr) {
        slot.fields.emplace_back(seg, Value{});
        child = &slot.fields.back().value;
    }
    insertSegments(*child, segments, pos + 1, text);
}

/// Splits one (already decoded) key into its path: `a[b][0][]` ->
/// [a, b, 0, ""]. More than `depth` brackets keeps the remainder literal
/// (qs `depth` rule). A malformed bracket tail makes the whole key flat.
std::vector<std::string> splitKey(const std::string& key, std::size_t depth) {
    std::vector<std::string> segments;
    const std::size_t open = key.find('[');
    if (open == std::string::npos) {
        segments.push_back(key);
        return segments;
    }
    segments.push_back(key.substr(0, open));
    std::size_t pos = open;
    while (pos < key.size() && segments.size() <= depth) {
        if (key[pos] != '[') {
            segments.clear();
            segments.push_back(key);
            return segments;
        }
        const std::size_t close = key.find(']', pos);
        if (close == std::string::npos) {
            segments.clear();
            segments.push_back(key);
            return segments;
        }
        segments.push_back(key.substr(pos + 1, close - pos - 1));
        pos = close + 1;
    }
    if (pos < key.size()) segments.push_back(key.substr(pos));
    return segments;
}

// ---------------------------------------------------------------------------
// Middleware plumbing
// ---------------------------------------------------------------------------

/// The buffered body, synchronously. HttpRequest::text() is an already
/// resolved promise (the server buffers before dispatch), so its value is
/// available without awaiting; a (defensively) unsettled promise reads "".
std::string bufferedBody(const HttpRequest& req) {
    auto promise = req.text();
    if (!promise.fulfilled()) return std::string();
    return promise.value();
}

/// Terminal parser failure: 400/413 with the client-safe message, no next().
/// (The next(err) swap is documented in README WIRING.)
Promise<void> failRequest(HttpResponse& res, int status,
                          const std::string& message) {
    res.status(static_cast<double>(status)).text(message);
    return Promise<void>::resolve();
}

bool overLimit(const HttpRequest& req, double limitBytes) {
    if (limitBytes <= 0.0) return false;  // <= 0 = unlimited (documented)
    if (!std::isfinite(limitBytes)) return false;
    return static_cast<double>(req.bodySize()) > limitBytes;
}

bool typeMatches(const std::string& contentType, const std::string& typeOption,
                 bool (*isParserType)(const std::string&)) {
    if (typeOption.empty()) return isParserType(contentType);
    const std::string wanted = lowerAscii(trimAscii(typeOption));
    if (wanted == "*/*") return !contentType.empty();
    return mediaTypeOf(contentType) == wanted;
}

bool isTextType(const std::string& contentType) {
    return mediaTypeOf(contentType) == "text/plain";
}

bool isRawType(const std::string& contentType) {
    return mediaTypeOf(contentType) == "application/octet-stream";
}

/// Double-parser guard for urlencoded: a first urlencoded/json parse writes
/// canonical JSON back, so a second urlencoded run sees a body starting
/// with '{' or '[' and must skip instead of re-parsing JSON as form text.
/// (A literal form body starting with '{'/'[' after '='-less truncation is
/// not valid form text anyway.) JSON re-parsing is idempotent, so json()
/// needs no guard.
bool looksParsed(const std::string& body) {
    const std::string trimmed = trimAscii(body);
    return !trimmed.empty() &&
           (trimmed.front() == '{' || trimmed.front() == '[');
}

}  // namespace

// ---------------------------------------------------------------------------
// limit + content-type helpers
// ---------------------------------------------------------------------------

double parseLimitText(const std::string& text) {
    const std::string trimmed = trimAscii(text);
    if (trimmed.empty()) {
        throw std::invalid_argument("body-parse: empty limit");
    }
    char* end = nullptr;
    const double value = std::strtod(trimmed.c_str(), &end);
    if (end == trimmed.c_str() || value < 0.0 || !std::isfinite(value)) {
        throw std::invalid_argument("body-parse: bad limit '" + text + "'");
    }
    std::string unit = lowerAscii(trimAscii(std::string(end)));
    double factor = 1.0;
    if (unit.empty() || unit == "b") {
        factor = 1.0;
    } else if (unit == "kb" || unit == "k") {
        factor = 1024.0;
    } else if (unit == "mb" || unit == "m") {
        factor = 1024.0 * 1024.0;
    } else if (unit == "gb" || unit == "g") {
        factor = 1024.0 * 1024.0 * 1024.0;
    } else if (unit == "tb" || unit == "t") {
        factor = 1024.0 * 1024.0 * 1024.0 * 1024.0;
    } else if (unit == "pb" || unit == "p") {
        factor = 1024.0 * 1024.0 * 1024.0 * 1024.0 * 1024.0;
    } else {
        throw std::invalid_argument("body-parse: bad limit unit '" + text + "'");
    }
    return value * factor;
}

std::string mediaTypeOf(const std::string& contentTypeHeader) {
    const std::size_t semi = contentTypeHeader.find(';');
    const std::string bare = semi == std::string::npos
                                 ? contentTypeHeader
                                 : contentTypeHeader.substr(0, semi);
    return lowerAscii(trimAscii(bare));
}

bool isJsonContentType(const std::string& contentTypeHeader) {
    const std::string media = mediaTypeOf(contentTypeHeader);
    if (media == "application/json") return true;
    // Any application/*+json (vendor JSON types, RFC 6839 suffix rule).
    return media.find('/') != std::string::npos && endsWith(media, "+json");
}

bool isUrlEncodedContentType(const std::string& contentTypeHeader) {
    return mediaTypeOf(contentTypeHeader) ==
           "application/x-www-form-urlencoded";
}

std::string headerValue(const HttpRequest& req, const std::string& name) {
    if (!req.headers) return std::string();
    const std::string wanted = lowerAscii(name);
    for (const auto& entry : *req.headers->entries()) {
        if (lowerAscii(entry.first) == wanted) return entry.second;
    }
    return std::string();
}

// ---------------------------------------------------------------------------
// Pure parsers
// ---------------------------------------------------------------------------

Value parseJsonBody(const std::string& text, bool strict) {
    if (trimAscii(text).empty()) return json::objectValue(json::Fields{});
    Value root = json::parseText(text);
    if (strict && root.kind != Value::Kind::Object &&
        root.kind != Value::Kind::Array) {
        throw json::ParseError(
            "strict mode: top-level JSON value must be an object or array");
    }
    return root;
}

FormPairs parseFormPairs(const std::string& text) {
    FormPairs pairs;
    std::size_t pos = 0;
    while (pos <= text.size()) {
        const std::size_t amp = text.find('&', pos);
        const std::string part = amp == std::string::npos
                                     ? text.substr(pos)
                                     : text.substr(pos, amp - pos);
        if (!part.empty()) {
            const std::size_t eq = part.find('=');
            if (eq == std::string::npos) {
                pairs.emplace_back(formDecode(part), std::string());
            } else {
                pairs.emplace_back(formDecode(part.substr(0, eq)),
                                   formDecode(part.substr(eq + 1)));
            }
        }
        if (amp == std::string::npos) break;
        pos = amp + 1;
    }
    return pairs;
}

Value parseUrlEncodedBody(const std::string& text, bool extended) {
    Value root = json::objectValue(json::Fields{});
    const FormPairs pairs = parseFormPairs(text);
    // qs parameterLimit (1000): pairs beyond the limit are dropped.
    const std::size_t limit = extended ? 1000 : pairs.size();
    const std::size_t count = std::min(limit, pairs.size());
    for (std::size_t i = 0; i < count; ++i) {
        const std::string& key = pairs[i].first;
        const std::string& value = pairs[i].second;
        if (extended) {
            const std::vector<std::string> segments = splitKey(key, 5);
            insertSegments(root, segments, 0, value);
        } else {
            Value* slot = findField(root, key);
            if (slot == nullptr) {
                root.fields.emplace_back(key, json::value(value));
            } else {
                insertSegments(*slot, {}, 0, value);
            }
        }
    }
    return root;
}

// ---------------------------------------------------------------------------
// req.body accessors
// ---------------------------------------------------------------------------

std::string rawBodyText(const HttpRequest& req) {
    return bufferedBody(req);
}

buffer::Buffer rawBodyBuffer(const HttpRequest& req) {
    return buffer::from(bufferedBody(req));
}

Value parsedBody(const HttpRequest& req) {
    const std::string text = bufferedBody(req);
    if (trimAscii(text).empty()) return json::objectValue(json::Fields{});
    try {
        return json::parseText(text);
    } catch (const json::ParseError&) {
        // text/raw bodies (and any unparsed body) read back as a string.
        return json::value(text);
    }
}

StringMap formBodyMap(const HttpRequest& req) {
    StringMap map = collections::makeMap<std::string, std::string>();
    const Value body = parsedBody(req);
    if (body.kind != Value::Kind::Object) return map;
    for (const auto& field : body.fields) {
        std::string value;
        switch (field.value.kind) {
            case Value::Kind::String: value = field.value.text; break;
            case Value::Kind::Number: value = json::stringify(field.value.number); break;
            case Value::Kind::Bool: value = field.value.boolean ? "true" : "false"; break;
            case Value::Kind::Null: value = "null"; break;
            default: value = json::stringify(field.value); break;
        }
        map->set(field.key, value);
    }
    return map;
}

// ---------------------------------------------------------------------------
// BodyParseError
// ---------------------------------------------------------------------------

BodyParseError BodyParseError::fromThrown(Thrown const& t, int fallback) {
    if (const BodyParseError* err = t.get<BodyParseError>()) return *err;
    BodyParseError out;
    out.status = fallback;
    out.code = "EPARSE";
    out.message = "Bad Request";
    return out;
}

// ---------------------------------------------------------------------------
// Middleware factories
// ---------------------------------------------------------------------------

MiddlewareFn makeJsonMiddleware(JsonBodyOptions options) {
    return [options = std::move(options)](HttpRequest& req, HttpResponse& res,
                                          Next& next) mutable -> Promise<void> {
        const std::string contentType = headerValue(req, "content-type");
        if (!typeMatches(contentType, options.type, isJsonContentType)) {
            next();
            return Promise<void>::resolve();
        }
        if (overLimit(req, options.limitBytes)) {
            return failRequest(res, 413, "request entity too large");
        }
        const std::string body = bufferedBody(req);
        try {
            const Value parsed = parseJsonBody(body, options.strict);
            req.setBody(json::stringify(parsed));
        } catch (const json::ParseError& err) {
            return failRequest(res, 400,
                               std::string("invalid JSON: ") + err.what());
        }
        next();
        return Promise<void>::resolve();
    };
}

MiddlewareFn makeUrlEncodedMiddleware(UrlEncodedBodyOptions options) {
    return [options = std::move(options)](HttpRequest& req, HttpResponse& res,
                                          Next& next) mutable -> Promise<void> {
        const std::string contentType = headerValue(req, "content-type");
        if (!typeMatches(contentType, options.type, isUrlEncodedContentType)) {
            next();
            return Promise<void>::resolve();
        }
        const std::string body = bufferedBody(req);
        if (looksParsed(body)) {  // an earlier parser already ran
            next();
            return Promise<void>::resolve();
        }
        if (overLimit(req, options.limitBytes)) {
            return failRequest(res, 413, "request entity too large");
        }
        const Value parsed = parseUrlEncodedBody(body, options.extended);
        req.setBody(json::stringify(parsed));
        next();
        return Promise<void>::resolve();
    };
}

MiddlewareFn makeTextMiddleware(TextBodyOptions options) {
    return [options = std::move(options)](HttpRequest& req, HttpResponse& res,
                                          Next& next) mutable -> Promise<void> {
        const std::string contentType = headerValue(req, "content-type");
        if (!typeMatches(contentType, options.type, isTextType)) {
            next();
            return Promise<void>::resolve();
        }
        if (overLimit(req, options.limitBytes)) {
            return failRequest(res, 413, "request entity too large");
        }
        // The body is already the string; nothing to rewrite.
        next();
        return Promise<void>::resolve();
    };
}

MiddlewareFn makeRawMiddleware(RawBodyOptions options) {
    return [options = std::move(options)](HttpRequest& req, HttpResponse& res,
                                          Next& next) mutable -> Promise<void> {
        const std::string contentType = headerValue(req, "content-type");
        if (!typeMatches(contentType, options.type, isRawType)) {
            next();
            return Promise<void>::resolve();
        }
        if (overLimit(req, options.limitBytes)) {
            return failRequest(res, 413, "request entity too large");
        }
        // Raw bytes stay buffered verbatim (rawBodyBuffer downstream).
        next();
        return Promise<void>::resolve();
    };
}

// ---------------------------------------------------------------------------
// sema / codegen helpers
// ---------------------------------------------------------------------------

namespace sema_codegen {

std::string middlewareCallText(const std::string& name,
                               const std::string& optionsExpr) {
    const std::string ns = "::cp3::runtime::network::express::body_parse::";
    const std::string options = optionsExpr.empty() ? std::string() : optionsExpr;
    if (name == "json") return ns + "makeJsonMiddleware(" + options + ")";
    if (name == "urlencoded") {
        return ns + "makeUrlEncodedMiddleware(" + options + ")";
    }
    if (name == "text") return ns + "makeTextMiddleware(" + options + ")";
    if (name == "raw") return ns + "makeRawMiddleware(" + options + ")";
    return std::string();
}

std::string parsedBodyCallText(const std::string& reqExpr) {
    return "::cp3::runtime::network::express::body_parse::parsedBody(" +
           reqExpr + ")";
}

std::string validateOptionKey(const std::string& parser,
                              const std::string& key) {
    if (key == "inflate" || key == "verify") {
        return "body-parse: option '" + key + "' is not supported (no zlib / "
               "no verify hook in this build)";
    }
    if (key == "limit" || key == "type") return std::string();
    if (parser == "json" && key == "strict") return std::string();
    if (parser == "urlencoded" && key == "extended") return std::string();
    return "body-parse: unknown option '" + key + "' for " + parser + "()";
}

}  // namespace sema_codegen

}  // namespace cp3::runtime::network::express::body_parse
