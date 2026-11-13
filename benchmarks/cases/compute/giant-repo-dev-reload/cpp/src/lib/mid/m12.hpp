#ifndef GIANT_MID_M12_HPP
#define GIANT_MID_M12_HPP

#include "../base/b36.hpp"
#include "../base/b76.hpp"

inline int m12(int x) {
    return b36(x) + b76(x + 1);
}

#endif  // GIANT_MID_M12_HPP
