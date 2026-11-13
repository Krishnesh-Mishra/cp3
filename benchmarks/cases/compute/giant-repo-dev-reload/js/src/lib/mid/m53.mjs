import { b31 } from "../base/b31.mjs";
import { b71 } from "../base/b71.mjs";
export function m53(x) {
    return b31(x) + b71(x + 1);
}
