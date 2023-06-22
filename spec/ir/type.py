class Type:
    @property
    def children(self):
        raise NotImplementedError(f"Must be overriden for {self}")


class UserType(Type):
    def __init__(self, name):
        self.name = name

    def __str__(self):
        return f"uTYPE({self.name})"

    @property
    def children(self):
        return ()

class EventType(UserType):
    def __init__(self, name):
        super().__init__(name)

    def __str__(self):
        return f"EventTy({self.name})"

    def __repr__(self):
        return f"EventType({self.name})"


class SimpleType(Type):
    pass

    @property
    def children(self):
        return ()


class BoolType(SimpleType):
    def __str__(self):
        return "Bool"


class NumType(SimpleType):
    def __init__(self, bitwidth=None):
        assert bitwidth is None or bitwidth in (8, 16, 32, 64), bitwidth
        self.bitwidth = bitwidth

    def __str__(self):
        if self.bitwidth is None:
            return "Num"
        return f"Num{self.bitwidth}"


class IntType(NumType):
    def __init__(self, bitwidth):
        super().__init__(bitwidth)
        self.signed = True

    def __str__(self):
        return f"Int{self.bitwidth}"


class UIntType(NumType):
    def __init__(self, bitwidth):
        super().__init__(bitwidth)
        self.signed = False

    def __str__(self):
        return f"UInt{self.bitwidth}"


class IterableType(Type):
    pass


class TraceType(IterableType):
    def __init__(self, subtypes):
        self.subtypes = subtypes

    def __str__(self):
        return f"Tr:{','.join(map(str, self.subtypes))}"

    @property
    def children(self):
        return self.subtypes


class HypertraceType(IterableType):
    def __init__(self, subtypes, bounded=True):
        assert all(
            map(lambda ty: isinstance(ty, (TraceType, UserType)), subtypes)
        ), subtypes
        self.subtypes = subtypes
        self.bounded = bounded

    def __str__(self):
        return f"Ht:{{{','.join(map(str, self.subtypes))} {'...' if not self.bounded else ''}}}"

    @property
    def children(self):
        return self.subtypes


def type_from_token(token):
    if token == "Bool":
        return BoolType()

    if token.startswith("Int"):
        if token == "Int64":
            return IntType(64)
        if token == "Int32":
            return IntType(32)
        if token == "Int8":
            return IntType(8)
        if token == "Int16":
            return IntType(16)

    if token.startswith("UInt"):
        if token == "UInt64":
            return UIntType(64)
        if token == "UInt32":
            return UIntType(32)
        if token == "UInt8":
            return UIntType(8)
        if token == "UInt16":
            return UIntType(16)

    raise NotImplementedError(f"Unknown type: {token}")


class TupleType(IterableType):
    pass


class StringType(IterableType):
    def __repr__(self):
        return "StringTy"
