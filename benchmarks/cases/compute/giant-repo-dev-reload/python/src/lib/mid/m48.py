from lib.base.b16 import b16
from lib.base.b56 import b56
def m48(x):
    return b16(x) + b56(x + 1)
