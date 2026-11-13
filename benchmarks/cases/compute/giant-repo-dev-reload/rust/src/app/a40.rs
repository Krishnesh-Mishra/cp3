use crate::mid::m72::m72;
use crate::mid::m21::m21;

pub fn a40(x: i64) -> i64 {
    m72(x) + m21(x + 2)
}

