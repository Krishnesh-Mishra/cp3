use crate::mid::m24::m24;
use crate::mid::m101::m101;

pub fn a56(x: i64) -> i64 {
    m24(x) + m101(x + 2)
}

