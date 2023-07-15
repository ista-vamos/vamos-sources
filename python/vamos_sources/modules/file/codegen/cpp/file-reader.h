#ifndef VAMOS_SOURCES_MODULE_FILE_READER_H
#define VAMOS_SOURCES_MODULE_FILE_READER_H

#include <string>

class LinesIterator {
public:
  bool operator==(const LinesIterator& rhs) const { abort(); }
  bool operator!=(const LinesIterator& rhs) const { return !operator==(rhs); }
  LinesIterator& operator++() { abort(); }
  LinesIterator operator++(int) { auto tmp = *this; operator++(); return tmp; }

  const std::string& operator*() { abort(); }
};


class LinesRange {
public:
    LinesIterator begin() { abort(); }
    LinesIterator end() { abort(); }
    LinesIterator begin() const { abort(); }
    LinesIterator end() const { abort(); }
};

class FileReader {
public:
    FileReader(const std::string& file) {
        abort();
    }

    LinesRange lines() { abort(); }
};

#endif
