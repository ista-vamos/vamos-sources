class Method:
    """
    Method description and its implementation for the interpreter
    """

    def __init__(self, header, fn):
        self.header = header
        self.fn = fn

    def __repr__(self):
        return f"Method({self.header} = {self.fn})"

    def execute(self, state, params):
        return self.fn(state, params)
