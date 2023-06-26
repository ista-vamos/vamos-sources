from lark import Transformer
from lark.visitors import merge_transformers

from ir.ir import Event, Yield, Statement, Let, ForEach, StatementList, Import, Program
from ir.type import NumType, type_from_token, UserType, TraceType, HypertraceType, Type, StringType
from ir.element import Identifier, Element
from ir.expr import Constant, BoolExpr, New, CommandLineArgument, Expr, MethodCall, IfExpr


class BaseTransformer(Transformer):
   #def NUMBER(self, items):
   #    return Constant(int(items.value), NumType())

    def constant_string(self, items):
        # strip quotes from the string
        return Constant(items[0][1:-1], StringType())

    def constant_number(self, items):
        return Constant(int(items[0]), NumType())


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

    def newexpr(self, items):
        return New(items[0])

    def methodcall(self, items):
        module = items[0].children[0]
        method = items[1].children[0]
        params = []
        for item in (items[2] or ()):
            if isinstance(item, Expr):
                params.append(item)
            else:
                assert item.data == "name", item
                params.append(item.children[0])
        return MethodCall(module, method, params)

    def expr(self, items):
        assert isinstance(items[0], Expr), items
        return items[0]

    def start(self, items):
        return Program(items[0], items[1], StatementList(items[2]))
    def eventseq(self, items):
        return items

    def statement(self, items):
        assert isinstance(items[0], (Statement, Expr)), items
        assert len(items) == 1, items
        return items[0]

    def statements(self, items):
        return StatementList(items)

    def imports(self, items):
        return items

    def importmod(self, items):
        return Import(items[0].children[0])

    def foreach(self, items):
        iterable = items[1].children[0]
        if not isinstance(iterable, Element):
            # then it is AST node
            if iterable.data == "name":
                iterable = iterable.children[0]
            else:
                raise NotImplementedError(f"Unknown type of iterable: {iterable}")
        return ForEach(items[0].children[0],
                       iterable,
                       items[2])
    def yieldto(self, items):
        return Yield(items[0], items[1].children[0])

    def event(self, items):
        name = items[0].children[0]
        params = []
        for p in items[1]:
            if isinstance(p, Expr):
                params.append(p)
            else:
                assert p.data == "name", p
                params.append(p.children[0])
        return Event(name, params)

    def specarg(self, items):
        return CommandLineArgument(items[0])

    def params(self, items):
        return items

    def ifexpr(self, items):
        assert isinstance(items[0], Expr), items[0]
        assert isinstance(items[1], StatementList), items[1]
        assert items[2] is None or isinstance(items[2], StatementList), items[1]
        return IfExpr(items[0], items[1], items[2])


    def let(self, items):
        assert len(items) == 2, items
        return Let(items[0].children[0], items[1])

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


def visit_ast(node, lvl, fn, *args):
    fn(lvl, node, *args)
    if node is None:
        return

    if not hasattr(node, "children"):
        return
    for ch in node.children:
        visit_ast(ch, lvl + 1, fn, args)


def prnode(lvl, node, *args):
    print(" " * lvl * 2, node)


def transform_ast(lark_ast):
    base = ProcessAST()
    T = merge_transformers(
        base,
        comm=ProcessAST(),
        types=ProcessTypes(),
        #expr=ProcessExpr(),
    )
    ast = T.transform(lark_ast)

    visit_ast(ast, 1, prnode)

    return ast
