use crate::mid::m52::m52;
use crate::mid::m1::m1;

pub fn a36(x: i64) -> i64 {
    m52(x) + m1(x + 2)
}

