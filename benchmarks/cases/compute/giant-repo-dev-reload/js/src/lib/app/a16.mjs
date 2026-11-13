import { m80 } from "../mid/m80.mjs";
import { m29 } from "../mid/m29.mjs";
export function a16(x) {
    return m80(x) + m29(x + 2);
}
