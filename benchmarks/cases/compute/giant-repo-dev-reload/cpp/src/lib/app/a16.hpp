#ifndef GIANT_APP_A16_HPP
#define GIANT_APP_A16_HPP

#include "../mid/m80.hpp"
#include "../mid/m29.hpp"

inline int a16(int x) {
    return m80(x) + m29(x + 2);
}

#endif  // GIANT_APP_A16_HPP
