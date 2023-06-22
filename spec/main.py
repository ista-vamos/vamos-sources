import sys
from os import readlink
from os.path import islink, dirname, abspath
from parser.parser import Parser
import argparse

self_path = abspath(dirname(readlink(__file__) if islink(__file__) else __file__))
sys.path.insert(0, abspath(f"{self_path}/.."))


def main(args):
    parser = Parser()
    print(args)
    for inp in args.inputs:
        ast = parser.parse_path(inp)
        #print(ast.pretty())


def parse_arguments():
    parser = argparse.ArgumentParser()
    parser.add_argument('files', nargs='+', help='Input files (.vsrc, additional C++ files')
    parser.add_argument('--out-dir', action='store', default="/tmp/mpt", help='Output directory (default: /tmp/mpt)')
    parser.add_argument('--build-type', action='store', help='Force build type for the CMake project')
    parser.add_argument('--debug', action='store_true', help='Debugging mode')
    parser.add_argument('--exit-on-error', action='store_true', help='Stop when a violation is found')
    parser.add_argument('--verbose', '-v', action='store_true', help='Print more messages')
    parser.add_argument('--stats', action='store_true', help='Gather statistics')
    parser.add_argument('-D', action='append', default=[], help='Additional CMake definitions')
    parser.add_argument('--overwrite-default', action='append', default=[],
                        help="Do not generate the default version of the given file, its replacement is assumed to be "
                             "provided as an additional source.")
    args = parser.parse_args()

    args.inputs = []
    args.cpp_files = []
    args.sources_def = None
    args.cmake_defs = args.D
    for fl in args.files:
        if fl.endswith(".cpp") or fl.endswith(".h") or\
             fl.endswith(".hpp") or fl.endswith(".cxx") or fl.endswith("cc"):
            args.cpp_files.append(abspath(fl))
        elif fl.endswith(".vsrc"):
            args.inputs.append(fl)
        else:
            raise RuntimeError(f"Unknown file type: {fl}")

    return args

if __name__ == "__main__":
    args = parse_arguments()
    main(args)
