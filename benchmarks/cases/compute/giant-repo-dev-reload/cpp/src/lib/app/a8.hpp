#ifndef GIANT_APP_A8_HPP
#define GIANT_APP_A8_HPP

#include "../mid/m40.hpp"
#include "../mid/m117.hpp"

inline int a8(int x) {
    return m40(x) + m117(x + 2);
}

#endif  // GIANT_APP_A8_HPP
