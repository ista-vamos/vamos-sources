from interpreter.iterators import TupleIterator
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

    def iterator(self):
        raise NotImplementedError(f"Child must override: {type(self)}")


class Tuple(Iterable):
    def iterator(self):
        return TupleIterator(self)

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
