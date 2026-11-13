use crate::mid::m92::m92;
use crate::mid::m41::m41;

pub fn a44(x: i64) -> i64 {
    m92(x) + m41(x + 2)
}

