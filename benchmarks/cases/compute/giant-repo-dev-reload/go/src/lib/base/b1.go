package base

func B1(x int) int {
    acc := x + 1
    acc = (acc * 31 + 7) % 1000033
    acc = (acc * 131 + x * 17 + 1) % 1000199
    return acc
}

