use crate::mid::m112::m112;
use crate::mid::m61::m61;

pub fn a48(x: i64) -> i64 {
    m112(x) + m61(x + 2)
}

