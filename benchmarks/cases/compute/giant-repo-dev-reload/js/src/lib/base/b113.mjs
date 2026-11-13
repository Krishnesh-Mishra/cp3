export function b113(x) {
    let acc = x + 113;
    acc = (acc * 31 + 791) % 1000033;
    acc = (acc * 131 + x * 17 + 113) % 1000199;
    return acc;
}
