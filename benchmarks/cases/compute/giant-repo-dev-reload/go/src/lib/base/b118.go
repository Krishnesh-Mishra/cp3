package base

func B118(x int) int {
    acc := x + 118
    acc = (acc * 31 + 826) % 1000033
    acc = (acc * 131 + x * 17 + 118) % 1000199
    return acc
}

