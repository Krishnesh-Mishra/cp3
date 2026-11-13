use crate::mid::m5::m5;
use crate::mid::m82::m82;

pub fn a1(x: i64) -> i64 {
    m5(x) + m82(x + 2)
}

