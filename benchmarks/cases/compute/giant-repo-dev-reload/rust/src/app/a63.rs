use crate::mid::m59::m59;
use crate::mid::m8::m8;

pub fn a63(x: i64) -> i64 {
    m59(x) + m8(x + 2)
}

