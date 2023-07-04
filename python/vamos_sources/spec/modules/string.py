from vamos_common.spec.ir.constant import Constant
from vamos_common.types.type import STRING_TYPE


def concat(_, params):
    assert isinstance(params[0], Constant)
    assert params[0]._type == STRING_TYPE
    assert isinstance(params[1], Constant)
    assert params[1]._type == STRING_TYPE
    return Constant(params[0].value + params[1].value, STRING_TYPE)


METHODS = {"concat": concat}
