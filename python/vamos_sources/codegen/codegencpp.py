from os import readlink
from os.path import abspath, dirname, islink, join as pathjoin, basename

from vamos_common.codegen.codegen import CodeGen
from vamos_common.codegen.lang.cpp import cpp_type
from vamos_common.spec.ir.constant import Constant
from vamos_common.spec.ir.expr import Expr
from vamos_common.spec.ir.identifier import Identifier
from vamos_common.types.type import (
    IntType,
    STRING_TYPE,
    TraceType,
    SimpleType,
    UserType,
    NumType,
)
from ..spec.ir.expr import (
    MethodCall,
    IfExpr,
    New,
    IsIn,
    Cast,
    CommandLineArgument,
    Event,
    BinaryOp,
)
from ..spec.ir.ir import Program, Statement, Let, ForEach, Yield, Continue, Break


class CodeGenCpp(CodeGen):
    def __init__(self, args, ctx):
        super().__init__(args, ctx)

        self_path = abspath(
            dirname(readlink(__file__) if islink(__file__) else __file__)
        )
        self.templates_path = pathjoin(self_path, "templates/cpp")
        self._gen_includes = set()
        self._std_includes = set()
        self._cmake_defs = []
        self._copy_files = set()
        # a set of features used by the generated code
        self._features = set()

    def get_type(self, elem):
        return self.ctx.types.get(elem)

    def add_cmake_def(self, definition):
        self._cmake_defs.append(definition)

    def add_include(self, name):
        self._gen_includes.add(name)

    def add_std_include(self, name):
        self._std_includes.add(name)

    def add_copy_file(self, name):
        self._copy_files.add(name)

    def _copy_common_files(self):
        files = [
            "main.cpp",
            "trace.h",
            "new_trace.h",
            "stdout_trace.h",
        ]
        for f in files:
            if f not in self.args.overwrite_default:
                self.copy_file(f)

        vamos_common_files = ["cpp/event_and_id.h"]
        for f in vamos_common_files:
            if f not in self.args.overwrite_default:
                self.copy_common_file(f)

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
        if isinstance(self.get_type(stmt.value), SimpleType):
            wr(f"for (/*{self.get_type(stmt.value)}*/ auto {stmt.value.name} : ")
        else:
            wr(f"for (/*{self.get_type(stmt.value)}*/ const auto& {stmt.value.name} : ")
        self.gen(stmt.iterable, wr, wr_h)
        wr(") {\n")
        for s in stmt.stmts:
            self.gen(s, wr, wr_h)
        wr("}\n")

    def _get_trace(self, tr):
        assert False, tr

    def _gen_yield(self, stmt, wr, wr_h):
        wr("{\n")
        for ev in stmt.events:
            # trace = self._get_trace(stmt.trace)
            wr(f"{stmt.trace.name}->push(")
            self.gen(ev, wr, wr_h)
            wr(");\n")
        wr("}\n")

    def _gen_new(self, stmt, wr):
        ty = stmt.objtype
        if isinstance(ty, TraceType):
            trace_ty = self.ctx.add_tracetype(ty, stmt.outputs).name
            wr(f"static_cast<{trace_ty}*>(__new_trace<{trace_ty}>())")
        elif isinstance(ty, TraceType):
            htrace_ty = self.ctx.add_hypertracetype(ty)
            wr(f"static_cast<{htrace_ty}*>(__new_hyper_trace<{htrace_ty}>())")
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

        val_ty = stmt.value.type()
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
        elif isinstance(val_ty, NumType):
            fun = f"reinterpret_cast<{cpp_type(ty)}>"

        if fun is None:
            raise NotImplementedError(f"Unhandled cast: {stmt}")

        wr(fun)
        wr("(")
        self.gen(stmt.value, wr, wr_h)
        wr(")")

    def _gen_cmdarg(self, stmt, wr, wr_h):
        assert int(stmt.num[1:]), stmt
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
        elif isinstance(stmt, BinaryOp):
            self._gen_bin_op(stmt, wr, wr_h)
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
        wr('#include "stdout_trace.h"\n')
        wr('#include "traces.h"\n')
        wr('#include "events.h"\n\n')
        wr('#include "src.h"\n\n')

        wr("void source_thrd(void *data) {\n")
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

    def _generate_cmake(self):
        from config import vamos_buffers_DIR  # , vamos_hyper_DIR

        build_type = self.args.build_type
        if not build_type:
            build_type = '"Debug"' if self.args.debug else ""

        self.gen_config(
            "CMakeLists.txt.in",
            "CMakeLists.txt",
            {
                "@vamos-buffers_DIR@": vamos_buffers_DIR,
                # "@vamos-hyper_DIR@": vamos_hyper_DIR,
                "@additional_sources@": " ".join(
                    (basename(f) for f in self.args.cpp_files + self.args.add_gen_files)
                ),
                "@additional_cmake_definitions@": " ".join(
                    (d for d in self.args.cmake_defs + self._cmake_defs)
                ),
                "@CMAKE_BUILD_TYPE@": build_type,
            },
        )

    def generate(self, ast):
        if self.args.debug:
            with self.new_dbg_file(f"src.ast") as fl:
                fl.write(str(ast))

        self._copy_common_files()
        print(ast)

        assert isinstance(ast, Program), ast

        with self.new_file("src.cpp") as f:
            with self.new_file("src.h") as fh:
                fh.write('#include "src-includes.h"\n\n')
                fh.write('#include "events.h"\n\n')

                fh.write("void source_thrd(void *data);\n\n")

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

        self._generate_cmake()

        self.try_clang_format_file("inputs.cpp")
        self.try_clang_format_file("src.cpp")
        self.try_clang_format_file("src.h")
