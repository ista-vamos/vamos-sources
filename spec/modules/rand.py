from random import randint

from interpreter.value import Value


class UniformDistribution(Value):
    def __init__(self, l, h):
        super().__init__("<UniformDistribution>", None)
        self.l = l
        self.h = h

    def get_method(self, name):
        if name == "get": return lambda s, p: randint(self.l, self.h)
        raise NotImplementedError(f"Invalid method: {name}")

def uniform(_, params):
    assert len(params) == 2, params
    l, h = params
    return UniformDistribution(l, h)

METHODS = {
    "uniform" : uniform
}