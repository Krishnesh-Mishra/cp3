#ifndef GIANT_APP_A35_HPP
#define GIANT_APP_A35_HPP

#include "../mid/m47.hpp"
#include "../mid/m124.hpp"

inline int a35(int x) {
    return m47(x) + m124(x + 2);
}

#endif  // GIANT_APP_A35_HPP
