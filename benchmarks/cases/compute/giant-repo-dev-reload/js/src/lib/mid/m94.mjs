import { b26 } from "../base/b26.mjs";
import { b66 } from "../base/b66.mjs";
export function m94(x) {
    return b26(x) + b66(x + 1);
}
