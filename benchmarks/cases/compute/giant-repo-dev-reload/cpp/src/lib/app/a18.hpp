#ifndef GIANT_APP_A18_HPP
#define GIANT_APP_A18_HPP

#include "../mid/m90.hpp"
#include "../mid/m39.hpp"

inline int a18(int x) {
    return m90(x) + m39(x + 2);
}

#endif  // GIANT_APP_A18_HPP
