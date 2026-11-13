import { b46 } from "../base/b46.mjs";
import { b86 } from "../base/b86.mjs";
export function m58(x) {
    return b46(x) + b86(x + 1);
}
