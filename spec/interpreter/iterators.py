from ir.constant import Constant
from ir.type import IterableType, ITERATOR_TYPE


class Iterator:
    """
    Base class for iterators over iterable values.
    """

    def iterator(self):
        return self

    def type(self):
        return ITERATOR_TYPE


class ListIterator(Iterator):
    def __init__(self, val):
        self._val = val
        self._pos = 0

    def has_next(self):
        return self._pos < len(self._val)

    def next(self):
        val = self._val[self._pos]
        self._pos += 1
        return val

class TupleIterator(Iterator):
    def __init__(self, val):
        self._vals = val.value()
        self._pos = 0

    def has_next(self):
        return self._pos < len(self._vals)

    def is_done(self):
        return not self.has_next()

    def next(self):
        p = self._pos
        val = self._vals[p]
        self._pos += 1

        assert isinstance(val, Constant), val
        return val


class FiniteIterator(Iterator):
    """
    Iterator over a finite iterable, i.e., all elements are present
    in the iterable at the time of creating the iterator.
    There is also `FiniteIterator` that waits for new elements to code.
    """
    def __init__(self, vals, ty):
        "ty: type of the constants of each value"
        self._const_ty = ty
        self._last_value = None
        self._done = False
        self.iter = iter(vals)

    def has_next(self):
        assert self._done is False
        try:
            self._last_value = next(self.iter)
            return True
        except StopIteration:
            self._last_value = None
            self._done = True
            return False

    def is_done(self):
        return self._done

    def next(self):
        assert self._last_value is not None, "has_next was not called?"
        return Constant(self._last_value, self._const_ty)


class LazyIterator(Iterator):

    def __init__(self, vals, ty):
        "ty: type of the constants of each value"
        self._const_ty = ty
        self._last_value = None
        self._done = False
        self.iter = iter(vals)

    def has_next(self):
        assert self._done is False
        try:
            self._last_value = next(self.iter)
            return True
        except StopIteration:
            self._last_value = None
            self._done = True
            return False

    def is_done(self):
        raise NotImplementedError("Must be overriden by child method")

    def next(self):
        assert self._last_value is not None, "has_next was not called?"
        return Constant(self._last_value, self._const_ty)
