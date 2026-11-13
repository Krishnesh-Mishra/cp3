use crate::mid::m65::m65;
use crate::mid::m14::m14;

pub fn a13(x: i64) -> i64 {
    m65(x) + m14(x + 2)
}

