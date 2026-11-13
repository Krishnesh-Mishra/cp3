import { b124 } from "../base/b124.mjs";
import { b36 } from "../base/b36.mjs";
export function m84(x) {
    return b124(x) + b36(x + 1);
}
