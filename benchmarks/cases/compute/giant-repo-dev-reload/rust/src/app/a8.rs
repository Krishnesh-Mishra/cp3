use crate::mid::m40::m40;
use crate::mid::m117::m117;

pub fn a8(x: i64) -> i64 {
    m40(x) + m117(x + 2)
}

