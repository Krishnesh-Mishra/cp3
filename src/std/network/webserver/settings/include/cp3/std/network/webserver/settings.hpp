// =============================================================================
// cp3/std/network/webserver/settings.hpp - webserver settings + view engine backing.
//
// OWNERSHIP: the std/network/webserver/settings family
// (src/std/network/webserver/settings/**). Additive to the generated-code ABI
// (D14): every function is `inline`, header-only; there is no runtime TU to
// link. Generated code includes this header only when a program uses the
// webserver settings surface (codegen sets `needswebserverSettings_`).
//
// -----------------------------------------------------------------------------
// CODEGEN CONTRACT
// -----------------------------------------------------------------------------
//   app.enable(name) / app.disable(name) / app.enabled(name)
//       -> the webserverApp member spellings below (chainable, like webserver).
//   app.set(name, value) / app.get(name)
//       -> `setSetting` / `getSetting`; the value crosses the boundary as
//          `SettingValue` (string | number | boolean | null | string[]).
//   app.locals.<key> = value / app.locals.<key> / app.locals["key"]
//       -> `Locals::set/getOr`, keyed by the member/index name.
//   res.locals.<key> = value / res.locals.<key>
//       -> the per-request `Locals` from `resLocalsFor(res)` (see below).
//   app.engine(ext, fn) -> `setEngine(ext, fn)`; `fn` is
//       `std::string (const std::string& file, const Locals& options)`.
//   app.set("views", dir) + app.set("view engine", ext)
//       -> `Settings` entries consumed by `webserverApp::renderToString`.
//   res.render(view, options?) -> `renderResponse(res, view, options)`:
//       resolves the file under `views`, dispatches to the registered engine
//       for its extension, and answers `text/html` through `res.text`.
//   app.render(view, options?) -> `renderToString` (webserver' callback-less
//       spelling; CP3 returns the HTML string synchronously).
//   req.protocol -> `requestProtocol(req)`; `req.ip` is the field the
//       dispatcher fills through `applyTrustProxy` (see `RequestScope`).
//
// -----------------------------------------------------------------------------
// SEMANTICS (webserver 4/5 parity, with the divergences marked)
// -----------------------------------------------------------------------------
//   * Setting names are case-sensitive and space-sensitive, exactly like
//     webserver ("trust proxy", "view engine", "x-powered-by"). No aliasing.
//   * `app.get(name)` on a missing setting yields `SettingValue` null
//     (webserver yields `undefined`; CP3 has no `undefined` value to hand
//     back here, and `null` keeps `=== null` / truthiness checks honest).
//   * Setting values are scalars only: string, number, boolean, null, or a
//     string array (the "views" list / "trust proxy" subnet list shapes).
//     There is no function-valued setting: webserver accepts a function for
//     "trust proxy" (a custom `trustProxy(addr)` predicate); that predicate
//     form is NOT supported (see the trust-proxy section below).
//   * `res.render` merges options with precedence
//     render-options > res.locals > app.locals (webserver' merge order).
//   * View lookup: `viewsDir / view`, appending `"." + viewEngine` when the
//     view name carries no extension; an absolute view name is used as-is.
//     A missing file throws `Error` with webserver' "Failed to lookup view"
//     wording, so the dispatcher's error path (500 / `onError`) sees it.
//   * No template language is bundled. When no engine is registered for an
//     extension, `renderToString` falls back to the trivial interpolation
//     engine (`makeTrivialEngine`): `{{ key }}` replaced by the option's
//     string form, missing keys by "". This is a deliberate divergence from
//     webserver (which errors with "No view engine registered"): it makes
//     `res.render` useful without a third-party engine, but it performs NO
//     HTML escaping and is not safe for untrusted data. Register a real
//     engine with `app.engine` to replace it per extension.
//   * `req.ip` / `req.protocol` honour the "trust proxy" setting only when
//     the dispatcher runs the request through `RequestScope` (or calls
//     `applyTrustProxy`). A bare `createServer` handler chain never calls
//     it, so `req.ip` stays the socket address and `req.protocol` is "http".
//   * Settings are not thread-safe for concurrent mutation (webserver apps
//     mutate settings before `listen`, then only read). The scaled server
//     (D79) runs one loop per thread: construct the app once, configure it,
//     then hand the same instance to every loop.
// =============================================================================
#pragma once

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "cp3/core/error.hpp"
#include "cp3/core/runtime.hpp"
#include "cp3/std/network.hpp"

namespace cp3::runtime::webserver {

namespace detail {
inline std::string toLowerAscii(std::string text) {
    for (char& c : text) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return text;
}
inline std::string trimAscii(const std::string& text) {
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
}  // namespace detail

// =============================================================================
// SettingValue - the `app.set` / `app.get` value carrier
// =============================================================================
/// The heterogeneous value of one webserver setting or one locals entry:
/// `string | number | boolean | null | string[]`. The cross-kind accessors
/// (`asString`, `asNumber`, `asBool`) coerce scalar kinds the way JS would
/// (`String(42)`, `Number("42")`, truthiness); a kind with no JS scalar
/// coercion (a string list read as a number, say) throws `Error` instead of
/// guessing, so a mistyped setting fails loudly at the read site.
class SettingValue {
public:
    enum class Kind { Null, Bool, Number, String, StringList };

    SettingValue() = default;  // Null
    SettingValue(std::nullptr_t) {}
    SettingValue(bool value) : kind_(Kind::Bool), boolean_(value) {}
    SettingValue(double value) : kind_(Kind::Number), number_(value) {}
    SettingValue(const std::string& value)
        : kind_(Kind::String), text_(value) {}
    SettingValue(std::string&& value)
        : kind_(Kind::String), text_(std::move(value)) {}
    SettingValue(const char* value)
        : kind_(Kind::String), text_(value ? value : "") {}
    SettingValue(const std::vector<std::string>& value)
        : kind_(Kind::StringList), list_(value) {}
    SettingValue(std::vector<std::string>&& value)
        : kind_(Kind::StringList), list_(std::move(value)) {}

    static SettingValue list(std::vector<std::string> value) {
        return SettingValue(std::move(value));
    }

    [[nodiscard]] Kind kind() const { return kind_; }
    [[nodiscard]] bool isNull() const { return kind_ == Kind::Null; }
    [[nodiscard]] bool isBool() const { return kind_ == Kind::Bool; }
    [[nodiscard]] bool isNumber() const { return kind_ == Kind::Number; }
    [[nodiscard]] bool isString() const { return kind_ == Kind::String; }
    [[nodiscard]] bool isList() const { return kind_ == Kind::StringList; }

    /// JS truthiness: null, false, 0, NaN and "" are falsy; everything else
    /// (including an empty string list) is truthy.
    [[nodiscard]] bool truthy() const {
        switch (kind_) {
            case Kind::Null: return false;
            case Kind::Bool: return boolean_;
            case Kind::Number: return number_ != 0.0 && !std::isnan(number_);
            case Kind::String: return !text_.empty();
            case Kind::StringList: return true;
        }
        return false;
    }

    [[nodiscard]] bool asBool() const {
        if (kind_ == Kind::StringList) {
            throw Error("SettingValue: a string list has no boolean form");
        }
        return truthy();
    }

    [[nodiscard]] double asNumber() const {
        switch (kind_) {
            case Kind::Number: return number_;
            case Kind::Bool: return boolean_ ? 1.0 : 0.0;
            case Kind::Null: return 0.0;
            case Kind::String: {
                try {
                    std::size_t used = 0;
                    const double parsed = std::stod(text_, &used);
                    if (used != text_.size()) break;
                    return parsed;
                } catch (...) {
                    break;
                }
            }
            case Kind::StringList: break;
        }
        throw Error("SettingValue: '" + asString() + "' is not a number");
    }

    [[nodiscard]] std::string asString() const {
        switch (kind_) {
            case Kind::Null: return "null";
            case Kind::Bool: return boolean_ ? "true" : "false";
            case Kind::Number: return toString(number_);
            case Kind::String: return text_;
            case Kind::StringList: return "(" + std::to_string(list_.size()) +
                                          " items)";
        }
        return "";
    }

    [[nodiscard]] const std::vector<std::string>& asList() const {
        if (kind_ != Kind::StringList) {
            throw Error("SettingValue: expected a string list, got '" +
                        asString() + "'");
        }
        return list_;
    }

    // Implicit scalar views, so generated code can use an `app.get(...)`
    // result where a string / number / boolean is expected
    // (`const dir: string = app.get("views")`). The comparisons below keep
    // `app.get("x") === true` / `=== "production"` working without a cast.
    operator std::string() const { return asString(); }
    operator double() const { return asNumber(); }
    explicit operator bool() const { return truthy(); }

private:
    Kind kind_ = Kind::Null;
    bool boolean_ = false;
    double number_ = 0.0;
    std::string text_;
    std::vector<std::string> list_;
};

inline bool operator==(const SettingValue& a, const SettingValue& b) {
    if (a.kind() != b.kind()) return false;
    switch (a.kind()) {
        case SettingValue::Kind::Null: return true;
        case SettingValue::Kind::Bool:
            return a.asBool() == b.asBool();
        case SettingValue::Kind::Number:
            return a.asNumber() == b.asNumber();
        case SettingValue::Kind::String:
            return a.asString() == b.asString();
        case SettingValue::Kind::StringList:
            return a.asList() == b.asList();
    }
    return false;
}
inline bool operator!=(const SettingValue& a, const SettingValue& b) {
    return !(a == b);
}
inline bool operator==(const SettingValue& a, const std::string& b) {
    return a.isString() && a.asString() == b;
}
inline bool operator==(const SettingValue& a, const char* b) {
    return a == std::string(b ? b : "");
}
inline bool operator==(const std::string& a, const SettingValue& b) {
    return b == a;
}
inline bool operator==(const SettingValue& a, bool b) {
    return a.isBool() && a.asBool() == b;
}
inline bool operator==(bool a, const SettingValue& b) { return b == a; }
inline bool operator==(const SettingValue& a, double b) {
    return a.isNumber() && a.asNumber() == b;
}
inline bool operator==(double a, const SettingValue& b) { return b == a; }
inline bool operator==(const SettingValue& a, std::nullptr_t) {
    return a.isNull();
}
inline bool operator==(std::nullptr_t, const SettingValue& a) {
    return a.isNull();
}
inline bool operator!=(const SettingValue& a, const std::string& b) {
    return !(a == b);
}
inline bool operator!=(const SettingValue& a, const char* b) {
    return !(a == b);
}
inline bool operator!=(const SettingValue& a, bool b) { return !(a == b); }
inline bool operator!=(const SettingValue& a, double b) { return !(a == b); }
inline bool operator!=(const SettingValue& a, std::nullptr_t) {
    return !(a == nullptr);
}

// =============================================================================
// Settings - the `app.set` / `app.get` / enable table
// =============================================================================
/// The webserver settings table. `enable(name)` / `disable(name)` are exactly
/// `set(name, true)` / `set(name, false)` (webserver' contract), and `enabled`
/// is the truthiness of the stored value. The constructor preloads the
/// webserver defaults a program can rely on without calling `set` first;
/// "view engine" is deliberately absent (webserver has no default engine).
class Settings {
public:
    Settings() {
        values_["env"] = SettingValue("development");
        values_["trust proxy"] = SettingValue(false);
        values_["views"] = SettingValue("./views");
        values_["x-powered-by"] = SettingValue(true);
        values_["etag"] = SettingValue("weak");
        values_["json spaces"] = SettingValue(0.0);
        values_["strict routing"] = SettingValue(false);
        values_["case sensitive routing"] = SettingValue(false);
        values_["subdomain offset"] = SettingValue(2.0);
    }

    void set(const std::string& name, SettingValue value) {
        values_[name] = std::move(value);
    }

    [[nodiscard]] std::optional<SettingValue> get(
        const std::string& name) const {
        const auto it = values_.find(name);
        if (it == values_.end()) return std::nullopt;
        return it->second;
    }

    [[nodiscard]] SettingValue getOr(const std::string& name,
                                     SettingValue fallback = SettingValue()) const {
        const auto it = values_.find(name);
        if (it == values_.end()) return fallback;
        return it->second;
    }

    [[nodiscard]] bool has(const std::string& name) const {
        return values_.find(name) != values_.end();
    }

    void remove(const std::string& name) { values_.erase(name); }

    void enable(const std::string& name) { set(name, SettingValue(true)); }
    void disable(const std::string& name) { set(name, SettingValue(false)); }
    [[nodiscard]] bool enabled(const std::string& name) const {
        return getOr(name).truthy();
    }

    [[nodiscard]] std::string getString(
        const std::string& name, const std::string& fallback = "") const {
        const auto value = get(name);
        if (!value || value->isNull()) return fallback;
        return value->asString();
    }

    [[nodiscard]] bool getBool(const std::string& name,
                               bool fallback = false) const {
        const auto value = get(name);
        if (!value) return fallback;
        return value->asBool();
    }

    [[nodiscard]] double getNumber(const std::string& name,
                                   double fallback = 0.0) const {
        const auto value = get(name);
        if (!value) return fallback;
        return value->asNumber();
    }

    [[nodiscard]] const std::map<std::string, SettingValue>& all() const {
        return values_;
    }

private:
    std::map<std::string, SettingValue> values_;
};

// =============================================================================
// Locals - `app.locals` / `res.locals`: the open key/value bag
// =============================================================================
/// The `app.locals` / `res.locals` bag: an open string-keyed map of
/// `SettingValue`s. CP3 code reaches it through member/index syntax
/// (`app.locals.title`, `app.locals["title"]`); codegen lowers those onto
/// `set` / `getOr` with the property name as the key. A missing key reads
/// back as null (never throws), matching a plain JS object's `undefined`
/// read for interpolation purposes.
class Locals {
public:
    void set(const std::string& name, SettingValue value) {
        values_[name] = std::move(value);
    }

    [[nodiscard]] std::optional<SettingValue> get(
        const std::string& name) const {
        const auto it = values_.find(name);
        if (it == values_.end()) return std::nullopt;
        return it->second;
    }

    [[nodiscard]] SettingValue getOr(const std::string& name) const {
        return getOr(name, SettingValue());
    }

    [[nodiscard]] SettingValue getOr(const std::string& name,
                                     SettingValue fallback) const {
        const auto it = values_.find(name);
        if (it == values_.end()) return fallback;
        return it->second;
    }

    [[nodiscard]] bool has(const std::string& name) const {
        return values_.find(name) != values_.end();
    }

    void remove(const std::string& name) { values_.erase(name); }
    void clear() { values_.clear(); }

    /// Overlay `other`'s entries onto this bag (other wins on a key clash).
    void mergeFrom(const Locals& other) {
        for (const auto& [key, value] : other.values_) {
            values_[key] = value;
        }
    }

    [[nodiscard]] const std::map<std::string, SettingValue>& entries() const {
        return values_;
    }

private:
    std::map<std::string, SettingValue> values_;
};

/// The options argument of `res.render(view, options)` / the second
/// parameter of a view engine function: a locals-shaped bag.
using RenderOptions = Locals;

// =============================================================================
// View engines - `app.engine(ext, fn)` + `res.render` lookup
// =============================================================================
/// A view engine function: renders `file` (the resolved template path) with
/// the merged options and returns the HTML. In CP3 this is the lambda
/// `(file, options) => string` passed to `app.engine(ext, fn)`; a failure is
/// a thrown `Error`, which the dispatcher turns into a 500 (or the
/// `onError` handler's response), exactly like an engine calling back with
/// an error in webserver.
using ViewEngineFn =
    std::function<std::string(const std::string& file, const Locals& options)>;

namespace detail {
/// Normalizes an extension for registry lookup: drops one leading dot and
/// lowercases, so "html", ".html" and ".HTML" name the same engine.
inline std::string normalizeExt(const std::string& ext) {
    std::string out = ext;
    if (!out.empty() && out.front() == '.') out.erase(out.begin());
    return toLowerAscii(out);
}

/// Reads a whole template file; a missing/unreadable file throws `Error`
/// with webserver' lookup wording (the dispatcher answers 500).
inline std::string readViewFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw Error("Failed to lookup view \"" + path +
                    "\" (template file not found)");
    }
    std::string text((std::istreambuf_iterator<char>(in)),
                     std::istreambuf_iterator<char>());
    return text;
}
}  // namespace detail

/// The trivial interpolation engine's renderer: every `{{ key }}` (inner
/// whitespace ignored) is replaced by the option's string form, a missing
/// key by "". No escaping, no control flow, no partials: this is the
/// bundled fallback only, not a template language.
inline std::string renderTrivialTemplate(const std::string& text,
                                         const Locals& options) {
    std::string out;
    out.reserve(text.size());
    std::size_t pos = 0;
    while (true) {
        const std::size_t open = text.find("{{", pos);
        if (open == std::string::npos) {
            out += text.substr(pos);
            break;
        }
        const std::size_t close = text.find("}}", open + 2);
        if (close == std::string::npos) {
            out += text.substr(pos);
            break;
        }
        out += text.substr(pos, open - pos);
        const std::string key =
            detail::trimAscii(text.substr(open + 2, close - open - 2));
        if (!key.empty()) {
            const auto value = options.get(key);
            if (value && !value->isNull()) out += value->asString();
        }
        pos = close + 2;
    }
    return out;
}

/// The engine behind the fallback: reads the file and interpolates it.
/// Register it explicitly with `app.engine(".html", makeTrivialEngine())`
/// when a program wants the fallback semantics pinned per extension.
inline ViewEngineFn makeTrivialEngine() {
    return [](const std::string& file, const Locals& options) {
        return renderTrivialTemplate(detail::readViewFile(file), options);
    };
}

/// Adapts an webserver-style callback engine
/// `(file, options, callback) => void` (callback invoked once with the HTML
/// string) to the `ViewEngineFn` shape. A callback that never fires, or an
/// engine that throws, surfaces as a thrown `Error` either way.
template <class F>
ViewEngineFn engineFromCallback(F fn) {
    return [fn = std::move(fn)](const std::string& file,
                                 const Locals& options) mutable {
        std::string html;
        bool called = false;
        fn(file, options, [&](std::string out) {
            html = std::move(out);
            called = true;
        });
        if (!called) {
            throw Error("View engine for \"" + file +
                        "\" did not invoke its callback");
        }
        return html;
    };
}

/// The registered view engines, keyed by normalized extension.
class ViewRegistry {
public:
    void setEngine(const std::string& ext, ViewEngineFn fn) {
        engines_[detail::normalizeExt(ext)] = std::move(fn);
    }

    [[nodiscard]] bool hasEngine(const std::string& ext) const {
        return engines_.find(detail::normalizeExt(ext)) != engines_.end();
    }

    [[nodiscard]] ViewEngineFn findEngine(const std::string& ext) const {
        const auto it = engines_.find(detail::normalizeExt(ext));
        if (it == engines_.end()) return ViewEngineFn();
        return it->second;
    }

    /// Renders `file` with the engine for `ext`; without one, the trivial
    /// interpolation engine runs (see the header comment). A missing file
    /// throws the lookup `Error` from `detail::readViewFile` — unless a
    /// registered engine reads the file itself, in which case its own
    /// failure propagates.
    [[nodiscard]] std::string render(const std::string& file,
                                     const std::string& ext,
                                     const Locals& options) const {
        if (const ViewEngineFn engine = findEngine(ext)) {
            return engine(file, options);
        }
        return makeTrivialEngine()(file, options);
    }

private:
    std::map<std::string, ViewEngineFn> engines_;
};

// =============================================================================
// Trust proxy - "trust proxy" setting -> req.ip / req.protocol
// =============================================================================
/// The parsed form of the "trust proxy" setting. webserver accepts
/// `boolean | number | string | string[] | function`; the function form
/// (a custom per-address predicate) is not supported. A string names one
/// of the built-in ranges ("loopback", "linklocal", "uniquelocal",
/// "trust all") or a hop count ("1"); any other string is treated as an
/// untrusted subnet name and behaves like `false` (documented divergence:
/// arbitrary CIDR / hostname trust lists are not implemented).
struct TrustProxyConfig {
    enum class Mode { None, All, Hops, Loopback, LinkLocal, UniqueLocal };
    Mode mode = Mode::None;
    int hops = 0;
};

inline TrustProxyConfig parseTrustProxy(const SettingValue& value) {
    TrustProxyConfig config;
    switch (value.kind()) {
        case SettingValue::Kind::Null:
            return config;
        case SettingValue::Kind::Bool:
            config.mode = value.asBool() ? TrustProxyConfig::Mode::All
                                         : TrustProxyConfig::Mode::None;
            return config;
        case SettingValue::Kind::Number: {
            const double n = value.asNumber();
            if (n > 0 && std::isfinite(n)) {
                config.mode = TrustProxyConfig::Mode::Hops;
                config.hops = static_cast<int>(std::floor(n));
            }
            return config;
        }
        case SettingValue::Kind::String: {
            const std::string name =
                detail::toLowerAscii(detail::trimAscii(value.asString()));
            if (name == "loopback") {
                config.mode = TrustProxyConfig::Mode::Loopback;
            } else if (name == "linklocal") {
                config.mode = TrustProxyConfig::Mode::LinkLocal;
            } else if (name == "uniquelocal") {
                config.mode = TrustProxyConfig::Mode::UniqueLocal;
            } else if (name == "trust all" || name == "all") {
                config.mode = TrustProxyConfig::Mode::All;
            } else if (!name.empty() &&
                       std::all_of(name.begin(), name.end(), [](char c) {
                           return std::isdigit(static_cast<unsigned char>(c));
                       })) {
                config.mode = TrustProxyConfig::Mode::Hops;
                try {
                    config.hops = std::stoi(name);
                } catch (...) {
                    config.mode = TrustProxyConfig::Mode::None;
                }
            }
            return config;
        }
        case SettingValue::Kind::StringList: {
            // A list trusts when every listed hop is in a named range; the
            // supported approximation is "trust all" when the list is
            // non-empty (each entry is assumed to name a trusted hop).
            if (!value.asList().empty()) {
                config.mode = TrustProxyConfig::Mode::All;
            }
            return config;
        }
    }
    return config;
}

namespace detail {
/// Parses a dotted-quad IPv4 address into its four octets; false for
/// anything else (IPv6, hostnames, malformed input).
inline bool parseIpv4(const std::string& ip, unsigned char octets[4]) {
    std::size_t pos = 0;
    for (int part = 0; part < 4; ++part) {
        if (pos >= ip.size()) return false;
        unsigned value = 0;
        bool digits = false;
        while (pos < ip.size() && ip[pos] >= '0' && ip[pos] <= '9') {
            value = value * 10 + static_cast<unsigned>(ip[pos] - '0');
            if (value > 255) return false;
            digits = true;
            ++pos;
        }
        if (!digits) return false;
        octets[part] = static_cast<unsigned char>(value);
        if (part < 3) {
            if (pos >= ip.size() || ip[pos] != '.') return false;
            ++pos;
        }
    }
    return pos == ip.size();
}
/// The first `count` hex-ish groups of an IPv6 address, lowercased, so the
/// range checks below compare prefixes without full v6 parsing.
inline std::string ipv6Head(const std::string& ip, std::size_t groups) {
    std::string head;
    std::size_t pos = 0;
    for (std::size_t g = 0; g < groups && pos < ip.size(); ++g) {
        const std::size_t colon = ip.find(':', pos);
        const std::string group = ip.substr(
            pos, colon == std::string::npos ? colon : colon - pos);
        if (g != 0) head += ":";
        head += toLowerAscii(group);
        if (colon == std::string::npos) break;
        pos = colon + 1;
    }
    return head;
}
}  // namespace detail

/// 127.0.0.0/8 or ::1 (webserver' "loopback").
inline bool isLoopbackAddress(const std::string& ip) {
    unsigned char o[4];
    if (detail::parseIpv4(ip, o)) return o[0] == 127;
    const std::string head = detail::ipv6Head(ip, 8);
    return head == "::1" || head == "0:0:0:0:0:0:0:1" || ip == "::1";
}

/// 169.254.0.0/16 or fe80::/10 (webserver' "linklocal").
inline bool isLinkLocalAddress(const std::string& ip) {
    unsigned char o[4];
    if (detail::parseIpv4(ip, o)) return o[0] == 169 && o[1] == 254;
    const std::string first = detail::ipv6Head(ip, 1);
    if (first.empty()) return false;
    try {
        const unsigned value = std::stoul(first, nullptr, 16);
        return (value & 0xffc0) == 0xfe80;
    } catch (...) {
        return false;
    }
}

/// 10.0.0.0/8, 172.16.0.0/12, 192.168.0.0/16 or fc00::/7
/// (webserver' "uniquelocal").
inline bool isUniqueLocalAddress(const std::string& ip) {
    unsigned char o[4];
    if (detail::parseIpv4(ip, o)) {
        return o[0] == 10 || (o[0] == 172 && o[1] >= 16 && o[1] <= 31) ||
               (o[0] == 192 && o[1] == 168);
    }
    const std::string first = detail::ipv6Head(ip, 1);
    if (first.empty()) return false;
    try {
        const unsigned value = std::stoul(first, nullptr, 16);
        return (value & 0xfe00) == 0xfc00;
    } catch (...) {
        return false;
    }
}

namespace detail {
/// Splits a comma-separated forwarding header into trimmed entries,
/// dropping empties (a proxy chain header with no usable value is absent).
inline std::vector<std::string> splitForwarded(const std::string& header) {
    std::vector<std::string> entries;
    std::size_t pos = 0;
    while (pos <= header.size()) {
        const std::size_t comma = header.find(',', pos);
        const std::string entry = trimAscii(header.substr(
            pos, comma == std::string::npos ? comma : comma - pos));
        if (!entry.empty()) entries.push_back(entry);
        if (comma == std::string::npos) break;
        pos = comma + 1;
    }
    return entries;
}

/// Case-insensitive header lookup over a `StringMap` (the server stores
/// header names lowercased, but a request built by hand may not).
inline std::string headerValue(const network::StringMap& headers,
                               const std::string& name) {
    if (!headers) return "";  // a hand-built request may carry no header map
    const std::string wanted = toLowerAscii(name);
    for (const auto& entry : *headers->entries()) {
        if (toLowerAscii(entry.first) == wanted) return entry.second;
    }
    return "";
}
}  // namespace detail

/// The client address for one request: the socket address, unless the
/// trust config says to believe `X-Forwarded-For`. With `All` the leftmost
/// entry wins (the original client); with `Hops n` the entry `n` hops back
/// from the socket wins (the leftmost entries may be spoofed by the client
/// itself); with a named range the chain is believed only when the socket
/// peer itself is in that range.
inline std::string resolveClientIp(const std::string& remoteAddress,
                                   const std::string& forwardedFor,
                                   const TrustProxyConfig& config) {
    using Mode = TrustProxyConfig::Mode;
    if (config.mode == Mode::None) return remoteAddress;
    const std::vector<std::string> chain =
        detail::splitForwarded(forwardedFor);
    if (chain.empty()) return remoteAddress;
    switch (config.mode) {
        case Mode::All: return chain.front();
        case Mode::Hops: {
            // chain + [remote]: the n-th hop from the right is the client.
            const std::size_t total = chain.size() + 1;
            const std::size_t back =
                static_cast<std::size_t>(std::max(config.hops, 0));
            if (back >= total) return chain.front();
            if (back == 0) return remoteAddress;
            return chain[total - 1 - back];
        }
        case Mode::Loopback:
            return isLoopbackAddress(remoteAddress) ? chain.front()
                                                    : remoteAddress;
        case Mode::LinkLocal:
            return isLinkLocalAddress(remoteAddress) ? chain.front()
                                                     : remoteAddress;
        case Mode::UniqueLocal:
            return isUniqueLocalAddress(remoteAddress) ? chain.front()
                                                       : remoteAddress;
        case Mode::None: return remoteAddress;
    }
    return remoteAddress;
}

/// The request protocol: "https" only when a trusted `X-Forwarded-Proto`
/// says so (the socket itself is plain HTTP in beta one, D70); otherwise
/// the socket protocol.
inline std::string resolveProtocol(const std::string& socketProtocol,
                                   const std::string& forwardedProto,
                                   const TrustProxyConfig& config) {
    if (config.mode == TrustProxyConfig::Mode::None) return socketProtocol;
    const std::vector<std::string> chain =
        detail::splitForwarded(forwardedProto);
    if (chain.empty()) return socketProtocol;
    return detail::toLowerAscii(chain.front());
}

// =============================================================================
// Request/response plumbing (the dispatcher's side of the surface)
// =============================================================================
/// The header slot `RequestScope` uses to carry the resolved protocol to
/// `requestProtocol` without touching the shared `HttpRequest` class
/// (which this family may not edit). The name is namespaced so a user
/// header cannot collide with it in practice; it is visible in
/// `req.headers` and documented as an internal slot.
inline constexpr const char* kProtocolHeaderSlot = "x-cp3-expr-protocol";

/// Applies the "trust proxy" setting to one request: rewrites `req.ip`
/// and records the resolved protocol for `requestProtocol`. Called by
/// `RequestScope`; calling it directly is the same thing.
inline void applyTrustProxy(network::HttpRequest& req,
                            const Settings& settings) {
    const TrustProxyConfig config =
        parseTrustProxy(settings.getOr("trust proxy"));
    const std::string forwardedFor =
        detail::headerValue(req.headers, "x-forwarded-for");
    const std::string forwardedProto =
        detail::headerValue(req.headers, "x-forwarded-proto");
    req.setRemoteAddress(
        resolveClientIp(req.ip, forwardedFor, config));
    if (!req.headers) {
        req.headers = collections::makeMap<std::string, std::string>();
    }
    req.headers->set(
        kProtocolHeaderSlot,
        resolveProtocol("http", forwardedProto, config));
}

/// `req.protocol` ("http" | "https"): the slot `applyTrustProxy` filled,
/// or "http" when the request never went through it.
inline std::string requestProtocol(const network::HttpRequest& req) {
    const std::string protocol =
        detail::headerValue(req.headers, kProtocolHeaderSlot);
    return protocol.empty() ? std::string("http") : protocol;
}

// The current app for `res.render`: the dispatcher sets it per request
// through `RequestScope`. Thread-local because the scaled server (D79)
// runs one loop per thread and handlers run on the loop thread.
class webserverApp;  // fwd (defined below)
inline const webserverApp*& currentAppSlot() {
    static thread_local const webserverApp* current = nullptr;
    return current;
}
inline const webserverApp* currentApp() { return currentAppSlot(); }

/// The per-response `res.locals` bags, one entry per live response.
/// Single home for the map so `clearResLocals` and `resLocalsFor` share it.
inline std::map<const network::HttpResponse*, Locals>& resLocalsBags() {
    static thread_local std::map<const network::HttpResponse*, Locals> bags;
    return bags;
}

/// The per-response `res.locals` bag. Fresh per request: `RequestScope`
/// clears the entry when the request starts, so middleware of one request
/// never sees another's locals. No `RequestScope` means the bag persists
/// for the process lifetime (a leak across requests in a hand-rolled
/// dispatcher - the documented reason to use the scope).
inline Locals& resLocalsFor(const network::HttpResponse& res) {
    return resLocalsBags()[&res];
}

inline void clearResLocals(const network::HttpResponse& res) {
    resLocalsBags().erase(&res);
}

// (RequestScope, which needs the complete webserverApp, is defined after
// webserverApp below.)

// =============================================================================
// webserverApp - the settings-owning application value
// =============================================================================
/// The value behind `app` for every settings / view-engine member:
/// `app.enable/disable/enabled`, `app.set/get`, `app.engine`, `app.locals`,
/// `app.render`. The routing surface (verbs, `use`, `listen`, ...) is the
/// http-server family; this class owns only the settings state the routing
/// surface shares. A base webserver app class either inherits this type or
/// holds one as `webserverState` and forwards - the member spellings codegen
/// emits are exactly the member names below.
class webserverApp {
public:
    Settings settings;
    Locals locals;
    ViewRegistry engines;

    /// `app.enable(name)`: `set(name, true)`, chainable.
    webserverApp& enable(const std::string& name) {
        settings.enable(name);
        return *this;
    }

    /// `app.disable(name)`: `set(name, false)`, chainable.
    webserverApp& disable(const std::string& name) {
        settings.disable(name);
        return *this;
    }

    /// `app.enabled(name)`: the truthiness of the setting.
    [[nodiscard]] bool enabled(const std::string& name) const {
        return settings.enabled(name);
    }

    /// `app.set(name, value)`, chainable. ("views" / "view engine" /
    /// "trust proxy" take effect through the readers below / the
    /// dispatcher; any other name is stored verbatim.)
    webserverApp& setSetting(const std::string& name, SettingValue value) {
        settings.set(name, std::move(value));
        return *this;
    }

    /// `app.get(name)`: the stored value, or null when unset (webserver'
    /// `undefined`; see the header comment).
    [[nodiscard]] SettingValue getSetting(const std::string& name) const {
        return settings.getOr(name);
    }

    /// `app.engine(ext, fn)`, chainable: registers `fn` for `ext`
    /// ("html" and ".html" name the same engine).
    webserverApp& setEngine(const std::string& ext, ViewEngineFn fn) {
        engines.setEngine(ext, std::move(fn));
        return *this;
    }

    /// Resolves the template file for `view` under the "views" setting and
    /// the extension to dispatch on ("view engine" when the view name
    /// carries no extension of its own). With a "views" list, the first
    /// directory that holds the file wins; when none does, the first
    /// directory's path is returned so the lookup error names it.
    [[nodiscard]] std::string resolveViewPath(
        const std::string& view) const {
        const std::string ext = viewExtension(view);
        std::vector<std::string> dirs;
        const auto views = settings.get("views");
        if (views && views->isList() && !views->asList().empty()) {
            dirs = views->asList();
        } else {
            dirs.push_back(settings.getString("views", "./views"));
        }
        std::string first;
        for (const std::string& dir : dirs) {
            const std::string candidate = joinViewPath(dir, view, ext);
            if (first.empty()) first = candidate;
            std::error_code ec;
            if (std::filesystem::is_regular_file(candidate, ec)) {
                return candidate;
            }
        }
        return first;
    }

    [[nodiscard]] std::string viewExtension(const std::string& view) const {
        const std::string own =
            std::filesystem::path(view).extension().string();
        if (!own.empty()) return detail::normalizeExt(own);
        return detail::normalizeExt(settings.getString("view engine"));
    }

    /// `app.render(view, options?)`: renders and returns the HTML string
    /// (webserver' callback form is not offered; a failure throws `Error`).
    [[nodiscard]] std::string renderToString(
        const std::string& view, const Locals& options = Locals()) const {
        Locals merged = locals;
        merged.mergeFrom(options);
        return engines.render(resolveViewPath(view), viewExtension(view),
                              merged);
    }

    /// `res.render(view, options?)`: merges app.locals < res.locals <
    /// options, renders, and answers `text/html`. Terminal, like
    /// `res.text`: the first terminal write of the chain wins.
    void render(network::HttpResponse& res, const std::string& view,
                const Locals& options = Locals()) const {
        Locals merged = locals;
        merged.mergeFrom(resLocalsFor(res));
        merged.mergeFrom(options);
        const std::string html = engines.render(
            resolveViewPath(view), viewExtension(view), merged);
        res.header("Content-Type", "text/html; charset=utf-8");
        res.text(html);
    }

private:
    /// `dir / view`, appending `"." + ext` when the view name carries no
    /// extension of its own (an absolute view name ignores `dir`).
    [[nodiscard]] static std::string joinViewPath(const std::string& dir,
                                                  const std::string& view,
                                                  const std::string& ext) {
        const std::filesystem::path viewPath(view);
        std::filesystem::path full =
            viewPath.is_absolute()
                ? viewPath
                : std::filesystem::path(dir) / viewPath;
        if (full.extension().empty() && !ext.empty()) {
            full += "." + ext;
        }
        return full.string();
    }
};

/// Scopes one request to an app: sets the current app, applies trust
/// proxy, and resets `res.locals`. The dispatcher constructs one at the
/// top of the handler chain and lets it die with the request:
///
///   webserver::RequestScope scope(app, req, res);
///   ... run the middleware / route chain ...
///
/// (The chain helpers in the server glue call `next()` synchronously or
/// await the handler promise before this scope exits, so the scope is
/// alive for every handler of the request. An async handler that outlives
/// the chain - e.g. a detached promise - renders without a current app;
/// `renderResponse` then throws, which the error path reports.)
class RequestScope {
public:
    RequestScope(const webserverApp& app, network::HttpRequest& req,
                 network::HttpResponse& res)
        : previous_(currentAppSlot()) {
        currentAppSlot() = &app;
        clearResLocals(res);
        applyTrustProxy(req, app.settings);
    }
    ~RequestScope() { currentAppSlot() = previous_; }

    RequestScope(const RequestScope&) = delete;
    RequestScope& operator=(const RequestScope&) = delete;

private:
    const webserverApp* previous_;
};

/// `res.render(view, options?)` as a free function (the codegen target):
/// renders through the request's current app. Without a `RequestScope`
/// there is no app to render with, and this throws `Error` - the
/// dispatcher's error path answers 500 instead of silently sending an
/// empty body.
inline void renderResponse(network::HttpResponse& res,
                           const std::string& view,
                           const Locals& options = Locals()) {
    const webserverApp* app = currentApp();
    if (app == nullptr) {
        throw Error("res.render(\"" + view +
                    "\") with no webserver app in scope (the request did not "
                    "run through webserver::RequestScope)");
    }
    app->render(res, view, options);
}

}  // namespace cp3::runtime::webserver
