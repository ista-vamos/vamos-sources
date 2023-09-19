from vamos_common.codegen.lang.cpp import cpp_type
from vamos_common.spec.ir.constant import Constant
from vamos_common.types.type import (
    TraceType,
    UserType,
    SimpleType,
    IntType,
    UIntType,
    STRING_TYPE,
    NumType,
    StringType,
    HypertraceType,
)

from .stmt import CodeGenStmt
from ...spec.ir.expr import MethodCall, New, IfExpr

from vamos_common.spec.ir.expr import (
    IsIn,
    Cast,
    CommandLineArgument,
    BinaryOp,
    Event,
)


class CodeGenExpr(CodeGenStmt):
    """
    Generate C++ code for statements
    """

    def __init__(self, args, ctx):
        super().__init__(args, ctx)

    def _gen_expr(self, stmt, wr, wr_h):
        if isinstance(stmt, MethodCall):
            self._gen_method_call(stmt, wr, wr_h)
        elif isinstance(stmt, New):
            self._gen_new(stmt, wr)
        elif isinstance(stmt, IsIn):
            self._gen_is_in(stmt, wr, wr_h)
        elif isinstance(stmt, IfExpr):
            self._gen_if(stmt, wr, wr_h)
        elif isinstance(stmt, Constant):
            self._gen_constant(stmt, wr)
        elif isinstance(stmt, Cast):
            self._gen_cast(stmt, wr, wr_h)
        elif isinstance(stmt, Event):
            self._gen_event(stmt, wr, wr_h)
        elif isinstance(stmt, CommandLineArgument):
            self._gen_cmdarg(stmt, wr, wr_h)
        elif isinstance(stmt, BinaryOp):
            self._gen_bin_op(stmt, wr, wr_h)
        else:
            raise NotImplementedError(f"Codegen not implemented for expr {stmt}")

    def _gen_constant(self, stmt, wr):
        if isinstance(stmt.type(), NumType):
            wr(str(stmt.value))
        elif isinstance(stmt.type(), StringType):
            wr(f'"{stmt.value}"')

    def _gen_new(self, stmt, wr):
        ty = stmt.objtype
        if isinstance(ty, TraceType):
            trace_ty = self.ctx.add_tracetype(ty, stmt.outputs).name
            wr(f"static_cast<{trace_ty}*>(__new_trace<{trace_ty}>())")
        elif isinstance(ty, HypertraceType):
            htrace_ty = self.ctx.add_hypertracetype(ty, stmt.outputs).name
            wr(f"static_cast<{htrace_ty}*>(__new_hyper_trace<{htrace_ty}>())")
        elif isinstance(ty, (UserType, SimpleType)):
            wr(f"__new_output_var(sizeof({cpp_type(ty)}))")
        else:
            raise NotImplementedError(f"Unknown type in `new`: {stmt}")

    def _gen_is_in(self, stmt, wr, wr_h):
        wr("__is_in(")
        self.gen(stmt.lhs, wr, wr_h)
        wr(", ")
        self.gen(stmt.rhs, wr, wr_h)
        wr(")")

        self._features.add("is_in")

    def _gen_method_call(self, stmt, wr, wr_h):
        mod = self.ctx.get_module(stmt.lhs)
        if mod:
            mod.gen("cpp", self, stmt, wr, wr_h)
        else:
            wr(f"{stmt.lhs.name}.{stmt.rhs.name}(")
            for n, p in enumerate(stmt.params):
                if n > 0:
                    wr(", ")
                self.gen(p, wr, wr_h)
            wr(")")
            # raise NotImplementedError(f"Unhandled method call: {stmt}")

    def _gen_if(self, stmt, wr, wr_h):
        wr(f"if (")
        self.gen(stmt.cond, wr, wr_h)
        wr(") {\n")
        for s in stmt.true_stmts:
            self.gen(s, wr, wr_h)
        wr("}")

        if not stmt.false_stmts:
            wr("\n")
            return

        wr(" else {\n")
        for s in stmt.false_stmts:
            self.gen(s, wr, wr_h)
        wr("}\n")

    def _gen_cast(self, stmt, wr, wr_h):
        assert stmt.type(), (stmt, stmt.type())

        val_ty = self.get_type(stmt.value)
        ty = stmt.type()
        fun = None
        if val_ty is None:
            # this must be a type annotation, just "assert" the type
            # with static cast
            fun = f"static_cast<{cpp_type(ty)}>"
        if val_ty == STRING_TYPE:
            if isinstance(ty, IntType):
                if ty.bitwidth <= 32:
                    self.add_std_include("string")
                    fun = "std::stoi"
            if isinstance(ty, UIntType):
                if ty.bitwidth <= 64:
                    self.add_std_include("string")
                    fun = "std::stoul"
        elif isinstance(val_ty, NumType):
            fun = f"reinterpret_cast<{cpp_type(ty)}>"

        if fun is None:
            raise NotImplementedError(f"Unhandled cast: {val_ty} as {ty}")

        wr(fun)
        wr("(")
        self.gen(stmt.value, wr, wr_h)
        wr(")")

    def _gen_cmdarg(self, stmt, wr, wr_h):
        assert int(stmt.num[1:]), stmt
        self.add_include("command_line_arg.h")
        self.add_copy_vamos_common_file("cpp/command_line_arg.h")
        self.add_copy_vamos_common_file("cpp/command_line_arg.cpp")
        self.add_cmake_source("command_line_arg.cpp")
        wr(f"__command_line_arg(data, {stmt.num[1:]})")

    def _gen_bin_op(self, stmt, wr, wr_h):
        if isinstance(self.get_type(stmt), NumType):
            wr("(")
            self.gen(stmt.lhs, wr, wr_h)
            wr(stmt.op)
            self.gen(stmt.rhs, wr, wr_h)
            wr(")")
        else:
            raise NotImplementedError(f"Codegen not implemented for binary op {stmt}")

    def _gen_event(self, stmt, wr, wr_h):
        wr(f"Event_{stmt.name}(")
        for n, p in enumerate(stmt.params):
            if n > 0:
                wr(", ")
            self.gen(p, wr, wr_h)
        wr(")")
