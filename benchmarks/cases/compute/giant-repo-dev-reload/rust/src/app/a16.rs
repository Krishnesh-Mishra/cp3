use crate::mid::m80::m80;
use crate::mid::m29::m29;

pub fn a16(x: i64) -> i64 {
    m80(x) + m29(x + 2)
}

