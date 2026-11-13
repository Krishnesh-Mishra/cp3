package mid

import "giant/src/lib/base"

func M92(x int) int {
    return base.B20(x) + base.B60(x + 1)
}

