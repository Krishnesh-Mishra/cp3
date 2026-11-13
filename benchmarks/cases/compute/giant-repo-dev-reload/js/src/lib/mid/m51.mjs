import { b25 } from "../base/b25.mjs";
import { b65 } from "../base/b65.mjs";
export function m51(x) {
    return b25(x) + b65(x + 1);
}
