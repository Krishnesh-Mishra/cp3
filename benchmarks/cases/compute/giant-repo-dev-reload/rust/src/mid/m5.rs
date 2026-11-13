use crate::base::b15::b15;
use crate::base::b55::b55;

pub fn m5(x: i64) -> i64 {
    b15(x) + b55(x + 1)
}

