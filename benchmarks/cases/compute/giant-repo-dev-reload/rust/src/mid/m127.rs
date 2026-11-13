use crate::base::b125::b125;
use crate::base::b37::b37;

pub fn m127(x: i64) -> i64 {
    b125(x) + b37(x + 1)
}

