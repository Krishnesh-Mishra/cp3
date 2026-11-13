use crate::base::b85::b85;
use crate::base::b125::b125;

pub fn m71(x: i64) -> i64 {
    b85(x) + b125(x + 1)
}

