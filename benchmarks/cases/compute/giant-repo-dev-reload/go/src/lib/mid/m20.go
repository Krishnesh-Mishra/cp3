package mid

import "giant/src/lib/base"

func M20(x int) int {
    return base.B60(x) + base.B100(x + 1)
}

