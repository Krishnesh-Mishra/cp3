#ifndef GIANT_APP_A12_HPP
#define GIANT_APP_A12_HPP

#include "../mid/m60.hpp"
#include "../mid/m9.hpp"

inline int a12(int x) {
    return m60(x) + m9(x + 2);
}

#endif  // GIANT_APP_A12_HPP
