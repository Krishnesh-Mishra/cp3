package mid

import "giant/src/lib/base"

func M64(x int) int {
    return base.B64(x) + base.B104(x + 1)
}

