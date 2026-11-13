use crate::mid::m67::m67;
use crate::mid::m16::m16;

pub fn a39(x: i64) -> i64 {
    m67(x) + m16(x + 2)
}

