from lib.base.b18 import b18
from lib.base.b58 import b58
def m6(x):
    return b18(x) + b58(x + 1)
