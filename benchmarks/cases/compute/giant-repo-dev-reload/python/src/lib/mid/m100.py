from lib.base.b44 import b44
from lib.base.b84 import b84
def m100(x):
    return b44(x) + b84(x + 1)
