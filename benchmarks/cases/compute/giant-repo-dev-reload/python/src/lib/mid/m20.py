from lib.base.b60 import b60
from lib.base.b100 import b100
def m20(x):
    return b60(x) + b100(x + 1)
