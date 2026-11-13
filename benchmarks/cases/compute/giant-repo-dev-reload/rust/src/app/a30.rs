use crate::mid::m22::m22;
use crate::mid::m99::m99;

pub fn a30(x: i64) -> i64 {
    m22(x) + m99(x + 2)
}

