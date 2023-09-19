from sys import stderr

from vamos_common.types.methods import MethodHeader
from vamos_sources.interpreter.method import Method
from vamos_sources.spec.ir.expr import MethodCall


def dbg(_, params):
    print("[dbg]", ", ".join(map(str, params)), file=stderr)


header_dbg = MethodHeader("dbg", [], None)
METHODS = {"dbg": Method(header_dbg, dbg)}


def gen(lang, codegen, stmt, wr, wr_h):
    assert lang == "cpp"
    gen_cpp(codegen, stmt, wr, wr_h)


def gen_cpp(codegen, stmt, wr, wr_h):
    if isinstance(stmt, MethodCall):
        codegen.add_std_include("iostream")
        codegen.add_cmake_def("-DDEBUG")
        if stmt.rhs.name == "dbg":
            wr('std::cerr << "[dbg] " <<')
            for n, p in enumerate(stmt.params):
                if n > 0:
                    wr("<< ")
                codegen.gen(p, wr, wr_h)
            wr('<< "\\n";')
        else:
            raise NotImplementedError(f"Unknown method: {stmt.rhs.name}")
    else:
        raise NotImplementedError(f"Unknown stmt: {stmt}")
