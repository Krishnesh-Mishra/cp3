use crate::mid::m49::m49;
use crate::mid::m126::m126;

pub fn a61(x: i64) -> i64 {
    m49(x) + m126(x + 2)
}

