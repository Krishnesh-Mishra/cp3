from lib.base.b85 import b85
from lib.base.b125 import b125
def m71(x):
    return b85(x) + b125(x + 1)
