import sys
from os import readlink
from os.path import abspath, dirname, islink, join as pathjoin

from vamos_common.codegen.codegen import CodeGen
from vamos_common.spec.ir.expr import Expr
from vamos_common.spec.ir.identifier import Identifier
from vamos_common.types.type import Type, BoolType, IntType, UIntType

from ..spec.ir.expr import MethodCall, IfExpr, New, IsIn
from ..spec.ir.ir import Program, Statement, Let, ForEach, Yield, Event, Continue, Break


class CodeMapper:
    def append_mstring(self, M, pos_s, pos_e):
        return f"{M}.append(MString::Letter({pos_s}, {pos_e}))"

    def c_type(self, ty: Type):
        assert isinstance(ty, Type), ty
        if isinstance(ty, BoolType):
            return f"bool"
        if isinstance(ty, IntType):
            return f"int{ty.bitwidth}_t"
        if isinstance(ty, UIntType):
            return f"uint{ty.bitwidth}_t"
        raise NotImplementedError(f"Unknown type: {ty}")


class CodeGenCpp(CodeGen):
    def __init__(self, args, ctx, codemapper=None):
        super().__init__(args, ctx)

        if codemapper is None:
            self.codemapper = CodeMapper()
        else:
            self.codemapper = codemapper

        self_path = abspath(
            dirname(readlink(__file__) if islink(__file__) else __file__)
        )
        self.templates_path = pathjoin(self_path, "templates/cpp")
        self._declarations = []
        self._modules = {}

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
        pass

    # files = ["monitor.h", "mstring.h", "trace.h", "inputs.h",
    #         "workbag.h", "cfgset.h", "cfg.h", "prefixexpr.h",
    #         "main.cpp", "mstring.cpp", "subword-compare.h"]
    # for f in files:
    #    if f not in self.args.overwrite_default:
    #        self.copy_file(f)

    # for f in self.args.cpp_files:
    #    self.copy_file(f)

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
        self._gen(stmt.obj, wr, wr_h)
        wr(";\n")

    def _gen_foreach(self, stmt, wr, wr_h):
        wr("/* FIXME: do not use reference on simple types */\n")
        wr(f"for (auto& {stmt.value.name} : ")
        self._gen(stmt.iterable, wr, wr_h)
        wr(") {\n")
        for s in stmt.stmts:
            self._gen(s, wr, wr_h)
        wr("}\n")

    def _gen_yield(self, stmt, wr, wr_h):
        wr("{\n")
        for ev in stmt.events:
            wr("__yield(")
            self._gen(ev, wr, wr_h)
            wr(f", {stmt.trace.name});\n")
        wr("}\n")

    def _gen_new(self, stmt, wr):
        wr(f"__new_trace(/*elem_size*/ 0)")

    def _gen_is_in(self, stmt, wr, wr_h):
        wr(f"__is_in(")
        self._gen(stmt.lhs, wr, wr_h)
        wr(", ")
        self._gen(stmt.rhs, wr, wr_h)
        wr(")")

    def _gen_method_call(self, stmt, wr, wr_h):
        mod = self._modules.get(stmt.lhs.name)
        if mod:
            mod.gen("cpp", stmt, wr, self._declarations)
        else:
            wr(f"{stmt.lhs.name}.{stmt.rhs.name}(")
            for n, p in enumerate(stmt.params):
                if n > 0:
                    wr(", ")
                self._gen(p, wr, wr_h)
            wr(")")
            # raise NotImplementedError(f"Unhandled method call: {stmt}")

    def _gen_if(self, stmt, wr, wr_h):
        wr(f"if (")
        self._gen(stmt.cond, wr, wr_h)
        wr(") {\n")
        for s in stmt.true_stmts:
            self._gen(s, wr, wr_h)
        wr("}")

        if not stmt.false_stmts:
            wr("\n")
            return

        wr(" else {\n")
        for s in stmt.false_stmts:
            self._gen(s, wr, wr_h)
        wr("}\n")

    def _gen_event(self, stmt, wr, wr_h):
        wr(f"Event_{stmt.name.name}(")
        for n, p in enumerate(stmt.params):
            if n > 0:
                wr(", ")
            self._gen(p, wr, wr_h)
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

    def _gen(self, stmt, wr, wr_h):
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

        wr('#include "src.h"\n\n')
        wr("int source_thrd(void *data) {\n")
        for stmt in ast.children:
            self._gen(stmt, wr_ind, wr_h)
        wr("}\n\n")

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
                fh.write('#include "events.h"\n\n')

                self._gen_src(ast, f.write, fh.write)

        with self.new_file("inputs.cpp") as f:
            wr = f.write

            wr('#include "src.h"\n\n')
            self._gen_input_stream_class(ast, wr)
            self._gen_inputs_class(ast, wr)

        self.try_clang_format_file("inputs.cpp")
        self.try_clang_format_file("src.cpp")
        self.try_clang_format_file("src.h")
