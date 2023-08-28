from importlib import import_module

from lark.visitors import merge_transformers
from vamos_common.parser.ast import BaseTransformer, ProcessEvents, ProcessTypes, ProcessExpr
from vamos_common.parser.ast import visit_ast
from vamos_common.parser.context import Context
from vamos_common.spec.ir.element import Element
from vamos_common.spec.ir.identifier import Identifier
from vamos_common.types.type import (
    type_from_token,
    HypertraceType,
    Type,
)

from ..ir.expr import (
    New,
    Expr,
    MethodCall,
    IfExpr,
)
from ..ir.ir import (
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


class ProcessAST(ProcessExpr):
    def newexpr(self, items):
        outputs = []
        if len(items) > 1:
            for item in items[1:]:
                if item.data == "stdout":
                    outputs.append("stdout")
                elif item.data == "hypertrace":
                    outputs += item.children[0].children
        return New(items[0], outputs)

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

    def start(self, items):
        return Program(items[0][1], items[0][0], StatementList(items[1]))

    def inlined(self, items):
        assert len(items) == 2, items
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
        #self._imports = items
        return items

    def events_and_imports(self, items):
        _imports = items[0]
        _events = items[1]
        assert all((e.name.name in self.ctx.eventdecls for e in _events)), (
            _events,
            self.ctx.eventdecls,
        )
        return items

    def importmod(self, items):
        name = items[0].children[0]
        assert isinstance(name, Identifier), name

        print(f"Importing: {name.name}", end=" ")
        mod = import_module(f"vamos_sources.modules.{name.name}")
        print(f"-> {mod}")
        self.ctx.add_module(name, mod)
        return Import(name)

    def eventsspec(self, items):
        if items[0] is not None:
            it = items[0]
            if it.data == "eventsfile":
                eventsfile = it.children[0]
                raise NotImplementedError(
                    f"events file: {eventsfile}... not implemented"
                )
        allevents = []
        for it in items[1:]:
            assert it.data == "eventdecl", it
            events = it.children[0]
            allevents.extend(events)
            # self.ctx.add_eventdecl(*events)
        # self.ctx.dump()
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


def prnode(lvl, node, *args):
    print(" " * lvl * 2, node)


def transform_ast(lark_ast, ctx=None):
    print(lark_ast.pretty())
    ctx = ctx or Context()
    base = ProcessAST(ctx)
    T = merge_transformers(
        base,
        comm=BaseTransformer(ctx),
        events=merge_transformers(
            ProcessEvents(ctx), comm=BaseTransformer(ctx), types=ProcessTypes(ctx)
        ),
        types=ProcessTypes(ctx),
        # expr=ProcessExpr(),
    )
    ast = T.transform(lark_ast)

    visit_ast(ast, 1, prnode)

    from vamos_common.types.typecheck import TypeChecker

    tc = TypeChecker(ctx)
    tc.typecheck(ast)
    for i, ty in tc.types().items():
        print("TY:", i, "->", ty)
    ctx.add_typecheck_results(tc)

    return ast, ctx
