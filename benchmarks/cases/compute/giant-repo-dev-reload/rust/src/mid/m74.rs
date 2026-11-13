use crate::base::b94::b94;
use crate::base::b6::b6;

pub fn m74(x: i64) -> i64 {
    b94(x) + b6(x + 1)
}

