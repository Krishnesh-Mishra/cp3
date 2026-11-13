use crate::mid::m60::m60;
use crate::mid::m9::m9;

pub fn a12(x: i64) -> i64 {
    m60(x) + m9(x + 2)
}

