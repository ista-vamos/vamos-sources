from ..ir.element import Identifier


class Context:
    def __init__(self):
        self.decls = {}
        self.eventdecls = {}
        self.usertypes = {}

        # self.decls = ctx.get('decls') if ctx else {}
        # self.eventdecls = ctx.get('eventdecls') if ctx else {}
        # self.usertypes = ctx.get('usertypes') if ctx else {}

    def add_eventdecl(self, *decls):
        for decl in decls:
            self._add_eventdecl(decl.name, decl)

    def _add_eventdecl(self, name, decl):
        if isinstance(name, Identifier):
            name = name.name

        assert isinstance(name, str), (name, type(name))
        if name in self.eventdecls:
            raise RuntimeError(f"Repeated declaration of an event: {decl}")

        self.eventdecls[name] = decl

    def get_eventdecl(self, name):
        if isinstance(name, Identifier):
            name = name.name
        assert isinstance(name, str), (name, type(name))
        return self.eventdecls.get(name)
