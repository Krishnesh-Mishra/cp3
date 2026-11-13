use crate::mid::m35::m35;
use crate::mid::m112::m112;

pub fn a7(x: i64) -> i64 {
    m35(x) + m112(x + 2)
}

