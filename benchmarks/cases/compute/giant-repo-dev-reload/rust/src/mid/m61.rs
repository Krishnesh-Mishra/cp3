use crate::base::b55::b55;
use crate::base::b95::b95;

pub fn m61(x: i64) -> i64 {
    b55(x) + b95(x + 1)
}

