from re import match as re_match, search as re_search

from vamos_common.spec.ir.constant import Constant
from vamos_common.types.type import (
    BoolType,
    BOOL_TYPE,
    STRING_TYPE,
    UIntType,
    ObjectType,
)
from vamos_sources.interpreter.method import Method
from vamos_sources.interpreter.value import Value


class ReMatch(Value):
    def __init__(self, s, rexp, _search=False):
        super().__init__("<REMatch>", "<rematch>")
        self.string = s

        assert isinstance(rexp, Constant), rexp
        assert rexp.type() == STRING_TYPE, rexp
        re = str(rexp.value)
        text = str(s.value)
        self.matched = re_search(re, text) if _search else re_match(re, text)

    def re_match_get(self, _, params):
        assert isinstance(params[0].value, int), params
        return Constant(self.matched[params[0].value], STRING_TYPE)

    def get_method(self, name):
        if name == "matched":
            return Method(
                "regex.matched",
                [],
                BOOL_TYPE,
                lambda state, params: Constant(bool(self.matched), BoolType()),
            )
        if name == "get":
            return Method(
                "regex.get",
                [UIntType(64)],
                STRING_TYPE,
                lambda state, params: self.re_match_get(state, params),
            )
        if name == "has":
            return Method(
                "regex.has",
                [UIntType(64)],
                STRING_TYPE,
                lambda state, params: Constant(
                    params[0].value <= self.matched.re.groups, BoolType()
                ),
            )
        raise RuntimeError(f"Invalid method: {name}")

    def __repr__(self):
        return f"ReMatch({self.string.value[:10]}.., {self.matched})"


def match(_, params):
    return ReMatch(params[0], params[1])


def search(_, params):
    return ReMatch(params[0], params[1], _search=True)


METHODS = {
    "match": Method("regex.match", [STRING_TYPE, STRING_TYPE], ObjectType(), match),
    "search": Method("regex.search", [STRING_TYPE, STRING_TYPE], ObjectType(), search),
}
