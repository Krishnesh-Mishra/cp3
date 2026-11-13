package app

import "giant/src/lib/mid"

func A19(x int) int {
    return mid.M95(x) + mid.M44(x + 2)
}

