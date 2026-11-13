use crate::base::b110::b110;
use crate::base::b22::b22;

pub fn m122(x: i64) -> i64 {
    b110(x) + b22(x + 1)
}

