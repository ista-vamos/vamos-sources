from vamos_common.codegen.lang.cpp import cpp_type
from vamos_common.types.type import IntType, NumType, ITERATOR_TYPE
from vamos_sources.interpreter.iterators import FiniteIterator
from vamos_sources.interpreter.method import Method
from vamos_sources.spec.ir.expr import MethodCall

from .._common import gen_params

from os.path import abspath, dirname, join as pathjoin


def range_m(_, params):
    assert isinstance(params[0].value, int), params[0]
    assert isinstance(params[1].value, int), params[1]
    return FiniteIterator(range(params[0].value, params[1].value), IntType(64))


METHODS = {"range": Method("range", [NumType(), NumType()], ITERATOR_TYPE, range_m)}


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
