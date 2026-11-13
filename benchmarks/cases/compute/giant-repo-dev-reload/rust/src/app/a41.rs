use crate::mid::m77::m77;
use crate::mid::m26::m26;

pub fn a41(x: i64) -> i64 {
    m77(x) + m26(x + 2)
}

