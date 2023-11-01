#ifndef VAMOS_SOURCES_MODULE_FILE_READER_H
#define VAMOS_SOURCES_MODULE_FILE_READER_H

#include <fstream>
#include <iostream>
#include <string>

class LinesRange;

class LinesIterator {
    size_t read_off{0};
    std::string line;
    std::fstream& _s;
    bool _ok;

    LinesIterator(std::fstream& s, bool ok = true) : _s(s), _ok(ok) {}

    friend class LinesRange;

   public:
    bool operator==(const LinesIterator& rhs) const {
        return (!_ok && !rhs._ok /* boths are end() */) ||
               (&_s == &rhs._s && read_off == rhs.read_off && _ok == rhs._ok);
    }
    bool operator!=(const LinesIterator& rhs) const { return !operator==(rhs); }
    LinesIterator& operator++() {
        std::getline(_s, line);

        read_off += line.size();
        _ok = !_s.eof();
        return *this;
    }
    LinesIterator operator++(int) {
        auto tmp = *this;
        operator++();
        return tmp;
    }

    const std::string& operator*() {
        // if this is the `begin` iterator, read the first line
        if (read_off == 0)
            operator++();
        return line;
    }
};

class FileReader;

class LinesRange {
    std::fstream& s;

    LinesRange(std::fstream& stream) : s(stream) {}

    friend class FileReader;

   public:
    LinesIterator begin() { return LinesIterator(s); }
    LinesIterator end() { return LinesIterator(s, false); }
    LinesIterator begin() const { return LinesIterator(s); }
    LinesIterator end() const { return LinesIterator(s, false); }
};

class FileReader {
    std::fstream s;

   public:
    FileReader(const std::string& file) {
        s.open(file, std::ios::in);
        if (!s.is_open()) {
            std::cerr << "Failed opening " << file << "\n";
            abort();
        }
    }

    ~FileReader() { s.close(); }

    LinesRange lines() { return LinesRange(s); }
};

#endif
