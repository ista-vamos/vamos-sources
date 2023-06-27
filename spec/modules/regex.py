from re import match as re_match, search as re_search

from interpreter.value import Value
from ir.expr import Constant
from ir.type import StringType, BoolType


class ReMatch(Value):
    def __init__(self, s, rexp, search=False):
        super().__init__("<REMatch>", "<rematch>")
        self.string = s

        assert isinstance(rexp, Constant), rexp
        assert rexp.type == StringType(), rexp
        re = str(rexp.value)
        text = str(s.value)
        self.matched = re_search(re, text) if search else re_match(re, text)

    def re_match_get(self, state, params):
        assert isinstance(params[0].value, int), params
        return Constant(self.matched[params[0].value], StringType())

    def get_method(self, name):
        if name == "matched":
            return lambda state, params: Constant(bool(self.matched), BoolType())
        if name == "get":
            return lambda state, params: self.re_match_get(state, params)
        if name == "has":
            return lambda state, params: Constant(
                params[0].value <= self.matched.re.groups, BoolType()
            )
        raise RuntimeError(f"Invalid method: {name}")

    def __repr__(self):
        return f"ReMatch({self.string.value[:10]}.., {self.matched})"


def match(state, params):
    return ReMatch(params[0], params[1])


def search(state, params):
    return ReMatch(params[0], params[1], search=True)


METHODS = {"match": match, "search": search}
