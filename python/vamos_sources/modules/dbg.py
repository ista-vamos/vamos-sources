from sys import stderr

from vamos_sources.spec.ir.expr import MethodCall
from vamos_sources.spec.modules._method import Method


def dbg(_, params):
    print("[dbg]", ", ".join(map(str, params)), file=stderr)


METHODS = {"dbg": Method("dbg", [], None, dbg)}


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
