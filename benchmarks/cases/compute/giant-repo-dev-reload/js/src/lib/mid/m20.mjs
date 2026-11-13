import { b60 } from "../base/b60.mjs";
import { b100 } from "../base/b100.mjs";
export function m20(x) {
    return b60(x) + b100(x + 1);
}
