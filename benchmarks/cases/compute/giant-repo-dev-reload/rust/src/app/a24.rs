use crate::mid::m120::m120;
use crate::mid::m69::m69;

pub fn a24(x: i64) -> i64 {
    m120(x) + m69(x + 2)
}

