from sys import stderr


def dbg(state, params):
    print("[dbg]", ", ".join(map(str, params)), file=stderr)


METHODS = {"dbg": dbg}
