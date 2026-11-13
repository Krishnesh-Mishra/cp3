export function b79(x) {
    let acc = x + 79;
    acc = (acc * 31 + 553) % 1000033;
    acc = (acc * 131 + x * 17 + 79) % 1000199;
    return acc;
}
