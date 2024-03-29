#!/usr/bin/env python3

import sys
from subprocess import run, PIPE
from os.path import dirname, abspath, basename

sys.path.append(f"{abspath(dirname(__file__))}/../..")
import config

DIR = abspath(dirname(sys.argv[0]))
LLVM_PASS_DIR = f"{DIR}/../llvm"
CFLAGS = ["-std=c11"]
SHAMON_INCLUDES = [f"-I{config.vamos_buffers_INCLUDE_DIR}"]
SHAMON_LIBS = [
    f"{config.vamos_buffers_LIBRARIES_DIRS_core}/libvamos-buffers-arbiter.a",
    f"{config.vamos_buffers_LIBRARIES_DIRS_core}/libvamos-buffers-stream.a",
    f"{config.vamos_buffers_LIBRARIES_DIRS_core}/libvamos-buffers-source.a",
    f"{config.vamos_buffers_LIBRARIES_DIRS_shmbuf}/libvamos-buffers-shmbuf.a",
    f"{config.vamos_buffers_LIBRARIES_DIRS_core}/libvamos-buffers-parallel-queue.a",
    f"{config.vamos_buffers_LIBRARIES_DIRS_core}/libvamos-buffers-ringbuf.a",
    f"{config.vamos_buffers_LIBRARIES_DIRS_core}/libvamos-buffers-list.a",
    f"{config.vamos_buffers_LIBRARIES_DIRS_core}/libvamos-buffers-signature.a",
    f"{config.vamos_buffers_LIBRARIES_DIRS_core}/libvamos-buffers-event.a",
    f"{config.vamos_buffers_LIBRARIES_DIRS_core}/libvamos-buffers-utils.a",
    f"{config.vamos_buffers_LIBRARIES_DIRS_streams}/libvamos-buffers-streams.a",
]


def get_llvm_version(cc="clang"):
    proc = run([cc, "--version"], stdout=PIPE)
    ver = proc.stdout.split()[2].split(b"-")[0].split(b".")
    if len(ver) == 3:
        return int(ver[0]), int(ver[1]), int(ver[2])
    return None


class CompileOptions:
    def __init__(self):
        self.files = []
        self.files_noinst = []
        self.cc = "clang"
        self.clang = "clang"
        self.output = "a.out"
        self.cflags = []
        self.link = []
        self.linkcmd = "llvm-link"
        self.optcmd = "opt"
        self.link_asm = []
        self.link_and_instrument = []
        self.dbg = False
        self.asan = False
        self.ubsan = False
        self.dbg_events = False


def get_opts(argv):
    opts = CompileOptions()

    i = 1
    while i < len(argv):
        if argv[i] == "-o":
            i += 1
            opts.output = argv[i]
        elif argv[i] == "-cc":
            i += 1
            opts.cc = argv[i]
        elif argv[i] == "-clang":
            i += 1
            opts.clang = argv[i]
        elif argv[i] == "-llvm-link":
            i += 1
            opts.linkcmd = argv[i]
        elif argv[i] == "-opt":
            i += 1
            opts.optcmd = argv[i]
        elif argv[i] == "-li":
            i += 1
            opts.link_and_instrument.append(argv[i])
        elif argv[i] == "-l":
            i += 1
            opts.link.append(argv[i])
        elif argv[i] == "-I":
            i += 1
            opts.cflags.append(f"-I{argv[i]}")
        elif argv[i] == "-noinst":
            i += 1
            opts.files_noinst.append(argv[i])
        elif argv[i] == "-dbg":
            opts.dbg = True
        elif argv[i] == "-asan":
            opts.asan = True
        elif argv[i] == "-ubsan":
            opts.ubsan = True
        elif argv[i] == "-dbg-events":
            opts.dbg_events = True
        elif argv[i] == "-omp":
            i += 1
            opts.link_and_instrument.append(argv[i])
            opts.cflags.append("-fopenmp")
        else:
            s = argv[i]
            if any((s.endswith(suffix) for suffix in (".c", ".cpp", ".i"))):
                opts.files.append(s)
            elif any((s.endswith(suffix) for suffix in (".bc", ".ll"))):
                opts.link_and_instrument.append(s)
            elif s.endswith(".S"):
                opts.link_asm.append(s)
            else:
                opts.cflags.append(s)
        i += 1
    return opts


def cmd(args):
    print("> ", " ".join(args))
    run(args, check=True)


def main(argv):
    opts = get_opts(argv)
    assert opts.files, "No input files given"

    build_type = config.vamos_buffers_BUILD_TYPE
    release = build_type in ("Release", "RelWithDebInfo")
    if build_type is None or release:
        CFLAGS.extend(("-g3", "-O3", "-DNDEBUG"))
        lto_flags = ["-flto", "-fno-fat-lto-objects"]
    else:
        CFLAGS.append("-g")
        lto_flags = []

    if release and basename(config.vamos_buffers_C_COMPILER) != "clang":
        print(
            "WARNING: Shamon was build in Release mode but not with clang. "
            "It may cause troubles with linking."
        )

    opt_args = []
    llvm_version = get_llvm_version(opts.clang)
    if llvm_version is None or llvm_version[0] > 12:
        opt_args.append("-enable-new-pm=0")

    output = opts.output
    compiled_files = [f"{file}.bc" for file in opts.files]
    for f, out in zip(opts.files, compiled_files):
        cmd(
            [
                opts.clang,
                "-emit-llvm",
                "-fgnu89-inline",
                "-D__inline=",
                "-c",
                "-fsanitize=thread",
                "-O0",
                "-o",
                f"{out}",
                f,
            ]
            + (["-fsanitize=undefined"] if opts.ubsan else [])
            + CFLAGS
            + opts.cflags
        )
    cmd(
        [opts.linkcmd, "-o", f"{output}.tmp2.bc"]
        + compiled_files
        + opts.link_and_instrument
    )

    cmd(
        [
            opts.optcmd,
            "-load",
            f"{LLVM_PASS_DIR}/race-instrumentation.so",
            "-vamos-race-instrumentation",
            f"{output}.tmp2.bc",
            "-o",
            f"{output}.tmp3.bc",
        ]
        + (["-vamos-print-vars-addr"] if opts.dbg else [])
        + opt_args
    )

    cmd(
        [
            opts.clang,
            "-std=c11",
            "-emit-llvm",
            "-c",
            f"{DIR}/tsan_impl.c",
            "-o",
            f"{DIR}/tsan_impl.bc",
        ]
        + (["-DDBGBUF"] if opts.dbg else [])
        + (["-DDEBUG_STDOUT"] if opts.dbg_events else [])
        + (["-fsanitize=address"] if opts.asan else [])
        + (["-fsanitize=undefined"] if opts.ubsan else [])
        + CFLAGS
        + SHAMON_INCLUDES
    )
    compiled_files_link = [f"{file}.bc" for file in opts.files_noinst]
    for f, out in zip(opts.files_noinst, compiled_files_link):
        cmd(
            [opts.clang, "-emit-llvm", "-c", "-g", "-o", f"{out}", f]
            + (["-fsanitize=address"] if opts.asan else [])
            + (["-fsanitize=undefined"] if opts.ubsan else [])
            + CFLAGS
            + opts.cflags
        )
    cmd(
        [
            opts.linkcmd,
            f"{DIR}/tsan_impl.bc",
            f"{output}.tmp3.bc",
            "-o",
            f"{output}.tmp4.bc",
        ]
        + opts.link
        + compiled_files_link
    )

    cmd([opts.optcmd, "-O3", f"{output}.tmp4.bc", "-o", f"{output}.tmp5.bc"])
    cmd(
        [opts.clang, f"{output}.tmp5.bc", "-c", "-o", f"{output}.tmp5.o"]
        + opts.link_asm
        + opts.cflags
        + CFLAGS
    )
    cmd(
        [opts.cc, "-pthread", f"{output}.tmp5.o", "-o", output]
        + (["-fsanitize=address"] if opts.asan else [])
        + (["-fsanitize=undefined"] if opts.ubsan else [])
        + opts.cflags
        + CFLAGS
        + lto_flags
        + SHAMON_LIBS
    )


if __name__ == "__main__":
    main(sys.argv)
    exit(0)
