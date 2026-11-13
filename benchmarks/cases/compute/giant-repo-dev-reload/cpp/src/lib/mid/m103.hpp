#ifndef GIANT_MID_M103_HPP
#define GIANT_MID_M103_HPP

#include "../base/b53.hpp"
#include "../base/b93.hpp"

inline int m103(int x) {
    return b53(x) + b93(x + 1);
}

#endif  // GIANT_MID_M103_HPP
