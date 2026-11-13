import { b76 } from "../base/b76.mjs";
import { b116 } from "../base/b116.mjs";
export function m68(x) {
    return b76(x) + b116(x + 1);
}
