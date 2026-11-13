#ifndef GIANT_APP_A31_HPP
#define GIANT_APP_A31_HPP

#include "../mid/m27.hpp"
#include "../mid/m104.hpp"

inline int a31(int x) {
    return m27(x) + m104(x + 2);
}

#endif  // GIANT_APP_A31_HPP
