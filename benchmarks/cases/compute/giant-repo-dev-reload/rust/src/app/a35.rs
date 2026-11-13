use crate::mid::m47::m47;
use crate::mid::m124::m124;

pub fn a35(x: i64) -> i64 {
    m47(x) + m124(x + 2)
}

