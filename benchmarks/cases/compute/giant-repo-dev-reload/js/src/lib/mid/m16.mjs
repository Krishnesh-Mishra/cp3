import { b48 } from "../base/b48.mjs";
import { b88 } from "../base/b88.mjs";
export function m16(x) {
    return b48(x) + b88(x + 1);
}
