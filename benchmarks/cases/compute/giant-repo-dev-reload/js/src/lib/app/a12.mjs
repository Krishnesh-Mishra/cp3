import { m60 } from "../mid/m60.mjs";
import { m9 } from "../mid/m9.mjs";
export function a12(x) {
    return m60(x) + m9(x + 2);
}
