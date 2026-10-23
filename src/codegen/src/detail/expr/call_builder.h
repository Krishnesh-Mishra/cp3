// =============================================================================
// src/codegen/src/detail/call_builder.h - PRIVATE argument lowering helper.
//
// The argument lowering of a call lives on a friend helper
// (`detail::CallBuilder`) so the Generator declaration stays within budget
// without introducing a second copy of the emitter state. The dispatch (the
// callee forms) is in emit/call_args.cpp; the spread lowering (the trailing
// spread into a fixed-argument callee) is in emit/call_spread.cpp.
// =============================================================================
#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "cp3/core/ast.hpp"
#include "cp3/core/types.hpp"

namespace cp3 {

class Generator;

namespace detail {

struct CallBuilder {
    explicit CallBuilder(Generator& generator) : g(generator) {}

    Generator& g;

    // One lowered argument text plus the explicit `undefined`s that were
    // dropped (stored back as the parameter's default or `std::nullopt`).
    struct ArgTexts {
        std::vector<std::string> value;
        std::vector<bool> drop;
        bool spreadRest = false;  // a lone `f(...xs)` passes the array itself
        // A trailing spread into a fixed-argument callee: the prologue binds
        // the spread source once, and the argument texts of the parameters the
        // spread fills follow the fixed arguments.
        bool hasSpread = false;
        std::string prologue;
        std::vector<std::string> spreadValues;
        std::size_t fixedCount = 0;
        std::size_t spreadIndex = 0;  // the spread argument's position
    };

    // The lowered argument list of a call: the argument texts plus the
    // prologue that binds a spread source ("" when the call has no spread).
    struct CallArgs {
        std::vector<std::string> values;
        std::string prologue;
    };

    // The spread source of a call: the element type it carries, the local its
    // value is bound to, and the first parameter it fills.
    struct SpreadSource {
        std::size_t first = 0;
        Type elem;
        std::string name;
    };

    ArgTexts callArgTexts(const CallExpr& c, const ParamList* params,
                          const std::vector<std::optional<Type>>& ptypes,
                          bool applyDefaults, bool reifyObjectArgs);
    bool findRestParam(const ParamList* params, std::size_t& restAt) const;
    std::optional<Type> restElemType(
        const std::vector<std::optional<Type>>& ptypes, std::size_t restAt) const;
    std::vector<std::string> packRestArgs(const CallExpr& c, const ArgTexts& texts,
                                          const std::optional<Type>& elem,
                                          std::size_t restAt);
    // The element type of the rest parameter AT THIS CALL SITE: the
    // substituted type argument, or - for an inferred call - the unified type
    // of the arguments the rest parameter absorbs, which is what C++ deduces
    // the callee's type parameter from. The declared type is the fallback.
    std::optional<Type> restElemTypeAt(
        const CallExpr& c, const std::vector<std::optional<Type>>& ptypes,
        std::size_t restAt) const;
    std::vector<std::string> plainArgList(const ArgTexts& texts,
                                          const ParamList* params,
                                          bool applyDefaults);
    CallArgs callArgs(const CallExpr& c, const ParamList* params,
                      const std::vector<std::optional<Type>>& ptypes,
                      bool applyDefaults, bool reifyObjectArgs = false);
    // The call text: `callee(args)`, or a lambda that binds the spread source
    // once and calls the callee with it.
    std::string callText(const std::string& callee, const CallArgs& args);
    // The argument text of a spread argument: a rest-only callee takes the
    // array itself, a fixed-argument callee takes the spread's elements.
    bool spreadArg(const CallExpr& c, const ParamList* params,
                   const std::vector<std::optional<Type>>& ptypes,
                   ArgTexts& texts);
    // A trailing spread into a fixed-argument callee: the spread's elements
    // fill the parameters the spread reaches. False when the call cannot be
    // lowered that way (the caller keeps its own path).
    bool spreadArgs(const CallExpr& c, const ParamList* params,
                    const std::vector<std::optional<Type>>& ptypes,
                    ArgTexts& texts);
    // The argument texts of the parameters a spread fills.
    std::vector<std::string> spreadValues(
        const ParamList& params,
        const std::vector<std::optional<Type>>& ptypes, const SpreadSource& src);
    // The argument text of a parameter the call may leave out: the spread's
    // element, or the parameter's default when the spread carries none.
    std::string spreadOmittableArg(const Parameter* p, const Type& pt,
                                    const std::string& element,
                                    const std::string& present);
    std::string emitFunctionCall(const CallExpr& c, const std::string& text,
                                 const Type& fnType, const ParamList* params);
    std::string emitIdentifierCall(const CallExpr& c);
    // The member-call hooks for the receiver shapes that own a call outright:
    // a native binding's own function and the parallel family. "" when
    // neither owns the call.
    std::string emitOwnedCall(const CallExpr& c, const MemberExpr& m);
    std::string emitMemberCall(const CallExpr& c, const MemberExpr& m);
};

}  // namespace detail
}  // namespace cp3
