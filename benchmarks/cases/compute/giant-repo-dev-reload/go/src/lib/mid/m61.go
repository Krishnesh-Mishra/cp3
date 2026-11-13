package mid

import "giant/src/lib/base"

func M61(x int) int {
    return base.B55(x) + base.B95(x + 1)
}

