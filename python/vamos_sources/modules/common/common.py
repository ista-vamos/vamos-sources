from os.path import abspath, dirname, join as pathjoin

from vamos_common.codegen.lang.cpp import cpp_type
from vamos_common.types.methods import MethodHeader
from vamos_common.types.type import IntType, NumType, IteratorType
from vamos_sources.interpreter.iterators import FiniteIterator
from vamos_sources.interpreter.method import Method
from vamos_sources.spec.ir.expr import MethodCall

from .._common import gen_params


def range_m(_, params):
    assert isinstance(params[0].value, int), params[0]
    assert isinstance(params[1].value, int), params[1]
    return FiniteIterator(range(params[0].value, params[1].value), IntType(64))


def range_typing(methodcall, types):
    """
    Type restrictions for the `range` method -- the input parameters should have the same type
    which is the same as the return type.
    """
    params = methodcall.params
    types.dump()

    types.assign(params[0], types.get(params[1]))
    types.assign(params[1], types.get(params[0]))
    types.assign(methodcall, IteratorType(types.get(params[0])))
    types.assign(methodcall, IteratorType(types.get(params[1])))

    retty = types.get(methodcall)
    types.assign(params[0], retty.elem_ty())
    types.assign(params[1], retty.elem_ty())


header_range = MethodHeader(
    "common.range", [NumType(), NumType()], IteratorType(NumType()), range_typing
)

METHODS = {"range": Method(header_range, range_m)}


def gen(lang, codegen, stmt, wr, wr_h):
    """
    Generate code in `lang` for `stmt`.
    """
    if lang == "cpp":
        gen_cpp(stmt, codegen, wr, wr_h)
    else:
        raise NotImplementedError(f"Unknown language: {lang}")


def gen_cpp(stmt, codegen, wr, wr_h):
    if isinstance(stmt, MethodCall):
        if stmt.rhs.name == "range":
            assert len(stmt.params) == 2, stmt
            wr(f"CommonRange<{cpp_type(stmt.params[0].type())}>(")
            gen_params(codegen, stmt, wr, wr_h)
            wr(")")
            codegen.add_include("common-range.h")
            codegen.add_copy_file(
                abspath(pathjoin(dirname(__file__), "codegen/cpp/common-range.h"))
            )
        else:
            raise NotImplementedError(f"Unknown method: {stmt.rhs.name}")
    else:
        raise NotImplementedError(f"Unknown stmt: {stmt}")
