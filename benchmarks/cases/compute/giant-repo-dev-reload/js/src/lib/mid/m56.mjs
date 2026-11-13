import { b40 } from "../base/b40.mjs";
import { b80 } from "../base/b80.mjs";
export function m56(x) {
    return b40(x) + b80(x + 1);
}
