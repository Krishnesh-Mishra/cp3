package mid

import "giant/src/lib/base"

func M90(x int) int {
    return base.B14(x) + base.B54(x + 1)
}

