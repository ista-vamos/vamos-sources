from spec.ir.type import BoolType


class Element:
    def __init__(self, ty):
        self._type = ty

    def type(self):
        return self._type
class Program(Element):
    def __init__(self):
        super().__init__(None)
        self.stmts = []

class Statement(Element):
    def __init__(self):
        super().__init__(None)
class RunCommand(Statement):
    def __init__(self, cmd):
        super().__init__()
        self.cmd = cmd

class Yield(Statement):
    def __init__(self, events, trace):
        super().__init__()
        self.events = events
        self.trace = trace

class New(Statement):
    def __init__(self, ty):
        super().__init__()
        self.type = ty

class ForEach(Statement):
    """
    foreach `val` in `iterable` { }
    """
    def __init__(self, val, iterable, stmts):
        super.__init__()
        self.value = val
        self.iterable = iterable
        self.stmts = stmts

class Expr(Element):
    def __init__(self, ty):
        super().__init__(ty)


class IfExpr(Expr):
    "if `cond` { `true_stmts` } [else { `false_stmts` }]"
    def __init__(self, cond, true_stmts, false_stmts=None):
        super.__init__(BoolType())
        assert isinstance(cond, Expr), cond
        assert cond.type().is_bool(), cond
        self.cond = cond
        self.true_stmts = true_stmts
        self.false_stmts = false_stmts

class Object(Element):
    """
    Object is identified by its methods.
    """
    pass

class Process(Object):
    pass