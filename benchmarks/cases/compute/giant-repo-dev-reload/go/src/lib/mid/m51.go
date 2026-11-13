package mid

import "giant/src/lib/base"

func M51(x int) int {
    return base.B25(x) + base.B65(x + 1)
}

