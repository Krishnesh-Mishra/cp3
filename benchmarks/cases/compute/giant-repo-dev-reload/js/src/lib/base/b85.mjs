export function b85(x) {
    let acc = x + 85;
    acc = (acc * 31 + 595) % 1000033;
    acc = (acc * 131 + x * 17 + 85) % 1000199;
    return acc;
}
