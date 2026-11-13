#ifndef GIANT_APP_A54_HPP
#define GIANT_APP_A54_HPP

#include "../mid/m14.hpp"
#include "../mid/m91.hpp"

inline int a54(int x) {
    return m14(x) + m91(x + 2);
}

#endif  // GIANT_APP_A54_HPP
