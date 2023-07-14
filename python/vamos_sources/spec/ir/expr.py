from vamos_common.spec.ir.expr import Expr
from vamos_common.types.type import EventType
from vamos_common.types.type import STRING_TYPE, BOOL_TYPE, OBJECT_TYPE


class TupleExpr(Expr):
    def __init__(self, vals, ty):
        super().__init__(ty)
        self.values = vals

    def __repr__(self):
        return f"TupleExpr({','.join(map(str, self.values))})"

    @property
    def children(self):
        return self.values


class Cast(Expr):
    def __init__(self, val, ty):
        super().__init__(ty)
        self._value = val

    @property
    def value(self):
        return self._value

    def pretty_str(self):
        return f"{self.value} as {self.type()}"

    def __repr__(self):
        return f"Cast({self.value}, {self.type()})"

    @property
    def children(self):
        return ()

    def typing_rule(self, types):
        types.assign(self, self.type())


class CommandLineArgument(Expr):
    """
    Argument to the specification from the command line ($1, $2, ...)
    """

    def __init__(self, n):
        super().__init__(STRING_TYPE)
        self.num = n

    def __repr__(self):
        return f"CommandLineArgument({self.num})"

    @property
    def children(self):
        return ()


class BoolExpr(Expr):
    def __init__(self):
        super().__init__(BOOL_TYPE)

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


class IsIn(BoolExpr):
    def __init__(self, lhs, rhs):
        super().__init__()
        self.lhs = lhs
        self.rhs = rhs

    def pretty_str(self):
        return f"{self.lhs.pretty_str()} in {self.rhs.pretty_str()}"

    def __repr__(self):
        return f"IsIn({self.lhs}, {self.rhs})"

    @property
    def children(self):
        return [self.lhs, self.rhs]


class New(Expr):
    def __init__(self, ty, outputs):
        super().__init__(OBJECT_TYPE)

        assert ty is not None
        self.objtype = ty
        self.outputs = outputs

    def __repr__(self):
        return f"New({self.objtype}, {self.outputs})"

    @property
    def children(self):
        return [self.objtype]

    def typing_rule(self, types):
        types.assign(self, self.objtype)


class IfExpr(Expr):
    "if `cond` { `true_stmts` } [else { `false_stmts` }]"

    def __init__(self, cond, true_stmts, false_stmts=None):
        super().__init__(None)
        assert isinstance(cond, Expr), cond
        # assert isinstance(cond._type(), BoolType), cond
        self.cond = cond
        self.true_stmts = true_stmts
        self.false_stmts = false_stmts

    def __repr__(self):
        return f"IfExpr({self.cond}, {self.true_stmts}, {self.false_stmts})"

    @property
    def children(self):
        return self.true_stmts.statements + (self.false_stmts or [])


class MethodCall(Expr):
    """
    Call a method `lhs`.`rhs`. A method is always associated to a module
    and therefore there's the `lhs` parameter. `rhs` is the name of the method.
    """

    def __init__(self, decl, lhs, rhs, params):
        super().__init__(None)
        self.decl = decl
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

    def typing_rule(self, types):
        types.assign(self, self.decl.retty)
        for pa, pf in zip(self.params, self.decl.types):
            types.assign(pa, pf)

        if self.decl.typing_rule:
            self.decl.typing_rule(self, types)


class Event(Expr):
    def __init__(self, decl, name, params):
        assert isinstance(name, str), name
        super().__init__(EventType(name))
        self.decl = decl
        self.name = name
        self.params = params

        assert decl is None or decl.name.name == name, (decl, name)

    def __repr__(self):
        return f"Event({self.name}, {self.params})"

    @property
    def children(self):
        return self.params or ()

    def typing_rule(self, types):
        types.assign(self, self.type())
        for param, field in zip(self.params, self.decl.fields):
            types.assign(param, field.type())


class BinaryOp(Expr):
    def __init__(self, op, lhs, rhs):
        super().__init__()
        self.op = op
        self.lhs = lhs
        self.rhs = rhs

    def pretty_str(self):
        return f"({self.lhs.pretty_str()} {self.op} {self.rhs.pretty_str()})"

    def __repr__(self):
        return f"BinaryOp({self.op}, {self.lhs}, {self.rhs})"

    @property
    def children(self):
        return [self.lhs, self.rhs]

    def typing_rule(self, types):
        types.assign(self, types.get(self.lhs.type()))
        types.assign(self, types.get(self.rhs.type()))
        types.assign(self.lhs, types.get(self.rhs.type()))
        types.assign(self.rhs, types.get(self.rhs.type()))
        types.assign(self.lhs, types.get(self))
        types.assign(self.rhs, types.get(self))
