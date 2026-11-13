use crate::mid::m4::m4;
use crate::mid::m81::m81;

pub fn a52(x: i64) -> i64 {
    m4(x) + m81(x + 2)
}

