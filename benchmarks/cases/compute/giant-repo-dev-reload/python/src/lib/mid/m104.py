from lib.base.b56 import b56
from lib.base.b96 import b96
def m104(x):
    return b56(x) + b96(x + 1)
