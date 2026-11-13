use crate::mid::m0::m0;
use crate::mid::m77::m77;

pub fn a0(x: i64) -> i64 {
    m0(x) + m77(x + 2)
}

pub use crate::mid::m0::greet;
