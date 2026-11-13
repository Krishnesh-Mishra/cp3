use crate::base::b93::b93;
use crate::base::b5::b5;

pub fn m31(x: i64) -> i64 {
    b93(x) + b5(x + 1)
}

