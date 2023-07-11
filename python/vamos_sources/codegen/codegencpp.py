import sys
from os import readlink
from os.path import abspath, dirname, islink, join as pathjoin

from vamos_common.codegen.codegen import CodeGen
from vamos_common.codegen.lang.cpp import cpp_type
from vamos_common.spec.ir.constant import Constant
from vamos_common.spec.ir.expr import Expr
from vamos_common.spec.ir.identifier import Identifier
from vamos_common.types.type import (
    Type,
    BoolType,
    IntType,
    UIntType,
    NumType,
    STRING_TYPE,
    TraceType,
    SimpleType,
    UserType,
    EventType,
)

from ..spec.ir.expr import MethodCall, IfExpr, New, IsIn, Cast, CommandLineArgument
from ..spec.ir.ir import Program, Statement, Let, ForEach, Yield, Event, Continue, Break


class CodeGenCpp(CodeGen):
    def __init__(self, args, ctx):
        super().__init__(args, ctx)

        self_path = abspath(
            dirname(readlink(__file__) if islink(__file__) else __file__)
        )
        self.templates_path = pathjoin(self_path, "templates/cpp")
        self._gen_includes = set()
        self._std_includes = set()
        self._copy_files = set()
        self._modules = {}
        # a set of features used by the generated code
        self._features = set()

    def add_include(self, name):
        self._gen_includes.add(name)

    def add_std_include(self, name):
        self._std_includes.add(name)

    def add_copy_file(self, name):
        self._copy_files.add(name)

    def _handle_imports(self, imports):
        from importlib import import_module

        for imp in imports:
            name = imp.module.name
            print(f"Importing: {name}", end=" ")
            mod = import_module(f"vamos_sources.spec.modules.{name}")
            print(f"-> {mod}")
            assert name not in self._modules, name
            self._modules[name] = mod

        sys.path.pop(0)

    def _copy_common_files(self):
        files = ["new_trace.h"]
        for f in files:
            if f not in self.args.overwrite_default:
                self.copy_file(f)

    def copy_files(self):
        for f in self._copy_files:
            self.copy_file(f)

    def _gen_input_stream_class(self, ast, wr):
        wr("class InputStream {\n")
        wr("  bool hasEvent() const {\n")
        wr("    abort(); \n")
        wr("  }\n")
        wr("};\n\n")

    def _gen_inputs_class(self, ast, wr):
        wr("class Inputs {\n")
        wr("};\n\n")

    def _gen_let(self, stmt, wr, wr_h):
        wr(f"auto {stmt.name.name} = ")
        # self._gen(stmt.name, wr)
        # wr(" = ")
        self.gen(stmt.obj, wr, wr_h)
        wr(";\n")

    def _gen_foreach(self, stmt, wr, wr_h):
        wr("/* FIXME: do not use reference on simple types */\n")
        wr(f"for (auto& {stmt.value.name} : ")
        self.gen(stmt.iterable, wr, wr_h)
        wr(") {\n")
        for s in stmt.stmts:
            self.gen(s, wr, wr_h)
        wr("}\n")

    def _gen_yield(self, stmt, wr, wr_h):
        wr("{\n")
        for ev in stmt.events:
            wr(f"{stmt.trace.name}->push(")
            self.gen(ev, wr, wr_h)
            wr(");\n")
        wr("}\n")

    def _gen_new(self, stmt, wr):
        ty = stmt.objtype
        if isinstance(ty, TraceType):
            wr(f"__new_trace<Event_{self.ctx.add_tracetype(ty)}>()")
        elif isinstance(ty, (UserType, SimpleType)):
            wr(f"__new_output_var(sizeof({cpp_type(ty)}))")
        else:
            raise NotImplementedError(f"Unknown type in `new`: {stmt}")

    def _gen_is_in(self, stmt, wr, wr_h):
        wr(f"__is_in(")
        self.gen(stmt.lhs, wr, wr_h)
        wr(", ")
        self.gen(stmt.rhs, wr, wr_h)
        wr(")")

        self._features.add("is_in")

    def _gen_method_call(self, stmt, wr, wr_h):
        mod = self._modules.get(stmt.lhs.name)
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
        assert stmt.value.type(), stmt
        assert stmt.type(), stmt

        fun = None
        if stmt.value.type() == STRING_TYPE:
            ty = stmt.type()
            if isinstance(ty, IntType):
                if ty.bitwidth <= 32:
                    self.add_std_include("string")
                    fun = "std::stoi"
        if fun is None:
            raise NotImplementedError(f"Unhandled cast: {stmt}")

        wr(fun)
        wr("(")
        self.gen(stmt.value, wr, wr_h)
        wr(")")

    def _gen_cmdarg(self, stmt, wr, wr_h):
        assert int(stmt.num[1:]), stmt
        wr(f"__command_line_arg(data, {stmt.num[1:]})")

    def _gen_event(self, stmt, wr, wr_h):
        wr(f"Event_{stmt.name}(")
        for n, p in enumerate(stmt.params):
            if n > 0:
                wr(", ")
            self.gen(p, wr, wr_h)
        wr(")")

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
            wr(str(stmt.value))
        elif isinstance(stmt, Cast):
            self._gen_cast(stmt, wr, wr_h)
        elif isinstance(stmt, CommandLineArgument):
            self._gen_cmdarg(stmt, wr, wr_h)
        else:
            raise NotImplementedError(f"Codegen not implemented for expr {stmt}")

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

    def gen(self, stmt, wr, wr_h):
        if isinstance(stmt, Identifier):
            wr(stmt.name)
        elif isinstance(stmt, Event):
            self._gen_event(stmt, wr, wr_h)
        elif isinstance(stmt, Expr):
            self._gen_expr(stmt, wr, wr_h)
        elif isinstance(stmt, Statement):
            self._gen_stmt(stmt, wr, wr_h)
        else:
            raise NotImplementedError(
                f"Codegen not implemented for {stmt} : {type(stmt)}"
            )

    def _gen_src(self, ast, wr, wr_h):
        """
        The main code generated for the source. It will run in its own thread,
        and it will generate data into shared structures read by another thread
        that feeds data into monitor or some other channel
        """

        def wr_ind(msg):
            wr("  ")
            wr(msg)

        wr('#include "new_trace.h"\n')
        wr('#include "traces.h"\n')
        wr('#include "events.h"\n\n')
        wr('#include "src.h"\n\n')

        wr("int source_thrd(void *data) {\n")
        for stmt in ast.children:
            self.gen(stmt, wr_ind, wr_h)
        wr("}\n\n")

    def _src_finish(self, ast, f, fh):
        """
        Finish generating src.cpp and src.h by adding
        definitions for features that the code uses
        """

        if "is_in" in self._features:
            self.input_file(fh, "partials/src-is_in.h")

    def generate(self, ast):
        if self.args.debug:
            with self.new_dbg_file(f"src.ast") as fl:
                fl.write(str(ast))

        self._copy_common_files()
        print(ast)

        assert isinstance(ast, Program), ast

        self._handle_imports(ast.imports)

        with self.new_file("src.cpp") as f:
            with self.new_file("src.h") as fh:
                fh.write('#include "src-includes.h"\n\n')
                fh.write('#include "events.h"\n\n')

                self._gen_src(ast, f.write, fh.write)
                self._src_finish(ast, f, fh)

        with self.new_file("src-includes.h") as f:
            write = f.write
            for inc in self._std_includes:
                write(f"#include <{inc}>\n")
            for inc in self._gen_includes:
                write(f'#include "{inc}"\n')

        self.copy_files()

        with self.new_file("inputs.cpp") as f:
            wr = f.write

            wr('#include "src.h"\n\n')
            self._gen_input_stream_class(ast, wr)
            self._gen_inputs_class(ast, wr)

        self.try_clang_format_file("inputs.cpp")
        self.try_clang_format_file("src.cpp")
        self.try_clang_format_file("src.h")
