from lib.base.b32 import b32
from lib.base.b72 import b72
def m96(x):
    return b32(x) + b72(x + 1)
