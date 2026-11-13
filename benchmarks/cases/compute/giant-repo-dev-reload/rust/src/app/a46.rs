use crate::mid::m102::m102;
use crate::mid::m51::m51;

pub fn a46(x: i64) -> i64 {
    m102(x) + m51(x + 2)
}

