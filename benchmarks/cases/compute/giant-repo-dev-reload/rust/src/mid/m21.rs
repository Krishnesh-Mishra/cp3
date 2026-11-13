use crate::base::b63::b63;
use crate::base::b103::b103;

pub fn m21(x: i64) -> i64 {
    b63(x) + b103(x + 1)
}

