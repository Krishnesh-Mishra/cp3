#ifndef GIANT_APP_A7_HPP
#define GIANT_APP_A7_HPP

#include "../mid/m35.hpp"
#include "../mid/m112.hpp"

inline int a7(int x) {
    return m35(x) + m112(x + 2);
}

#endif  // GIANT_APP_A7_HPP
