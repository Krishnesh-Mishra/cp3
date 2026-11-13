from lib.base.b36 import b36
from lib.base.b76 import b76
def m12(x):
    return b36(x) + b76(x + 1)
