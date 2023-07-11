#ifndef VAMOS_SOURCES_CODEGEN_COMMON_RANGE_H_
#define VAMOS_SOURCES_CODEGEN_COMMON_RANGE_H_

template <typename Type>
struct NumIter {
    Type value;

    NumIter(Type v) : value(v) {}

    Type operator*() const { return value; }
    NumIter &operator++() { ++value; return *this; }
    NumIter operator++(int) {  auto tmp = *this; ++value; return tmp; }

    bool operator==(NumIter& rhs) const { return value == rhs.value; }
    bool operator!=(NumIter& rhs) const { return value != rhs.value; }
};

template <typename Type>
struct CommonRange {
  Type _start, _end;

  CommonRange(Type s, Type e) : _start(s), _end(e) {}

  NumIter<Type> begin() { return NumIter<Type>(_start); }
  NumIter<Type> end() { return NumIter<Type>(_end); }

  NumIter<Type> begin() const { return NumIter<Type>(_start); }
  NumIter<Type> end() const { return NumIter<Type>(_end); }
};

#endif
