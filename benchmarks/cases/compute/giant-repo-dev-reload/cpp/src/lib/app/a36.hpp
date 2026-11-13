#ifndef GIANT_APP_A36_HPP
#define GIANT_APP_A36_HPP

#include "../mid/m52.hpp"
#include "../mid/m1.hpp"

inline int a36(int x) {
    return m52(x) + m1(x + 2);
}

#endif  // GIANT_APP_A36_HPP
