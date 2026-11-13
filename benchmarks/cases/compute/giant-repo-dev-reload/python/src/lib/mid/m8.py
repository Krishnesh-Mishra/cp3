from lib.base.b24 import b24
from lib.base.b64 import b64
def m8(x):
    return b24(x) + b64(x + 1)
