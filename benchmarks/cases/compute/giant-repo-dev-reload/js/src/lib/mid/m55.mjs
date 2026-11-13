import { b37 } from "../base/b37.mjs";
import { b77 } from "../base/b77.mjs";
export function m55(x) {
    return b37(x) + b77(x + 1);
}
