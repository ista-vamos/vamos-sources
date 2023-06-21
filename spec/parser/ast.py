from lark import Transformer
from lark.visitors import merge_transformers

from ir.type import NumType, type_from_token, UserType, TraceType, HypertraceType
from parser.element import Identifier
from ir.expr import Constant, BoolExpr


class BaseTransformer(Transformer):
    def NUMBER(self, items):
        return Constant(int(items.value), NumType())

    def NAME(self, items):
        return Identifier(str(items.value))


# class ProcessExpr(BaseTransformer):
#     def eq(self, items):
#         assert len(items) == 2, items
#         return CompareExpr("==", items[0].children[0], items[1].children[0])
#
#     def ne(self, items):
#         assert len(items) == 2, items
#         return CompareExpr("!=", items[0].children[0], items[1].children[0])
#
#     def ge(self, items):
#         assert len(items) == 2, items
#         return CompareExpr(">=", items[0].children[0], items[1].children[0])
#
#     def gt(self, items):
#         assert len(items) == 2, items
#         return CompareExpr(">", items[0].children[0], items[1].children[0])
#
#     def land(self, items):
#         if len(items) == 1:
#             return items[0]
#         return And(items[0], items[1])
#
#     def lor(self, items):
#         if len(items) == 1:
#             return items[0]
#         return Or(items[0], items[1])
#
#     def constexpr(self, items):
#         assert isinstance(items[0], ConstExpr), items
#         return items[0]
#
#     def labelexpr(self, items):
#         return Label(items[0])
#
#     def subwordexpr(self, items):
#         return SubWord(items[0], items[1])


class ProcessTypes(BaseTransformer):
    def simpletype(self, items):
        assert len(items) == 1, items
        return type_from_token(items[0])

    def usertype(self, items):
        return UserType(str(items[0]))

    def tracetype(self, items):
        return TraceType(items)

    def hypertracetype(self, items):
        return HypertraceType(items)

    def type(self, items):
        return items[0]


class ProcessAST(BaseTransformer):
    def __init__(self):
        super().__init__()
        self.decls = {}
        self.eventdecls = {}
        self.usertypes = {}

    def boolexpr(self, items):
        assert len(items) == 1, items
        assert isinstance(items[0], BoolExpr)
        return items[0]

    def typeannot(self, items):
        return items[0]

    def hypertracetype_unbounded(self, items):
        return HypertraceType(items, bounded=False)

    def simpletype(self, items):
        assert len(items) == 1, items
        return type_from_token(items[0])

    def type(self, items):
        assert len(items) == 1, items
        assert isinstance(items[0], Type), items
        return items[0]

    def tracelist(self, items):
        return items


# def visit_ast(node, lvl, fn, *args):
#     fn(lvl, node, *args)
#     if node is None:
#         return
#
#     if not hasattr(node, "children"):
#         return
#     for ch in node.children:
#         visit_ast(ch, lvl + 1, fn, args)
#
#
# def prnode(lvl, node, *args):
#     print(" " * lvl * 2, node)


def transform_ast(lark_ast):
    base = ProcessAST()
    T = merge_transformers(
        base,
        comm=ProcessAST(),
        types=ProcessTypes(),
        #expr=ProcessExpr(),
    )
    ast = T.transform(lark_ast)

    return ast
