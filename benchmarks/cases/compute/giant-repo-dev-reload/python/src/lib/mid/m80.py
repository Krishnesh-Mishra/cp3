from lib.base.b112 import b112
from lib.base.b24 import b24
def m80(x):
    return b112(x) + b24(x + 1)
