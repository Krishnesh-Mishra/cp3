from lib.base.b45 import b45
from lib.base.b85 import b85
def m15(x):
    return b45(x) + b85(x + 1)
