import { b44 } from "../base/b44.mjs";
import { b84 } from "../base/b84.mjs";
export function m100(x) {
    return b44(x) + b84(x + 1);
}
