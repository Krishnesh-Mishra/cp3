package mid

import "giant/src/lib/base"

func M85(x int) int {
    return base.B127(x) + base.B39(x + 1)
}

