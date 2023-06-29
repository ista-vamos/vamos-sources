import sys
from os import readlink
from os.path import abspath, dirname, islink

from interpreter.iterators import ListIterator, Iterator
from interpreter.method import Method
from interpreter.module import Module
from interpreter.state import State
from interpreter.typemethods import initialize_type_methods
from interpreter.value import Iterable, Value, Trace, Tuple
from ir.constant import Constant
from ir.element import Identifier
from ir.expr import MethodCall, New, IfExpr, CommandLineArgument, Expr, IsIn, TupleExpr
from ir.ir import Let, Yield, StatementList, ForEach, Event, Continue, Break
from ir.type import OutputType, STRING_TYPE, BOOL_TYPE

CONTINUE = 2
BREAK = 3

class StdoutOutput(Value):
    def __init__(self):
        super().__init__("<stdout>", OutputType())

    def push(self, trace, event):
        assert isinstance(trace, Trace), trace
        assert isinstance(event, Event), event

        print(
            f"[{trace.id()}]: {trace.size()}: \033[0;34m{event.name.pretty_str()}\033[0m",
            end="",
        )
        print(", " if event.params else "", end="")
        print(", ".join(map(lambda p: str(p.value), event.params)))


STDOUT_OUTPUT = StdoutOutput()


def dbg(msg, *args, **kwargs):
    return
    print("[dbg] ", end="")
    print(msg, *args, file=sys.stderr, **kwargs)


class Interpreter:
    def __init__(self, program, inp, args):
        self.program = program
        self.input = inp
        self.args = args
        self.output_traces = []

        self.state = State()

        self.handlers = {
            Let: self.Let,
            Yield: self.Yield,
            StatementList: self.StatementList,
            ForEach: self.ForEach,
            IfExpr: self.IfExpr,
            MethodCall: self.methodcall,
            Continue: self.Continue,
            Break: self.Break,
        }

        initialize_type_methods()

        self.executed_stmts = 0

        self.modules = {}
        self.execute_imports()

    def execute_imports(self):
        from importlib import import_module

        self_path = abspath(
            dirname(readlink(__file__) if islink(__file__) else __file__)
        )
        sys.path.insert(0, abspath(f"{self_path}/../modules"))
        for mpath in reversed(self.args.modules_dirs):
            sys.path.insert(0, abspath(mpath))

        for imp in self.program.imports:
            name = imp.module.name
            mod = import_module(f"modules.{name}")
            print(f"Imported: {mod}")
            m = Module(mod.METHODS)
            self.modules[name] = m
            self.bind_name(name, m)

    def get_method(self, name):
        method = None
        if isinstance(name.lhs, Expr):
            obj = name.lhs.type()
        else:
            obj = self.state.try_get(name.lhs.name)

        if obj:
            method = obj.get_method(name.rhs.name)
        return method

    def bind_name(self, name, val):
        self.state.bind(name, val)

    def methodcall(self, name):
        assert isinstance(name, MethodCall), name
        method = self.get_method(name)
        if method is None:
            raise RuntimeError(f"Unknown method: `{name.lhs.name}.{name.rhs.name}`")

        assert isinstance(method, Method), (name, method)
        if isinstance(name.lhs, Expr):
            # this method is a method of a type, we must pass the object as a parameter
            return method.execute(self.state, list(map(self.eval, [name.lhs] + name.params)))
        return method.execute(self.state, list(map(self.eval, name.params)))

    def eval(self, name):
        if isinstance(name, Identifier):
            val = self.state.try_get(name.name)
            if val is not None:
                return val
            raise NotImplementedError(f"Unbound identifier `{name}` : {type(name)}")

        if isinstance(name, Constant):
            return name

        if isinstance(name, MethodCall):
            return self.methodcall(name)

        if isinstance(name, IsIn):
            return self.eval_is_in(name)

        if isinstance(name, New):
            out = None  # name.
            return Trace(name.objtype, out=(out or STDOUT_OUTPUT))

        if isinstance(name, Event):
            # evaluate the parameters in the event
            return Event(name.name, [self.eval(p) for p in name.params or ()])

        if isinstance(name, CommandLineArgument):
            n = int(name.num) - 1
            if n >= len(self.input.args):
                raise RuntimeError(f"Asking for command line argument {n}, but there is no such argument: {self.input}")
            return Constant(self.input.args[n], STRING_TYPE)

        if isinstance(name, TupleExpr):
            return Tuple([self.eval(v) for v in name.values], name.type())

        raise NotImplementedError(f"Invalid parameter to eval: {name} : {type(name)}")

    def eval_is_in(self, expr):
        lhs = self.eval(expr.lhs)
        iterable = self.eval(expr.rhs)
        iterator = iterable.iterator()
        while iterator.has_next():
            c = iterator.next()
            if c == lhs:
                return Constant(True, BOOL_TYPE)
        return Constant(False, BOOL_TYPE)



    def StatementList(self, stmt):
        dbg("Handling StatementList")
        execute = self.exec
        for s in stmt:
            r = execute(s)
            if r is not None:
                assert self.in_iteration()
                return r

    def IfExpr(self, stmt):
        dbg("Handling IfExpr")
        execute = self.exec
        cond = self.eval(stmt.cond)
        assert cond.value in (True, False), cond
        if cond.value is True:
            stmts = stmt.true_stmts
        elif cond.value is False:
            stmts = stmt.false_stmts or []
        else:
            raise RuntimeError(f"Invalid evaluated condition: {cond}")

        self.state.enter_scope(stmt)
        for s in stmts:
            r = execute(s)
            if r is not None:
                assert self.in_iteration(), self.state
                self.state.leave_scope(stmt)
                return r
        self.state.leave_scope(stmt)

    def ForEach(self, stmt):
        execute = self.exec
        # has next element?
        rhs = self.eval(stmt.iterable)
        if isinstance(rhs, Trace):
            iterable = ListIterator(rhs.events)
        elif isinstance(rhs, Iterator):
            iterable = rhs
        else:
            raise NotImplementedError(rhs)
        assert isinstance(iterable, (Iterable, Iterator)), iterable
        iterator = iterable.iterator()

        self.state.enter_scope(stmt)
        while iterator.has_next():
            self.bind_name(stmt.value.name, iterator.next())

            # if so, evaluate statements
            for s in stmt:
                action = execute(s)

                if action == CONTINUE:
                    break
                elif action == BREAK:
                    self.state.leave_scope(stmt)
                    return
                assert action is None, action
        self.state.leave_scope(stmt)

    def Continue(self, _):
        assert self.in_iteration(), self.state.scopes()
        return CONTINUE

    def Break(self, _):
        assert self.in_iteration(), self.state.scopes()
        return BREAK

    def in_iteration(self):
        return any((isinstance(s, ForEach) for s in self.state.scopes()))

    def Let(self, stmt):
        obj = self.eval(stmt.obj)
        self.bind_name(stmt.name.name, obj)
        dbg(f"Let {stmt.name.name} = {obj}")

    def Yield(self, stmt):
        # stmt.events
        seval = self.eval
        trace = seval(stmt.trace)
        assert isinstance(trace, Trace), trace

        for ev in stmt.events:
            event = seval(ev)
            # print(f"[{trace.id()}]: {trace.size()}: "
            #        "\033[0;34m{event.name.pretty_str()}\033[0m", end="")
            # print(", " if event.params else "", end="")
            # print(", ".join(map(lambda p: p.value, event.params)))
            trace.push(event)

    def run(self):
        print("[Interpreter] running on program")
        execute = self.exec
        for stmt in self.program:
            r = execute(stmt)
            if r is not None:
                assert self.in_iteration(), self.state
                return r

    def exec(self, stmt):
        dbg(f"{self.executed_stmts}: {stmt}")
        self.executed_stmts += 1
        return self.handlers[type(stmt)](stmt)
