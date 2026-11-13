import { b120 } from "../base/b120.mjs";
import { b32 } from "../base/b32.mjs";
export function m40(x) {
    return b120(x) + b32(x + 1);
}
