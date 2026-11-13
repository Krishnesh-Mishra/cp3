#ifndef GIANT_APP_A28_HPP
#define GIANT_APP_A28_HPP

#include "../mid/m12.hpp"
#include "../mid/m89.hpp"

inline int a28(int x) {
    return m12(x) + m89(x + 2);
}

#endif  // GIANT_APP_A28_HPP
