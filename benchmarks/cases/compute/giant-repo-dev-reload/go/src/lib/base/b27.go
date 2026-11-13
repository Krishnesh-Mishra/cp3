package base

func B27(x int) int {
    acc := x + 27
    acc = (acc * 31 + 189) % 1000033
    acc = (acc * 131 + x * 17 + 27) % 1000199
    return acc
}

