import { b59 } from "../base/b59.mjs";
import { b99 } from "../base/b99.mjs";
export function m105(x) {
    return b59(x) + b99(x + 1);
}
