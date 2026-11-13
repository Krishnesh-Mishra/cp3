use crate::mid::m32::m32;
use crate::mid::m109::m109;

pub fn a32(x: i64) -> i64 {
    m32(x) + m109(x + 2)
}

