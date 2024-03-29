import argparse
from os.path import abspath


class Input:
    def __init__(self, spec, cmdargs):
        self.file = spec
        self.args = cmdargs

    def __repr__(self):
        return f"Input({self.file}, args={self.args})"


def parse_arguments():
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "files",
        nargs="+",
        help="Input files (.vsrc, additional C++ files) and their arguments"
        " (each .vsrc file can be followed by arguments)",
    )
    parser.add_argument(
        "--out-dir",
        action="store",
        default="/tmp/vsrc",
        help="Output directory (default: /tmp/vsrc)",
    )
    parser.add_argument(
        "--out-dir-overwrite",
        action="store",
        default=True,
        help="Overwrite the contents of the output dir if it exists (default: True)",
    )
    parser.add_argument(
        "--build-type", action="store", help="Force build _type for the CMake project"
    )
    parser.add_argument("--debug", action="store_true", help="Debugging mode")
    parser.add_argument(
        "--exit-on-error", action="store_true", help="Stop when a violation is found"
    )
    parser.add_argument(
        "--verbose", "-v", action="store_true", help="Print more messages"
    )
    parser.add_argument("--stats", action="store_true", help="Gather statistics")
    parser.add_argument(
        "-D", action="append", default=[], help="Additional CMake definitions"
    )
    parser.add_argument(
        "--modules-dirs", action="append", default=[], help="Paths to extension modules"
    )
    parser.add_argument(
        "--overwrite-default",
        action="append",
        default=[],
        help="Do not generate the default version of the given file, "
        "its replacement is assumed to be provided as an additional source.",
    )
    args = parser.parse_args()

    args.inputs = []
    args.cpp_files = []
    args.sources_def = None
    args.cmake_defs = args.D
    args.add_gen_files = []
    inp = None
    for fl in args.files:
        if (
            fl.endswith(".cpp")
            or fl.endswith(".h")
            or fl.endswith(".hpp")
            or fl.endswith(".cxx")
            or fl.endswith("cc")
        ):
            args.cpp_files.append(abspath(fl))
        elif fl.endswith(".vsrc"):
            inp = Input(fl, [])
            args.inputs.append(inp)
        elif inp is not None:
            inp.args.append(fl)
        else:
            raise RuntimeError(f"Unknown file _type: {fl}")

    return args
