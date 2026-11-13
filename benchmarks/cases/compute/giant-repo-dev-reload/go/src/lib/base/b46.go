package base

func B46(x int) int {
    acc := x + 46
    acc = (acc * 31 + 322) % 1000033
    acc = (acc * 131 + x * 17 + 46) % 1000199
    return acc
}

