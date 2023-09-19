from os.path import basename

from vamos_common.spec.ir.expr import Expr
from vamos_common.spec.ir.identifier import Identifier

from .expr import CodeGenExpr
from ...spec.ir.ir import Program, Statement


class CodeGenCpp(CodeGenExpr):
    """
    The final class for generating C++ code that inherits from all
    subclasses of CodeGenBase which implement code generation.
    """

    def __init__(self, args, ctx):
        super().__init__(args, ctx)

        self.add_copy_file("main.cpp")
        self.add_copy_file("trace.h")
        self.add_copy_file("htrace.h")
        self.add_copy_file("new_trace.h")
        self.add_copy_file("stdout_trace.h")

        self.add_copy_vamos_common_file("cpp/event_and_id.h")
        self.add_copy_vamos_common_file("cpp/program_data.h")

    def _gen_input_stream_class(self, ast, wr):
        wr("class InputStream {\n")
        wr("  bool hasEvent() const {\n")
        wr("    abort(); \n")
        wr("  }\n")
        wr("};\n\n")

    def _gen_inputs_class(self, ast, wr):
        wr("class Inputs {\n")
        wr("};\n\n")

    def _get_trace(self, tr):
        assert False, tr

    def gen(self, stmt, wr, wr_h):
        if isinstance(stmt, Identifier):
            wr(stmt.name)
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
        wr('#include "src.h"\n')
        wr('#include "src-includes.h"\n\n')

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
                    (
                        basename(f)
                        for f in self.args.cpp_files
                        + self.args.add_gen_files
                        + self._cmake_sources
                    )
                ),
                "@additional_cmake_definitions@": " ".join(
                    (d for d in self.args.cmake_defs + self._cmake_defs)
                ),
                "@CMAKE_BUILD_TYPE@": build_type,
            },
        )

    def generate(self, ast):
        if self.args.debug:
            with self.new_dbg_file("src.ast") as fl:
                fl.write(str(ast))

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

        with self.new_file("inputs.cpp") as f:
            wr = f.write

            wr('#include "src.h"\n\n')
            self._gen_input_stream_class(ast, wr)
            self._gen_inputs_class(ast, wr)

        self.copy_files()
        self.copy_files_no_overwrite()
        self._generate_cmake()

        self.try_clang_format_file("inputs.cpp")
        self.try_clang_format_file("src.cpp")
        self.try_clang_format_file("src.h")
