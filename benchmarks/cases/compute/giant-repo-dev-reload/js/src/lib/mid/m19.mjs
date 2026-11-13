import { b57 } from "../base/b57.mjs";
import { b97 } from "../base/b97.mjs";
export function m19(x) {
    return b57(x) + b97(x + 1);
}
