from interpreter.method import Method
from vamos_common.spec.ir.constant import Constant
from vamos_common.types.type import (
    StringType,
    type_from_token,
    NumType,
    STRING_TYPE,
    Type,
)


def string_as(_, params):
    assert len(params) == 2, params
    obj = params[0]
    ty = params[1]
    assert isinstance(obj.value, str), obj.value
    assert isinstance(ty.value, str), obj.value

    ty = type_from_token(ty.value)
    assert ty is not None
    if isinstance(ty, NumType):
        # FIXME: check that the bitwidth matches
        return Constant(int(obj.value), ty)
    raise NotImplementedError(f"Cast {obj} to {ty}")


def initialize_type_methods():
    StringType.methods["as"] = Method("String.as", [STRING_TYPE], Type(), string_as)
