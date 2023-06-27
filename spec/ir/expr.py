from ir.element import Element
from ir.type import BoolType


class Expr(Element):
    """
    Class representing an expression in the language
    """


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

    @property
    def children(self):
        return ()


#
# class Var(Expr):
#     def __init__(self, v):
#         super().__init__()
#         self.name = v
#
#     def pretty_str(self):
#         return self.name
#
#     def __str__(self):
#         return f"VAR({self.name})"
#
#     def __repr__(self):
#         return f"Var({self.name})"


class CommandLineArgument(Expr):
    """
    Argument to the specification from the command line ($1, $2, ...)
    """

    def __init__(self, n):
        super().__init__()
        self.num = n

    def __repr__(self):
        return f"CommandLineArgument({self.num})"

    @property
    def children(self):
        return ()


class BoolExpr(Expr):
    def __init__(self):
        super().__init__()
        self.type = BoolType

    @property
    def children(self):
        return ()


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

    @property
    def children(self):
        return [self.lhs, self.rhs]


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

    @property
    def children(self):
        return [self.lhs, self.rhs]


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


class New(Expr):
    def __init__(self, ty):
        super().__init__()

        assert ty is not None
        self.objtype = ty

    def __repr__(self):
        return f"New({self.objtype})"

    @property
    def children(self):
        return [self.objtype]


class IfExpr(Expr):
    "if `cond` { `true_stmts` } [else { `false_stmts` }]"

    def __init__(self, cond, true_stmts, false_stmts=None):
        super().__init__(BoolType())
        assert isinstance(cond, Expr), cond
        self.cond = cond
        self.true_stmts = true_stmts
        self.false_stmts = false_stmts

    def __repr__(self):
        return f"IfExpr({self.cond}, {self.true_stmts}, {self.false_stmts})"

    @property
    def children(self):
        return self.true_stmts.statements + (self.false_stmts or [])


class MethodCall(Expr):
    def __init__(self, lhs, rhs, params):
        super().__init__()
        self.lhs = lhs
        self.rhs = rhs
        self.params = params

    # def pretty_str(self):
    #     return f"({self.lhs.pretty_str()} && {self.rhs.pretty_str()})"

    def __repr__(self):
        return f"MethodCall({self.lhs}, {self.rhs}, {self.params})"

    @property
    def children(self):
        return self.params or ()
