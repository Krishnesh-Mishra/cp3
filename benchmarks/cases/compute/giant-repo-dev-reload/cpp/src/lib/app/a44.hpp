#ifndef GIANT_APP_A44_HPP
#define GIANT_APP_A44_HPP

#include "../mid/m92.hpp"
#include "../mid/m41.hpp"

inline int a44(int x) {
    return m92(x) + m41(x + 2);
}

#endif  // GIANT_APP_A44_HPP
