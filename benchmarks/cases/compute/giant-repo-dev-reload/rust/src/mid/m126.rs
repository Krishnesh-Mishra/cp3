use crate::base::b122::b122;
use crate::base::b34::b34;

pub fn m126(x: i64) -> i64 {
    b122(x) + b34(x + 1)
}

