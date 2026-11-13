package app

import "giant/src/lib/mid"

func A32(x int) int {
    return mid.M32(x) + mid.M109(x + 2)
}

