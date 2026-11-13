import { m32 } from "../mid/m32.mjs";
import { m109 } from "../mid/m109.mjs";
export function a32(x) {
    return m32(x) + m109(x + 2);
}
