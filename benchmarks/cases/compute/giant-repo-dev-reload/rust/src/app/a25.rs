use crate::mid::m125::m125;
use crate::mid::m74::m74;

pub fn a25(x: i64) -> i64 {
    m125(x) + m74(x + 2)
}

