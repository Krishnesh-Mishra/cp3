package mid

import "giant/src/lib/base"

func M35(x int) int {
    return base.B105(x) + base.B17(x + 1)
}

