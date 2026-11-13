export function b100(x) {
    let acc = x + 100;
    acc = (acc * 31 + 700) % 1000033;
    acc = (acc * 131 + x * 17 + 100) % 1000199;
    return acc;
}
