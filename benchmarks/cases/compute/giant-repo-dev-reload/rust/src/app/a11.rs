use crate::mid::m55::m55;
use crate::mid::m4::m4;

pub fn a11(x: i64) -> i64 {
    m55(x) + m4(x + 2)
}

