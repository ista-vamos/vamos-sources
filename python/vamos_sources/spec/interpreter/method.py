class Method:
    def __init__(self, name, types, retty, fn):
        """
        :types: are types of parameters
        """
        self.name = name
        self.types = types
        self.retty = retty
        self.fn = fn

    def __repr__(self):
        return f"Method({self.name}, {self.types}, {self.retty} = {self.fn})"

    def execute(self, state, params):
        return self.fn(state, params)
