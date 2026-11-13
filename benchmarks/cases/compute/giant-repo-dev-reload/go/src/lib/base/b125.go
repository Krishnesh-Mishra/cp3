package base

func B125(x int) int {
    acc := x + 125
    acc = (acc * 31 + 875) % 1000033
    acc = (acc * 131 + x * 17 + 125) % 1000199
    return acc
}

