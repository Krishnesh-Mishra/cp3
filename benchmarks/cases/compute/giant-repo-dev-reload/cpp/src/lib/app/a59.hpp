#ifndef GIANT_APP_A59_HPP
#define GIANT_APP_A59_HPP

#include "../mid/m39.hpp"
#include "../mid/m116.hpp"

inline int a59(int x) {
    return m39(x) + m116(x + 2);
}

#endif  // GIANT_APP_A59_HPP
