from .element import Element


class TransitionOutput(Element):
    def __init__(self, seq):
        super().__init__()
        self.values = seq

    def __bool__(self):
        return bool(self.values)

    def __getitem__(self, item):
        return self.values[item]

    def __len__(self):
        return len(self.values)

    def __repr__(self):
        return f"TransitionOutput({self.values})"

    @property
    def children(self):
        return [self.values]


class Transition(Element):
    def __init__(self, start, end, mpe, cond, output):
        super().__init__()
        self.start = start
        self.end = end
        self.mpe = mpe
        self.cond = cond
        self.output = output

    def __repr__(self):
        return f"Transition({self.start} -> {self.end}, {self.mpe}[{self.cond}]/{self.output})"

    @property
    def children(self):
        return [self.cond]
