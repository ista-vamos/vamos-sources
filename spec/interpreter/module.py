from interpreter.value import Value


class Module(Value):
    def __init__(self, methods):
        super().__init__("<module>", "<module>")
        self._methods = methods

    def get_method(self, name):
        return self._methods.get(name)
