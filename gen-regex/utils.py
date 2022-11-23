from sys import argv as cmdargs, stderr
from subprocess import run as _run
from os.path import dirname, abspath, join as joinpath

def run(cmd, *args, **kwargs):
    print("\033[32m>", " ".join(cmd), "\033[0m", file=stderr)
    return _run(cmd, *args, **kwargs)

def run_re2c(infile, outfile):
    run(["re2c", "-i", "-P", "-W", infile, "-o", outfile]).check_returncode()

def run_clang_format(infile):
    return run(["clang-format", "-i", infile]).returncode

def run_clang(src, out):
    top_dir = abspath(joinpath(dirname(cmdargs[0]), "../.."))
    core_dir = joinpath(top_dir, "core")
    shmbuf_dir = joinpath(top_dir, "shmbuf")

    include_dirs = [top_dir, core_dir]
    link_dirs = [core_dir, shmbuf_dir]
    cflags = ["-Wall", "-g"]
    ldflags = ["-lshamon-source", "-lshamon-shmbuf",
               "-lshamon-signature", "-lshamon-utils"]
    libraries = []
    run(["clang", src, "-o", out] +
        [f"-I{d}" for d in include_dirs] +
        [f"-L{d}" for d in link_dirs] +
        cflags + libraries + ldflags).check_returncode()

def msg_and_exit(msg):
    print(msg, file=stderr)
    exit(1)


