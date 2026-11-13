#ifndef GIANT_MID_M63_HPP
#define GIANT_MID_M63_HPP

#include "../base/b61.hpp"
#include "../base/b101.hpp"

inline int m63(int x) {
    return b61(x) + b101(x + 1);
}

#endif  // GIANT_MID_M63_HPP
