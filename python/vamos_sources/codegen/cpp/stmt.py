from vamos_common.types.type import SimpleType

from .base import CodeGenBase
from ...spec.ir.ir import Let, ForEach, Yield, Continue, Break


class CodeGenStmt(CodeGenBase):
    """
    Generate C++ code for statements
    """

    def __init__(self, args, ctx):
        super().__init__(args, ctx)

    def _gen_stmt(self, stmt, wr, wr_h):
        if isinstance(stmt, Let):
            self._gen_let(stmt, wr, wr_h)
        elif isinstance(stmt, ForEach):
            self._gen_foreach(stmt, wr, wr_h)
        elif isinstance(stmt, Yield):
            self._gen_yield(stmt, wr, wr_h)
        elif isinstance(stmt, Continue):
            wr("continue;\n")
        elif isinstance(stmt, Break):
            wr("break;\n")
        else:
            raise NotImplementedError(f"Codegen not implemented for statement {stmt}")

    def _gen_let(self, stmt, wr, wr_h):
        wr(f"auto {stmt.name.name} = ")
        # self._gen(stmt.name, wr)
        # wr(" = ")
        self.gen(stmt.obj, wr, wr_h)
        wr(";\n")

    def _gen_foreach(self, stmt, wr, wr_h):
        if isinstance(self.get_type(stmt.value), SimpleType):
            wr(f"for (/*{self.get_type(stmt.value)}*/ auto {stmt.value.name} : ")
        else:
            wr(f"for (/*{self.get_type(stmt.value)}*/ const auto& {stmt.value.name} : ")
        self.gen(stmt.iterable, wr, wr_h)
        wr(") {\n")
        for s in stmt.stmts:
            self.gen(s, wr, wr_h)
        wr("}\n")

    def _gen_yield(self, stmt, wr, wr_h):
        wr("{\n")
        for ev in stmt.events:
            # trace = self._get_trace(stmt.trace)
            wr(f"{stmt.trace.name}->push(")
            self.gen(ev, wr, wr_h)
            wr(");\n")
        wr("}\n")
