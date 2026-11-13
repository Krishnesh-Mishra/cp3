export function b31(x) {
    let acc = x + 31;
    acc = (acc * 31 + 217) % 1000033;
    acc = (acc * 131 + x * 17 + 31) % 1000199;
    return acc;
}
