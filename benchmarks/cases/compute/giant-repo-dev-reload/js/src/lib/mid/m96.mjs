import { b32 } from "../base/b32.mjs";
import { b72 } from "../base/b72.mjs";
export function m96(x) {
    return b32(x) + b72(x + 1);
}
