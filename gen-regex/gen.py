#!/usr/bin/python3

from sys import argv as cmdargs, stderr
from gen_stdin import gen_stdin
from gen_bpf import gen_bpf
from gen_drio import gen_drio
from utils import *

def usage_and_exit(msg=None):
    if msg:
        print(msg, file=stderr)
    print(f"Usage: {argv[0]} source-type shmkey "
           "event-name data-descr event-signature ",
           "[event-name data-descr event-signature] ...",
          file=stderr)
    exit(1)

def usage_and_exit(msg=None):
    if msg:
        print(msg, file=stderr)
    print(f"Usage: {cmdargs[0]} source-type shmkey "
           "event-name data-descr event-signature ",
           "[event-name data-descr event-signature] ...",
          file=stderr)
    exit(1)

def gen(source_type, shmkey, events):
    if source_type == "stdin":
        return gen_stdin(shmkey, events)
   #if source_type == "bpf":
   #    return gen_bpf("syscall.enter.write", shmkey, events)
    if source_type in ("drio", "dynamorio", "dr"):
        return gen_drio("drio-stdin", shmkey, events)

    msg_and_exit("Unknown source type")

def main(argv):
    """
    source-type shmkey event-name data-descr event-signature [event-name
    data-descr event-signature] ...

    data-descr is usually a regular expression
    """
    if len(argv) < 6:
        usage_and_exit()

    source_type = argv[1]
    shmkey = argv[2]
    events = argv[3:]

    if shmkey[0] != '/':
        usage_and_exit("shmkey must start with /")
    if len(events) % 3 != 0:
        usage_and_exit()

    src = gen(source_type, shmkey, argv[3:])

    run_clang_format(src)
    run_clang(src, "source")

if __name__ == "__main__":
    main(cmdargs)
