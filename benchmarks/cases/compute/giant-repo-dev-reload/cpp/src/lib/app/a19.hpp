#ifndef GIANT_APP_A19_HPP
#define GIANT_APP_A19_HPP

#include "../mid/m95.hpp"
#include "../mid/m44.hpp"

inline int a19(int x) {
    return m95(x) + m44(x + 2);
}

#endif  // GIANT_APP_A19_HPP
