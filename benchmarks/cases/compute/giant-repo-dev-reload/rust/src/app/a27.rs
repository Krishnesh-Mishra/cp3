use crate::mid::m7::m7;
use crate::mid::m84::m84;

pub fn a27(x: i64) -> i64 {
    m7(x) + m84(x + 2)
}

