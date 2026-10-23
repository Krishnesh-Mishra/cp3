// =============================================================================
// src/codegen/src/emit/spread_call.cpp - the spread-call lowering
// (STYLE.md section 8).
//
// `f(...xs)` splices the elements of an array into the argument list. The
// contract is in detail/spread_call.h; this unit spells the two shapes:
//
//   * a callee with fixed parameters -> a switch over the spread's length.
//     The elements fill the parameters the spread covers, the parameters it
//     does not reach take their default (a required parameter without one
//     is a runtime error, the honest analogue of cp3's compile-time arity
//     check), and the elements beyond the last parameter are dropped, which
//     is what JS does with the extra arguments;
//   * a callee whose last parameter is a rest -> the elements join the
//     packed rest array, together with the fixed arguments around them.
//
// The spread source is evaluated once (into `cp3_sp`), like every other
// once-only source in the emitter.
// =============================================================================
#include "detail/expr/spread_call.h"

#include <cstddef>
#include <string>
#include <vector>

namespace cp3 {
namespace detail {

namespace {

// The text of a parameter the spread did not supply: its default, or a
// runtime error (a required parameter without a default).
std::string missingArgText(const SpreadParam& param) {
    if (!param.fallback.empty()) return param.fallback;
    return "[&]{ ::cp3::runtime::fatalError("
           "\"not enough arguments in a spread call\"); return " +
           param.type + "{}; }()";
}

// The arguments of the call for a spread source of `n` elements: the fixed
// arguments before the spread, then the elements (or the parameter's
// fallback), then the fixed arguments after the spread.
std::vector<std::string> argsFor(const SpreadCall& call, std::size_t n) {
    const std::size_t filled = call.params.size() - call.before.size() -
                               call.after.size();
    std::vector<std::string> args = call.before;
    args.reserve(call.params.size());
    for (std::size_t i = 0; i < filled; ++i) {
        if (i < n) {
            args.push_back("(*cp3_sp)[" + std::to_string(i) + "]");
            continue;
        }
        args.push_back(missingArgText(call.params[call.before.size() + i]));
    }
    for (const std::string& arg : call.after) args.push_back(arg);
    return args;
}

// The joined argument list of a call.
std::string joinArgs(const std::vector<std::string>& args) {
    std::string out;
    for (std::size_t i = 0; i < args.size(); ++i) {
        if (i != 0) out += ", ";
        out += args[i];
    }
    return out;
}

// Does the spread's element type fit the parameter it fills? The element
// text is passed straight into the call, so the two spellings must agree
// (an optional parameter also takes the bare value).
bool elementFits(const SpreadCall& call, std::size_t index) {
    const std::string& param = call.params[index].type;
    if (param == call.elementType) return true;
    const std::string prefix = "std::optional<";
    if (param.rfind(prefix, 0) == 0 && param.back() == '>') {
        return param.substr(prefix.size(), param.size() - prefix.size() - 1) ==
               call.elementType;
    }
    return false;
}

}  // namespace

// A spread into a callee with fixed parameters.
std::string spreadCallText(const SpreadCall& call) {
    const std::size_t pcount = call.params.size();
    const std::size_t before = call.before.size();
    const std::size_t after = call.after.size();
    if (before + after > pcount) return std::string();
    for (std::size_t i = before; i < pcount - after; ++i) {
        if (!elementFits(call, i)) return std::string();
    }
    const std::size_t filled = pcount - before - after;
    const std::string call_ = call.callee;
    if (after != 0) {
        // A fixed argument after the spread pins the spread's length: the
        // arguments after the spread would move with it.
        std::string out = "([&]{ const auto& cp3_sp = " + call.arrayText + ";";
        out += " if (cp3_sp->size() != " + std::to_string(filled) + ") {";
        out += " ::cp3::runtime::fatalError("
               "\"wrong number of arguments in a spread call\"); }";
        out += " return " + call_ + "(" + joinArgs(argsFor(call, filled)) +
               "); }())";
        return out;
    }
    // The switch over the spread's length: every length the callee accepts
    // has its own call, and the elements beyond the last parameter are
    // dropped.
    std::string out = "([&]{ const auto& cp3_sp = " + call.arrayText + ";";
    out += " switch (cp3_sp->size()) {";
    for (std::size_t n = 0; n <= filled; ++n) {
        out += " case " + std::to_string(n) + ": return " + call_ + "(" +
               joinArgs(argsFor(call, n)) + ");";
    }
    out += " default: return " + call_ + "(" + joinArgs(argsFor(call, filled)) +
           "); } }())";
    return out;
}

// A spread into a callee whose last parameter is a rest.
std::string spreadRestCallText(const SpreadCall& call) {
    if (call.restType != call.elementType) return std::string();
    const std::string elem = call.elementType;
    std::string out = "::cp3::runtime::makeArray<" + elem + ">([&]{ ";
    out += "std::vector<" + elem + "> cp3_v;";
    for (const std::string& arg : call.before) {
        out += " cp3_v.push_back(" + arg + ");";
    }
    out += " const auto& cp3_s = " + call.arrayText + ";";
    out += " cp3_v.insert(cp3_v.end(), cp3_s->begin(), cp3_s->end());";
    for (const std::string& arg : call.after) {
        out += " cp3_v.push_back(" + arg + ");";
    }
    out += " return cp3_v; }())";
    return out;
}

}  // namespace detail
}  // namespace cp3
