import argparse
import os
import sys
from multiprocessing import Process
from os import readlink
from os.path import islink, dirname, abspath

from cmd import parse_arguments

self_path = abspath(dirname(readlink(__file__) if islink(__file__) else __file__))
sys.path.insert(0, abspath(f"{self_path}/../.."))

sys.path.insert(0, abspath(f"{self_path}/../../.."))
from config import vamos_common_PYTHONPATH
sys.path.pop(0)

sys.path.append(vamos_common_PYTHONPATH)

from vamos_sources.codegen.codegencpp import CodeGenCpp
from vamos_sources.spec.parser.parser import Parser


def compile(program, inp, cmdargs):
    print("module name:", __name__)
    print("Parent process:", os.getppid())
    print("Interpreter PID:", os.getpid())

    I = Interpreter(program, inp, cmdargs)
    I.run()


def main(cmdargs):
    parser = Parser()
    # print(args)
    programs = []
    if len(cmdargs.inputs) != 1:
        raise NotImplementedError("Currently we support only a single .vsrc file")

    for inp in cmdargs.inputs:
        ast, ctx = parser.parse_path(inp.file)
        programs.append((ast, ctx, inp))
        # print(ast.pretty())

    for ast, ctx, inp in programs:
        codegen = CodeGenCpp(cmdargs, ctx)
        codegen.generate(ast)
        ctx.dump()


# for p in processes:
#    p.join()


if __name__ == "__main__":
    cmd_args = parse_arguments()
    main(cmd_args)
