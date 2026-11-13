package base

func B70(x int) int {
    acc := x + 70
    acc = (acc * 31 + 490) % 1000033
    acc = (acc * 131 + x * 17 + 70) % 1000199
    return acc
}

