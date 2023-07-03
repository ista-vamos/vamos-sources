from sys import stderr

from vamos_sources.interpreter.method import Method


def dbg(_, params):
    print("[dbg]", ", ".join(map(str, params)), file=stderr)


METHODS = {"dbg": Method("dbg", [], None, dbg)}
