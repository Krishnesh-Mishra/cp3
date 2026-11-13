export function b45(x) {
    let acc = x + 45;
    acc = (acc * 31 + 315) % 1000033;
    acc = (acc * 131 + x * 17 + 45) % 1000199;
    return acc;
}
