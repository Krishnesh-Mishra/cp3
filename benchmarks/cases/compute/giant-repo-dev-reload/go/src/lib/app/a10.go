package app

import "giant/src/lib/mid"

func A10(x int) int {
    return mid.M50(x) + mid.M127(x + 2)
}

