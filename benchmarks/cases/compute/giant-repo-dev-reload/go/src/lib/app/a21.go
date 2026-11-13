package app

import "giant/src/lib/mid"

func A21(x int) int {
    return mid.M105(x) + mid.M54(x + 2)
}

