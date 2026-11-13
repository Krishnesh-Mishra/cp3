use crate::mid::m10::m10;
use crate::mid::m87::m87;

pub fn a2(x: i64) -> i64 {
    m10(x) + m87(x + 2)
}

