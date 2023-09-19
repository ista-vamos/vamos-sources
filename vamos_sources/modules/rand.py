from random import randint

from vamos_common.spec.ir.constant import Constant
from vamos_common.types.type import IntType, NumType, ObjectType
from vamos_sources.interpreter.value import Value
from vamos_sources.interpreter.method import Method
from vamos_sources.spec.ir.expr import MethodCall
from vamos_common.types.methods import MethodHeader


from ._common import gen_params

header_get = MethodHeader(
    "get",
    [],
    NumType(),
)


class UniformDistribution(Value):
    def __init__(self, l, h):
        super().__init__("<UniformDistribution>", None)
        self.l = l
        self.h = h

        def _typing_rules_get(methodcall, types):
            types.assign(methodcall, types.get(l))
            types.assign(methodcall, types.get(h))
            types.assign(l, types.get(methodcall))
            types.assign(h, types.get(methodcall))

        self._method_get = Method(
            MethodHeader("get", [], NumType(), _typing_rules_get),
            lambda s, p: Constant(randint(self.l, self.h), IntType(32)),
        )

    def get_method(self, name):
        if name == "get":
            return self._method_get
        raise NotImplementedError(f"Invalid method: {name}")


def uniform(_, params):
    assert len(params) == 2, params
    l, h = params
    return UniformDistribution(l.value, h.value)


def uni(_, params):
    assert len(params) == 2, params
    l, h = params
    return Constant(randint(l.value, h.value), IntType(32))


class RandUniformTy(ObjectType):
    methods = {"get": header_get}


header_uniform = MethodHeader("rand.uniform", [NumType(), NumType()], RandUniformTy())
header_uni = MethodHeader("rand.uni", [IntType(32), IntType(32)], IntType(32))

METHODS = {
    "uniform": Method(header_uniform, uniform),
    "uni": Method(header_uni, uni),
}


def gen(lang, codegen, stmt, wr, wr_h):
    """
    Generate code in `lang` for `stmt`.
    """
    if lang == "cpp":
        gen_cpp(stmt, codegen, wr, wr_h)
    else:
        raise NotImplementedError(f"Unknown language: {lang}")


def can_use_randint(ty):
    return (isinstance(ty, IntType) and ty.bitwidth <= 32) or isinstance(ty, NumType)


def gen_cpp(stmt, codegen, wr, wr_h):
    if isinstance(stmt, MethodCall):
        if stmt.rhs.name == "uniform":
            wr("__rand_uniform(")
            gen_params(codegen, stmt, wr, wr_h)
            wr(")")
        elif stmt.rhs.name == "uni":
            if can_use_randint(stmt.params[0].type()):
                wr("std::experimental::randint(")
                gen_params(codegen, stmt, wr, wr_h)
                wr(")")
                codegen.add_std_include("experimental/random")
            else:
                raise NotImplementedError(f"Not implemented rand.uni: {stmt}")
        else:
            raise NotImplementedError(f"Unknown method: {stmt}")
    else:
        raise NotImplementedError(f"Unknown stmt: {stmt}")
