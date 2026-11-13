package app

import "giant/src/lib/mid"

func A24(x int) int {
    return mid.M120(x) + mid.M69(x + 2)
}

