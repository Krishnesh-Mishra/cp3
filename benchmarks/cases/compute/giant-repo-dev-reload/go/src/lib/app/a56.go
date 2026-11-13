package app

import "giant/src/lib/mid"

func A56(x int) int {
    return mid.M24(x) + mid.M101(x + 2)
}

