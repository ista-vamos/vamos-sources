from interpreter.value import Value
from ir.constant import Constant


class State:
    def __init__(self):
        self.values = {}
        self.next_instr = {}
        self._scopes = []

    def enter_scope(self, stmt):
        self._scopes.append(stmt)

    def leave_scope(self, stmt):
        assert self._scopes[-1] is stmt
        self._scopes.pop()

    def scope(self):
        return self._scopes[-1] if self._scopes else None

    def scopes(self):
        return self._scopes

    def bind(self, name, val):
        assert isinstance(name, str), (name, type(name))
        assert isinstance(val, (Value, Constant)), val
        self.values[name] = val

    def get(self, name):
        return self.values[name]

    def try_get(self, name):
        return self.values.get(name)

    def __repr__(self):
        return "-- values --\n"  + '\n'.join(map(str, self.values.items())) + "\n-- scopes --\n" + '\n'.join(map(str, self.scopes()))
