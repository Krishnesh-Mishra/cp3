#ifndef GIANT_MID_M107_HPP
#define GIANT_MID_M107_HPP

#include "../base/b65.hpp"
#include "../base/b105.hpp"

inline int m107(int x) {
    return b65(x) + b105(x + 1);
}

#endif  // GIANT_MID_M107_HPP
