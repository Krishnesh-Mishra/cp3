package base

func B67(x int) int {
    acc := x + 67
    acc = (acc * 31 + 469) % 1000033
    acc = (acc * 131 + x * 17 + 67) % 1000199
    return acc
}

