from lib.base.b64 import b64
from lib.base.b104 import b104
def m64(x):
    return b64(x) + b104(x + 1)
