use crate::mid::m87::m87;
use crate::mid::m36::m36;

pub fn a43(x: i64) -> i64 {
    m87(x) + m36(x + 2)
}

