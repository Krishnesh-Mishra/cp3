use crate::mid::m25::m25;
use crate::mid::m102::m102;

pub fn a5(x: i64) -> i64 {
    m25(x) + m102(x + 2)
}

