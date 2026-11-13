use crate::mid::m45::m45;
use crate::mid::m122::m122;

pub fn a9(x: i64) -> i64 {
    m45(x) + m122(x + 2)
}

