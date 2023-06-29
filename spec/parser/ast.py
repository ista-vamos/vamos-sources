from lark import Transformer
from lark.visitors import merge_transformers

from ir.element import Identifier, Element
from ir.expr import (
    BoolExpr,
    New,
    CommandLineArgument,
    Expr,
    MethodCall,
    IfExpr,
    TupleExpr,
    IsIn,
)
from ir.constant import Constant
from ir.ir import (
    Event,
    Yield,
    Statement,
    Let,
    ForEach,
    StatementList,
    Import,
    Program,
    OutputDecl,
    Continue,
    Break,
)
from ir.type import (
    NumType,
    type_from_token,
    UserType,
    TraceType,
    HypertraceType,
    Type,
    StringType,
    TupleType,
)
from parser.decls import DataField, EventDecl


class BaseTransformer(Transformer):
    # def NUMBER(self, items):
    #    return Constant(int(items.value), NumType())
    def constant_tuple(self, items):
        return TupleExpr(items, TupleType([it.type for it in items]))

    def constant_string(self, items):
        # strip quotes from the string
        return Constant(items[0][1:-1], StringType())

    def constant_number(self, items):
        return Constant(int(items[0]), NumType())

    def constant(self, items):
        assert len(items) == 1
        assert isinstance(items[0], Expr), items[0]
        return items[0]

    def NAME(self, items):
        return Identifier(str(items.value))


# class ProcessExpr(BaseTransformer):
#     def const_or_var(self, items):
#         if isinstance(items[0], (Identifier, Expr)):
#             return items[0]
#         raise NotImplementedError(str(items[0], type(items[0])))


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

class ProcessEvents(BaseTransformer):
    def __init__(self):
        super().__init__()
        self.eventdecls = {}

    def datafield(self, items):
        name = items[0].children[0]
        ty = items[1].children[0]
        assert isinstance(ty, Type), items[1]
        return DataField(name, ty)

    def fieldsdecl(self, items):
        return items

    def eventdecl(self, items):
        names = items[0].children
        fields = items[1] if len(items) > 1 else []

        decls = []
        for name in names:
            assert name.name not in self.eventdecls, name
            ev = EventDecl(name, fields)
            self.eventdecls[name.name] = ev
            decls.append(ev)
        return decls


class ProcessAST(BaseTransformer):
    def __init__(self):
        super().__init__()
        self.decls = {}
        self.eventdecls = {}
        self.usertypes = {}

    def boolexpr(self, items):
        return items
        assert len(items) == 1, items
        assert isinstance(items[0], BoolExpr), items
        return items[0]

    def expr(self, items):
        assert len(items) == 1, items
        assert isinstance(items[0], Expr), items
        return items[0]

    def is_in(self, items):
        lhs = items[0] if isinstance(items[0], Expr) else items[0].children[0]
        assert isinstance(lhs, (Expr, Identifier)), lhs

        rhs = items[1] if isinstance(items[1], Expr) else items[1].children[0]
        assert isinstance(rhs, (Expr, Identifier)), rhs
        return IsIn(lhs, rhs)


    def newexpr(self, items):
        return New(items[0])

    def methodcall(self, items):
        lhs = items[0]
        if not isinstance(lhs, Expr):
            assert items[0].data == "name", items[0]
            lhs = items[0].children[0]
        method = items[1].children[0]
        params = []
        for item in items[2] or ():
            if isinstance(item, Expr):
                params.append(item)
            else:
                assert item.data == "name", item
                params.append(item.children[0])
        return MethodCall(lhs, method, params)


    def expr(self, items):
        if isinstance(items[0], Expr):
            return items[0]
        assert items[0].data == "name", items
        # this is an identifier
        return items[0]

    def start(self, items):
        return Program(items[0][1], items[0][0], StatementList(items[1]))

    def eventseq(self, items):
        return items

    def cont(self, _):
        return Continue()

    def brk(self, _):
        return Break()

    def statement(self, items):
        assert isinstance(items[0], (Statement, Expr)), items
        assert len(items) == 1, items
        return items[0]

    def out(self, items):
        assert items[0].data == "name", items[0]
        assert isinstance(items[1], Expr), (items[1], type(items[1]))
        trace = items[0].children[0]
        out = items[1]
        return OutputDecl(trace, out)

    def statements(self, items):
        return StatementList(items)

    def imports(self, items):
        self.imports = items
        return items

    def events_and_imports(self, items):
        _imports = items[0]
        _events = items[1]
        assert all((e.name.name in self.eventdecls for e in _events)), (_events, self.eventdecls)
        return items

    def importmod(self, items):
        return Import(items[0].children[0])

    def eventsspec(self, items):
        if items[0] is not None:
            it = items[0]
            if it.data == "eventsfile":
                eventsfile = it.children[0]
                raise NotImplementedError(f"events file: {eventsfile}... not implemented")
        allevents = []
        for it in items[1:]:
            assert it.data == "eventdecl", it
            events = it.children[0]
            allevents.extend(events)
            for ev in events:
                assert ev.name.name not in self.eventdecls, ev
                self.eventdecls[ev.name.name] = ev
        return allevents

    def foreach(self, items):
        iterable = items[1].children[0]
        if not isinstance(iterable, Element):
            # then it is AST node
            if iterable.data == "name":
                iterable = iterable.children[0]
            else:
                raise NotImplementedError(f"Unknown type of iterable: {iterable}")
        return ForEach(items[0].children[0], iterable, items[2])

    def yieldto(self, items):
        return Yield(items[0], items[1].children[0])

    def event(self, items):
        name = items[0].children[0]
        params = []
        for p in items[1] or ():
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
    print(lark_ast.pretty())
    base = ProcessAST()
    T = merge_transformers(
        base,
        comm=BaseTransformer(),
        events=merge_transformers(ProcessEvents(), comm=BaseTransformer(), types=ProcessTypes()),
        types=ProcessTypes(),
        #expr=ProcessExpr(),
    )
    ast = T.transform(lark_ast)

    visit_ast(ast, 1, prnode)

    return ast
