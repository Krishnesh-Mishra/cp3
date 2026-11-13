from lib.base.b120 import b120
from lib.base.b32 import b32
def m40(x):
    return b120(x) + b32(x + 1)
