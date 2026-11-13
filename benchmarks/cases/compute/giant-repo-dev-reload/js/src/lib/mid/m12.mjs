import { b36 } from "../base/b36.mjs";
import { b76 } from "../base/b76.mjs";
export function m12(x) {
    return b36(x) + b76(x + 1);
}
