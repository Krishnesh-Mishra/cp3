#ifndef GIANT_APP_A14_HPP
#define GIANT_APP_A14_HPP

#include "../mid/m70.hpp"
#include "../mid/m19.hpp"

inline int a14(int x) {
    return m70(x) + m19(x + 2);
}

#endif  // GIANT_APP_A14_HPP
