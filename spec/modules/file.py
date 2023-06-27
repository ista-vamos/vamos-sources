from interpreter.value import Value, LazyIterator, Trace
from ir.expr import Constant
from ir.ir import Event
from ir.type import IterableType, StringType, OutputType, STRING_TYPE


class FileReader(Value):
    def __init__(self, fl: Constant):
        super().__init__("<FileReader>", IterableType())
        assert isinstance(fl, Constant), fl
        assert fl.type == STRING_TYPE, fl

        self.file = fl.value
        self.fobj = open(self.file, "r")

    def __del__(self):
        self.fobj.close()
        del self

    def get_method(self, name):
        if name == "lines":
            return lambda state, params: self.lines()

        raise RuntimeError(f"Invalid method: {name}")

    def lines(self):
        return LazyIterator(self.fobj, StringType)


class FileWriter(Value):
    def __init__(self, fl: Constant):
        assert isinstance(fl, Constant), fl
        assert fl.type == STRING_TYPE, fl

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


METHODS = {"reader": reader, "writer": writer}
