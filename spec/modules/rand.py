from random import randint

from interpreter.method import Method
from interpreter.value import Value
from ir.constant import Constant
from ir.type import IntType, NumType, ObjectType


class UniformDistribution(Value):
    def __init__(self, l, h):
        super().__init__("<UniformDistribution>", None)
        self.l = l
        self.h = h

        self._method_get = Method("get", [], IntType(32), lambda s, p: Constant(randint(self.l, self.h), IntType(32)))

    def get_method(self, name):
        if name == "get": return self._method_get
        raise NotImplementedError(f"Invalid method: {name}")

def uniform(_, params):
    assert len(params) == 2, params
    l, h = params
    return UniformDistribution(l.value, h.value)


def uni(_, params):
    assert len(params) == 2, params
    l, h = params
    return Constant(randint(l.value, h.value), IntType(32))

METHODS = {
    "uniform" : Method("uniform", [NumType(), NumType()], ObjectType(), uniform),
    "uni" : Method("uniform", [IntType(32), IntType(32)], IntType(32), uni)
}