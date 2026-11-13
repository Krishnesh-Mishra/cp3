package mid

import "giant/src/lib/base"

func M27(x int) int {
    return base.B81(x) + base.B121(x + 1)
}

