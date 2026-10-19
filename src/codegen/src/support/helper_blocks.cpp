// =============================================================================
// src/codegen/src/support/helper_blocks.cpp - PRIVATE generated-code helper
// blocks (see detail/helper_blocks.h).
//
// Two families whose generated-code helpers would otherwise sit unused in
// every unit's .cpp: the primitive union formatter/truthiness pair (nameable
// only with cp3/core/union.hpp) and the object value shape dispatchers (SFINAE
// over a generated struct's own members). Each is emitted only when
// Generator::run saw the family in the unit's text.
// =============================================================================
#include "detail/helper_blocks.h"

namespace cp3 {
namespace detail {

const char* unionHelperBlock() {
    return R"CP3HELPERS(namespace {

// ----- primitive unions (cp3/core/union.hpp) ---------------------------------
template <class... Ts>
inline std::string cp3_ts(const std::variant<Ts...>& v) {
    return std::visit(
        [](const auto& held) { return ::cp3::runtime::toString(held); }, v);
}
template <class... Ts>
inline bool cp3_truthy(const std::variant<Ts...>& v) {
    return ::cp3::runtime::unions::truthy(v);
}

}  // namespace

)CP3HELPERS";
}

const char* objectHelperBlock() {
    return R"CP3HELPERS(namespace {

// ----- object value shapes ---------------------------------------------------
template <class T>
inline auto cp3_ts(const T& v) -> decltype(v.cp3ToText()) {
    return v.cp3ToText();
}
template <class T>
inline auto cp3_truthy(const T& v) -> decltype(v.cp3Truthy()) {
    return v.cp3Truthy();
}

}  // namespace

)CP3HELPERS";
}

}  // namespace detail
}  // namespace cp3
