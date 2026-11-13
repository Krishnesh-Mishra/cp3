package base

func B124(x int) int {
    acc := x + 124
    acc = (acc * 31 + 868) % 1000033
    acc = (acc * 131 + x * 17 + 124) % 1000199
    return acc
}

