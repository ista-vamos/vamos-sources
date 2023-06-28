from interpreter.value import FiniteIterator
from ir.type import IntType


def range_m(_, params):
    return LazyIterator(range(params[0], params[1]), IntType(64))

METHODS = {
    "range": range_m
}
