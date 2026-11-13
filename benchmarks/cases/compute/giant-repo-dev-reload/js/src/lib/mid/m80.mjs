import { b112 } from "../base/b112.mjs";
import { b24 } from "../base/b24.mjs";
export function m80(x) {
    return b112(x) + b24(x + 1);
}
