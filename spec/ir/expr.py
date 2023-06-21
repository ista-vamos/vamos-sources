from parser.element import Element
from ir.type import BoolType


class Expr(Element):
    pass


class Constant(Expr):
    def __init__(self, c, ty=None):
        super().__init__()
        self.value = c
        self.type = ty

    def pretty_str(self):
        return f"{self.value} : {self.type or ''}"

    def __str__(self):
        return f"CONST({self.value} : {self.type or ''})"

    def __repr__(self):
        return f"Constant({self.value} : {self.type or ''})"


class Var(Expr):
    def __init__(self, v):
        super().__init__()
        self.name = v

    def pretty_str(self):
        return self.name

    def __str__(self):
        return f"VAR({self.name})"

    def __repr__(self):
        return f"Var({self.name})"


class BoolExpr(Expr):
    def __init__(self):
        super().__init__()
        self.type = BoolType


class And(BoolExpr):
    def __init__(self, lhs, rhs):
        super().__init__()
        self.lhs = lhs
        self.rhs = rhs

    def pretty_str(self):
        return f"({self.lhs.pretty_str()} && {self.rhs.pretty_str()})"

    def __str__(self):
        return f"({self.lhs} && {self.rhs})"

    def __repr__(self):
        return f"And({self.lhs} && {self.rhs})"


class Or(BoolExpr):
    def __init__(self, lhs, rhs):
        super().__init__()
        self.lhs = lhs
        self.rhs = rhs

    def pretty_str(self):
        return f"({self.lhs.pretty_str()} || {self.rhs.pretty_str()})"

    def __str__(self):
        return f"({self.lhs} || {self.rhs})"

    def __repr__(self):
        return f"Or({self.lhs} || {self.rhs})"


class CompareExpr(BoolExpr):
    def __init__(self, comparison, lhs, rhs):
        super().__init__()
        self.comparison = comparison
        self.lhs = lhs
        self.rhs = rhs

    def pretty_str(self):
        return f"{self.lhs.pretty_str()} {self.comparison} {self.rhs.pretty_str()}"

    def __str__(self):
        return f"{self.lhs} {self.comparison} {self.rhs}"

    def __repr__(self):
        return f"CompareExpr({self.lhs} {self.comparison} {self.rhs})"

    @property
    def children(self):
        return [self.lhs, self.rhs]


