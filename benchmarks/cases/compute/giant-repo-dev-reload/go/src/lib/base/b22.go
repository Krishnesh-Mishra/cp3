package base

func B22(x int) int {
    acc := x + 22
    acc = (acc * 31 + 154) % 1000033
    acc = (acc * 131 + x * 17 + 22) % 1000199
    return acc
}

