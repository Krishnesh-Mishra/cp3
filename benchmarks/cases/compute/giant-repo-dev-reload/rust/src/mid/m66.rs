use crate::base::b70::b70;
use crate::base::b110::b110;

pub fn m66(x: i64) -> i64 {
    b70(x) + b110(x + 1)
}

