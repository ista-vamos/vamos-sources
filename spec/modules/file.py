from interpreter.value import Value, LazyIterator
from ir.type import IterableType, StringType




class FileReader(Value):
    def __init__(self, fl):
        super().__init__("<FileReader>", IterableType())
        self.file = fl
        self.fobj = open(fl)

    def __del__(self):
        self.fobj.close()
        del self

    def get_method(self, name):
        if name == 'lines':
            return lambda state, params: self.lines()

    def lines(self):
        return LazyIterator(self.fobj, StringType)

def reader(state, params):
    assert len(params) == 1, params
    print("Calling file.reader", params)
    return FileReader(params[0])


METHODS = {
    'reader' : reader
}