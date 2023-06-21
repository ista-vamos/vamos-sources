import logging
import sys
from os import readlink
from os.path import islink, dirname

from lark import Lark, logger

from .ast import transform_ast

grammars_dir = dirname(readlink(__file__) if islink(__file__) else __file__)
grammars_dir = f"{grammars_dir}/grammars"


class LarkParser:
    def __init__(self, debug=False):
        self._parser = Lark.open(
            "grammars/grammar.lark",
            rel_to=__file__,
            import_paths=[grammars_dir],
            debug=debug,
        )
        if debug:
            logger.setLevel(logging.DEBUG)

    def parse_path(self, path):
        return self._parser.parse((open(path).read()))

    def parse_file(self, f):
        return self._parser.parse(f.read())

    def parse_text(self, text):
        return self._parser.parse(text)


class Parser(LarkParser):
    def __init__(self, debug=False):
        super().__init__(debug)

    def parse_file(self, f):
        return transform_ast(super().parse_file(f))

    def parse_path(self, path):
        return transform_ast(super().parse_path(path))

    def parse_text(self, text):
        return transform_ast(super().parse_text(text))


def main():
    if len(sys.argv) != 2:
        print("Usage: parser.py file", file=sys.stderr)
        exit(1)

    parser = Parser()
    ast = parser.parse_path(sys.argv[1])
    # print(ast.pretty())
    # print(parser.parse_path(sys.argv[1]).pretty())


if __name__ == "__main__":
    main()
