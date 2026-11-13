#ifndef GIANT_APP_A32_HPP
#define GIANT_APP_A32_HPP

#include "../mid/m32.hpp"
#include "../mid/m109.hpp"

inline int a32(int x) {
    return m32(x) + m109(x + 2);
}

#endif  // GIANT_APP_A32_HPP
