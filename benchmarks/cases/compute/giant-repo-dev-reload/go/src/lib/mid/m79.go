package mid

import "giant/src/lib/base"

func M79(x int) int {
    return base.B109(x) + base.B21(x + 1)
}

