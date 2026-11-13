from lib.base.b121 import b121
from lib.base.b33 import b33
def m83(x):
    return b121(x) + b33(x + 1)
