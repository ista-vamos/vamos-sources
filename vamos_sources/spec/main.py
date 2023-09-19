import os
import sys
from multiprocessing import Process
from os import readlink
from os.path import islink, dirname, abspath

from _cmd import parse_arguments

self_path = abspath(dirname(readlink(__file__) if islink(__file__) else __file__))
sys.path.insert(0, abspath(f"{self_path}/../.."))

sys.path.insert(0, abspath(f"{self_path}/../../.."))
from config import vamos_common_PYTHONPATH

sys.path.pop(0)
sys.path.append(vamos_common_PYTHONPATH)

from vamos_sources.interpreter.interpreter import Interpreter
from vamos_sources.spec.parser.parser import Parser


def interpret(program, inp, cmdargs):
    print("module name:", __name__)
    print("Parent process:", os.getppid())
    print("Interpreter PID:", os.getpid())

    I = Interpreter(program, inp, cmdargs)
    I.run()


def main(cmdargs):
    parser = Parser()
    # print(args)
    programs = []
    for inp in cmdargs.inputs:
        ast, _ = parser.parse_path(inp.file)
        programs.append((ast, inp))
        # print(ast.pretty())

    processes = []
    for p, inp in programs:
        proc = Process(target=interpret, args=(p, inp, cmdargs))
        processes.append(proc)
        proc.run()


# for p in processes:
#    p.join()


if __name__ == "__main__":
    cmd_args = parse_arguments()
    main(cmd_args)
