use crate::mid::m14::m14;
use crate::mid::m91::m91;

pub fn a54(x: i64) -> i64 {
    m14(x) + m91(x + 2)
}

