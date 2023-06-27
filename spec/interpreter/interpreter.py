import sys
from os import readlink
from os.path import abspath, dirname, islink

from interpreter.module import Module
from interpreter.value import Iterable, Value, Trace, ListIterator, Iterator
from ir.element import Identifier
from ir.expr import MethodCall, New, Constant, IfExpr, CommandLineArgument
from ir.ir import Let, Yield, StatementList, ForEach, Event, OutputDecl
from ir.type import OutputType


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
        print(", ".join(map(lambda p: p.value, event.params)))


STDOUT_OUTPUT = StdoutOutput()


def dbg(msg, *args, **kwargs):
    return
    print("[dbg] ", end="")
    print(msg, *args, file=sys.stderr, **kwargs)


class State:
    def __init__(self):
        self.values = {}
        self.next_instr = {}

    def bind(self, name, val):
        assert isinstance(name, str), (name, type(name))
        assert isinstance(val, (Value, Constant)), val
        self.values[name] = val

    def get(self, name):
        return self.values[name]

    def try_get(self, name):
        return self.values.get(name)

    def __repr__(self):
        return f"""
-- values --
{self.values}
"""


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
            OutputDecl: self.OutputDecl,
            MethodCall: self.methodcall,
        }

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
        return method(self.state, list(map(self.eval, name.params)))

    def eval(self, name):
        if isinstance(name, Identifier):
            val = self.state.try_get(name.name)
            if val is not None:
                return val

        if isinstance(name, Constant):
            return name

        if isinstance(name, MethodCall):
            return self.methodcall(name)

        if isinstance(name, New):
            out = None  # name.
            return Trace(name.objtype, out=(out or STDOUT_OUTPUT))

        if isinstance(name, Event):
            # evaluate the parameters in the event
            return Event(name.name, [self.eval(p) for p in name.params or ()])

        if isinstance(name, CommandLineArgument):
            n = int(name.num) - 1
            assert n < len(self.input.args), self.input
            return self.input.args[n]

        raise NotImplementedError(f"Invalid name: {name}")

    def OutputDecl(self, stmt):
        raise NotImplementedError("Not implemented yet")
        trace = self.eval(stmt.trace)
        out = self.eval(stmt.out)
        print(trace, out)

    def StatementList(self, stmt):
        dbg("Handling StatementList")
        execute = self.exec
        for s in stmt:
            execute(s)

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

        for s in stmts:
            execute(s)

    def ForEach(self, stmt):
        dbg("Handling ForEach")
        execute = self.exec
        # has next element?
        rhs = self.eval(stmt.iterable)
        if isinstance(rhs, Trace):
            iterable = ListIterator(rhs.events, rhs.type())
        elif isinstance(rhs, Iterator):
            iterable = rhs
        else:
            raise NotImplementedError(rhs)
        assert isinstance(iterable, Iterable)
        while iterable.has_next():
            self.bind_name(stmt.value.name, iterable.next())

            # if so, evaluate statements
            for s in stmt:
                execute(s)

    def Let(self, stmt):
        obj = self.eval(stmt.obj)
        self.bind_name(stmt.name.name, obj)
        dbg(f"Let {stmt.name.name} = {obj}")

    def Yield(self, stmt):
        dbg("Handling Yield")
        # stmt.events
        seval = self.eval
        trace = seval(stmt.trace)
        assert isinstance(trace, Trace), trace

        for ev in stmt.events:
            event = seval(ev)
            # print(f"[{trace.id()}]: {trace.size()}: \033[0;34m{event.name.pretty_str()}\033[0m", end="")
            # print(", " if event.params else "", end="")
            # print(", ".join(map(lambda p: p.value, event.params)))
            trace.push(event)

    def run(self):
        print(f"[Interpreter] running on program")
        execute = self.exec
        for stmt in self.program:
            execute(stmt)

    def exec(self, stmt):
        dbg(f"{self.executed_stmts}: {stmt}")
        self.handlers[type(stmt)](stmt)
        self.executed_stmts += 1
