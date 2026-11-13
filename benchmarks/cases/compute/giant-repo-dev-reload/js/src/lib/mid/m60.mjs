import { b52 } from "../base/b52.mjs";
import { b92 } from "../base/b92.mjs";
export function m60(x) {
    return b52(x) + b92(x + 1);
}
