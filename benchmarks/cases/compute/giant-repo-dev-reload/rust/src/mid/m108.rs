use crate::base::b68::b68;
use crate::base::b108::b108;

pub fn m108(x: i64) -> i64 {
    b68(x) + b108(x + 1)
}

