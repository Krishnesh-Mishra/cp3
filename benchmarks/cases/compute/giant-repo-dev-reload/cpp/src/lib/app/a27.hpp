#ifndef GIANT_APP_A27_HPP
#define GIANT_APP_A27_HPP

#include "../mid/m7.hpp"
#include "../mid/m84.hpp"

inline int a27(int x) {
    return m7(x) + m84(x + 2);
}

#endif  // GIANT_APP_A27_HPP
