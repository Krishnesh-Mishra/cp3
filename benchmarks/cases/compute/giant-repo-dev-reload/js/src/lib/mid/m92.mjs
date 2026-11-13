import { b20 } from "../base/b20.mjs";
import { b60 } from "../base/b60.mjs";
export function m92(x) {
    return b20(x) + b60(x + 1);
}
