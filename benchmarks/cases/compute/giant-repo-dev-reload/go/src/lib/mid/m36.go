package mid

import "giant/src/lib/base"

func M36(x int) int {
    return base.B108(x) + base.B20(x + 1)
}

