package base

func B96(x int) int {
    acc := x + 96
    acc = (acc * 31 + 672) % 1000033
    acc = (acc * 131 + x * 17 + 96) % 1000199
    return acc
}

