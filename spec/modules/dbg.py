from sys import stderr


def dbg(_, params):
    print("[dbg]", ", ".join(map(str, params)), file=stderr)


METHODS = {"dbg": dbg}
