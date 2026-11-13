from lib.base.b58 import b58
from lib.base.b98 import b98
def m62(x):
    return b58(x) + b98(x + 1)
