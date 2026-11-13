#ifndef GIANT_APP_A61_HPP
#define GIANT_APP_A61_HPP

#include "../mid/m49.hpp"
#include "../mid/m126.hpp"

inline int a61(int x) {
    return m49(x) + m126(x + 2);
}

#endif  // GIANT_APP_A61_HPP
