use crate::mid::m70::m70;
use crate::mid::m19::m19;

pub fn a14(x: i64) -> i64 {
    m70(x) + m19(x + 2)
}

