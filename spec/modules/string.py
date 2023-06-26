from ir.expr import Constant
from ir.type import STRING_TYPE


def concat(state, params):
    assert isinstance(params[0], Constant)
    assert params[0].type == STRING_TYPE
    assert isinstance(params[1], Constant)
    assert params[1].type == STRING_TYPE
    return Constant(params[0].value + params[1].value, STRING_TYPE)

METHODS = {
    'concat': concat
}
