from lib.base.b96 import b96
from lib.base.b8 import b8
def m32(x):
    return b96(x) + b8(x + 1)
