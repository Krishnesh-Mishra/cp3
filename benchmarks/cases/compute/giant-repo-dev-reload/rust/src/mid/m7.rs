use crate::base::b21::b21;
use crate::base::b61::b61;

pub fn m7(x: i64) -> i64 {
    b21(x) + b61(x + 1)
}

