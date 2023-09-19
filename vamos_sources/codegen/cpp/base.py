from os import readlink
from os.path import abspath, dirname, islink, join as pathjoin

from vamos_common.codegen.codegen import CodeGen


class CodeGenBase(CodeGen):
    """
    The base class for generation C++ code. From this class, other classes inherit.
    In fact, the inheritance forms a chain: one child implements code generation for
    statements, its child for expressions, and so on. We use inheritance here only
    to split the code generation logically into several parts to avoid having
    one large class.
    """

    def __init__(self, args, ctx):
        super().__init__(args, ctx)

        self_path = abspath(
            dirname(readlink(__file__) if islink(__file__) else __file__)
        )
        self.templates_path = pathjoin(self_path, "templates/")
        self._gen_includes = set()
        self._std_includes = set()
        self._cmake_defs = []
        self._cmake_sources = []
        self._copy_files = set()
        self._copy_files_no_overwrite = set()
        # files from vamos-comon to copy
        self._copy_vamos_common_files = set()
        # a set of features used by the generated code
        self._features = set()

    def add_cmake_source(self, f):
        self._cmake_sources.append(f)

    def get_type(self, elem):
        return self.ctx.get_type(elem)

    def add_cmake_def(self, definition):
        self._cmake_defs.append(definition)

    def add_include(self, name):
        self._gen_includes.add(name)

    def add_std_include(self, name):
        self._std_includes.add(name)

    def add_copy_file_no_overwrite(self, name):
        """
        Add a file to copy to the output dir. It is not affected by the
        --overwrite argument, i.e., it is always copied no matter
        what are the command-line arguments.
        """
        self._copy_files_no_overwrite.add(name)

    def add_copy_file(self, name):
        """
        Add a file to copy to the output dir. This file will not be copied
        if it is included in the files to overwrite (spec. by the command-line arg)
        """
        self._copy_files.add(name)

    def add_copy_vamos_common_file(self, name):
        """
        Add a file from vamos-common to copy to the output dir
        """
        self._copy_vamos_common_files.add(name)

    def copy_files_no_overwrite(self):
        for f in self._copy_files_no_overwrite:
            print("COPY:", f)
            if f not in self.args.overwrite_default:
                self.copy_file(f)

        for f in self._copy_vamos_common_files:
            print("COPY vamos-common:", f)
            if f not in self.args.overwrite_default:
                self.copy_common_file(f)

    def copy_files(self):
        for f in self._copy_files:
            self.copy_file(f)
