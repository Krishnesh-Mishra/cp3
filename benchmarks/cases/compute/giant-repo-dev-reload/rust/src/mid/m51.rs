use crate::base::b25::b25;
use crate::base::b65::b65;

pub fn m51(x: i64) -> i64 {
    b25(x) + b65(x + 1)
}

