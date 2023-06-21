from parser.element import Element
from parser.types.type import SimpleType, BoolType


class Decl(Element):
    pass


class DataField(Element):
    def __init__(self, name, ty):
        super().__init__(ty)
        self.name = name

    @property
    def children(self):
        return ()

    def __str__(self):
        return f"{self.name} : {self.type}"

    def __repr__(self):
        return f"DataField({self.name} : {self.type})"


class EventDecl(Decl):
    def __init__(self, name, fields):
        super().__init__()
        self.name = name
        assert isinstance(fields, list), fields
        assert all(map(lambda f: isinstance(f, DataField), fields)), fields
        self.fields = fields

    @property
    def children(self):
        return self.fields

    def __str__(self):
        return f"Event {self.name} {{{','.join(map(str, self.fields))}}}"

    def __repr__(self):
        return f"EventDecl({self.name} {{{','.join(map(str, self.fields))}}})"


class TraceDecl(Decl):
    def __init__(self, name, ty):
        super().__init__()
        self.name = name
        self.type = ty

    def has_simple_type(self):
        """
        An output trace can have also non-trace type, e.g., Bool or Int32
        which means that the output is a single boolean (number, resp.),
        usually representing the output of monitoring or another computation.
        :return: True if the type of the trace is not trace type, False otherwise
        """
        return isinstance(self.type, SimpleType)

    def has_simple_bool_type(self):
        """
        :see: has_simple_type() for more generic function
        :return: True if the trace has non-trace simple Bool type, False otherwise.
        """
        return isinstance(self.type, BoolType)

    @property
    def children(self):
        return [self.name, self.type]

    def __repr__(self):
        return f"TraceDecl({self.name} : {self.type})"
