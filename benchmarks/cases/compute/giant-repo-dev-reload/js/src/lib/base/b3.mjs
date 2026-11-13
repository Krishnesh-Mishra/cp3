export function b3(x) {
    let acc = x + 3;
    acc = (acc * 31 + 21) % 1000033;
    acc = (acc * 131 + x * 17 + 3) % 1000199;
    return acc;
}
