from lib.base.b61 import b61
from lib.base.b101 import b101
def m63(x):
    return b61(x) + b101(x + 1)
