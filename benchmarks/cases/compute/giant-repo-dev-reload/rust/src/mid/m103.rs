use crate::base::b53::b53;
use crate::base::b93::b93;

pub fn m103(x: i64) -> i64 {
    b53(x) + b93(x + 1)
}

