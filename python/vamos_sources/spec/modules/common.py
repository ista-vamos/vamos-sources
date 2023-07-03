from .. interpreter.iterators import FiniteIterator
from .. interpreter.method import Method
from .. ir.type import IntType, NumType, ITERATOR_TYPE
from .. ir.expr import MethodCall


def range_m(_, params):
    assert isinstance(params[0].value, int), params[0]
    assert isinstance(params[1].value, int), params[1]
    return FiniteIterator(range(params[0].value, params[1].value), IntType(64))


METHODS = {"range": Method("range", [NumType(), NumType()], ITERATOR_TYPE, range_m)}

def gen(lang, stmt, wr, declarations):
    """
    Generate code in `lang` for `stmt`.
    """
    if lang == "cpp":
        gen_cpp(stmt, wr, declarations)
    else:
        raise NotImplementedError(f"Unknown language: {lang}")


def gen_cpp(stmt, wr, declarations):
    if isinstance(stmt, MethodCall):
        if stmt.rhs.name == "range":
            wr("__common_range()")
