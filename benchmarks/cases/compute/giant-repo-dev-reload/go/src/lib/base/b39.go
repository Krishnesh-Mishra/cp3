package base

func B39(x int) int {
    acc := x + 39
    acc = (acc * 31 + 273) % 1000033
    acc = (acc * 131 + x * 17 + 39) % 1000199
    return acc
}

