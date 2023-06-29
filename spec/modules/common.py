from interpreter.iterators import FiniteIterator
from interpreter.method import Method
from ir.type import IntType, NumType, ITERATOR_TYPE


def range_m(_, params):
    assert isinstance(params[0].value, int), params[0]
    assert isinstance(params[1].value, int), params[1]
    return FiniteIterator(range(params[0].value, params[1].value), IntType(64))

METHODS = {
    "range": Method("range", [NumType(), NumType()], ITERATOR_TYPE, range_m)
}
