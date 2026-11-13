#ifndef GIANT_APP_A53_HPP
#define GIANT_APP_A53_HPP

#include "../mid/m9.hpp"
#include "../mid/m86.hpp"

inline int a53(int x) {
    return m9(x) + m86(x + 2);
}

#endif  // GIANT_APP_A53_HPP
