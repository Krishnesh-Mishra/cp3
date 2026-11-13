#ifndef GIANT_APP_A23_HPP
#define GIANT_APP_A23_HPP

#include "../mid/m115.hpp"
#include "../mid/m64.hpp"

inline int a23(int x) {
    return m115(x) + m64(x + 2);
}

#endif  // GIANT_APP_A23_HPP
