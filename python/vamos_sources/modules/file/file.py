from os.path import abspath, dirname, join as pathjoin

from vamos_common.spec.ir.constant import Constant
from vamos_common.types.methods import MethodHeader
from vamos_common.types.type import (
    IterableType,
    IteratorType,
    StringType,
    OutputType,
    STRING_TYPE,
    ObjectType,
)
from vamos_sources.interpreter.iterators import FiniteIterator
from vamos_sources.interpreter.method import Method
from vamos_sources.interpreter.value import Value, Trace
from vamos_sources.spec.ir.expr import Event
from vamos_sources.spec.ir.expr import MethodCall

header_lines = MethodHeader("lines", [], IteratorType(STRING_TYPE))


class FileReader(Value):
    def __init__(self, fl: Constant):
        super().__init__("<FileReader>", IterableType())
        assert isinstance(fl, Constant), fl
        assert fl.type() == STRING_TYPE, fl

        self.file = fl.value
        self.fobj = open(self.file, "r")

    def __del__(self):
        self.fobj.close()
        del self

    def get_method(self, name):
        if name == "lines":
            return Method(header_lines, lambda state, params: self.lines())

        raise RuntimeError(f"Invalid method: {name}")

    def lines(self):
        return FiniteIterator(self.fobj, StringType)


class FileWriter(Value):
    def __init__(self, fl: Constant):
        assert isinstance(fl, Constant), fl
        assert fl.type() == STRING_TYPE, fl

        super().__init__("<FileWriter>", OutputType())
        self.file = fl.value
        self.fobj = open(self.file, "w")

    def __del__(self):
        self.fobj.close()
        del self

    def push(self, trace, event):
        assert isinstance(trace, Trace), trace
        assert isinstance(event, Event), event

        print(
            f"[{trace.id()}]: {trace.size()}: {event.name.pretty_str()}",
            end="",
            file=self.fobj,
        )
        print(", " if event.params else "", end="", file=self.fobj)
        print(", ".join(map(lambda p: p.value, event.params)), file=self.fobj)

    def get_method(self, name):
        if name == "push":
            return lambda _, params: self.push(params[0], params[1])
        return None


def reader(_, params):
    assert len(params) == 1, params
    return FileReader(params[0])


def writer(_, params):
    assert len(params) == 1, params
    return FileWriter(params[0])


class FileReaderTy(ObjectType):
    methods = {"lines": header_lines}


METHODS = {
    "reader": Method(
        MethodHeader("file.reader", [STRING_TYPE], FileReaderTy()), reader
    ),
    "writer": Method(MethodHeader("file.writer", [STRING_TYPE], ObjectType()), writer),
}


def gen(lang, codegen, stmt, wr, wr_h):
    assert lang == "cpp"
    gen_cpp(codegen, stmt, wr, wr_h)


def gen_cpp(codegen, stmt, wr, wr_h):
    if isinstance(stmt, MethodCall):
        if stmt.rhs.name == "reader":
            codegen.add_include("file-reader.h")
            codegen.add_copy_file(
                abspath(pathjoin(dirname(__file__), "codegen/cpp/file-reader.h"))
            )
            wr("FileReader(")
            for n, p in enumerate(stmt.params):
                if n > 0:
                    wr(", ")
                codegen.gen(p, wr, wr_h)
            wr(")")
        else:
            raise NotImplementedError(f"Unknown method: {stmt.rhs.name}")
    else:
        raise NotImplementedError(f"Unknown stmt: {stmt}")
