from lib.base.b62 import b62
from lib.base.b102 import b102
def m106(x):
    return b62(x) + b102(x + 1)
