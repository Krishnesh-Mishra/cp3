use crate::mid::m75::m75;
use crate::mid::m24::m24;

pub fn a15(x: i64) -> i64 {
    m75(x) + m24(x + 2)
}

