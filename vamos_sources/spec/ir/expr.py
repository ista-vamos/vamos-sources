from vamos_common.spec.ir.expr import Expr
from vamos_common.types.type import (
    BOOL_TYPE,
    OBJECT_TYPE,
)


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

    def typing_rule(self, types):
        types.assign(self.cond, BOOL_TYPE)
        types.visit(self.cond)

    # types.assign(self, types.get(self.true_stmts[-1]))
    # if self.false_stmts:
    #    types.assign(self, types.get(self.false_stmts[-1]))


class MethodCall(Expr):
    """
    Call a method `lhs`.`rhs`. A method is always associated to a module
    and therefore there's the `lhs` parameter. `rhs` is the name of the method.
    """

    def __init__(self, lhs, rhs, params):
        super().__init__(None)
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
        header = types.get_method(self.lhs, self.rhs)
        if header:
            types.assign(self, header.retty)
            for pa, pf in zip(self.params, header.types):
                types.assign(pa, pf)

            if header.typing_rule:
                header.typing_rule(self, types)
