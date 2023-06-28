from ir.expr import Constant
from ir.type import IterableType


class Value:
    def __init__(self, val, ty):
        self._value = val
        self._type = ty

    def type(self):
        return self._type

    def value(self):
        return self._value

    def __repr__(self):
        return f"({self.value()} : {self.type()})"


class Iterable(Value):
    def __init__(self, val, ty):
        super().__init__(val, ty)
        assert isinstance(ty, IterableType), ty

    def has_next(self):
        raise NotImplementedError(f"Child must override: {type(self)}")

    def next(self):
        raise NotImplementedError(f"Child must override: {type(self)}")

class Tuple(Iterable):
    def __init__(self, vals, ty):
        super().__init__(vals, ty)

    def __repr__(self):
        return f"Tuple({','.join(map(str, self.value()))})"

class Trace(Iterable):
    next_id = 1

    def __init__(self, ty, name=None, out=None):
        super().__init__(f"<Trace>-{Trace.next_id}" if name is None else name, ty)
        self._id = Trace.next_id
        Trace.next_id += 1
        self.events = []
        self._pushed = 0
        self.out = out

    def id(self):
        return self._id

    def size(self):
        return self._pushed

    def push(self, elem):
        self._pushed += 1
        if self.out:
            self.out.push(self, elem)
        else:
            self.events.append(elem)


class Iterator(Iterable):
    pass


class ListIterator(Iterator):
    def __init__(self, val):
        super().__init__(val, IterableType())
        self._pos = 0

    def has_next(self):
        return self._pos < len(self.value())

    def next(self):
        val = self.value()[self._pos]
        self._pos += 1
        return val


class LazyIterator(Iterator):
    def __init__(self, vals, ty):
        "ty: type of the constants of each value"
        super().__init__(vals, IterableType())
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

    def next(self):
        assert self._last_value is not None, "has_next was not called?"
        return Constant(self._last_value, self._const_ty)


class Object:
    """
    Object is identified by its methods.
    """

    def __init__(self, ty):
        self._type = ty

    def type(self):
        return self._type


class Process(Object):
    pass
