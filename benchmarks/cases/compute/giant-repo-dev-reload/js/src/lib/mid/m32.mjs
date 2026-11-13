import { b96 } from "../base/b96.mjs";
import { b8 } from "../base/b8.mjs";
export function m32(x) {
    return b96(x) + b8(x + 1);
}
