#pragma once
#include <cctype>
#include <cstddef>    // for size_t
#include <cstring>    // for memmove, memcpy
#include <functional> // for std::hash specialization
#include <iostream>   // for ostream, istream
#include <stdexcept>  // for out_of_range (substr)

#include "STL_rewrite/string_view.hpp"

// IMPORTANT: I did not count '\0' in size or capacity
class string {

  public:
    static constexpr size_t npos = static_cast<size_t>(-1);

    // Default Constructor
    // REQUIRES: Nothing
    // MODIFIES: *this
    // EFFECTS: Creates an empty string
    string() : _buffer(emptyBuffer()), _size(0), _capacity(0) {}

    // string Literal / C string Constructor
    // REQUIRES: cstr is a null terminated C style string
    // MODIFIES: *this
    // EFFECTS: Creates a string with equivalent contents to cstr
    string(const char *cstr) : _buffer(emptyBuffer()), _size(getLength(cstr)), _capacity(_size) {
        if (_size == 0) {
            return;
        }
        _buffer = new char[_capacity + 1];
        copyData(_buffer, cstr, _size);
        _buffer[_size] = '\0';
    }

    // Range constructor [begin, end)
    // REQUIRES: begin/end define a valid range or both are null
    // MODIFIES: *this
    // EFFECTS: Creates a string containing characters from begin up to (not including) end
    string(const char *begin, const char *end) : _buffer(emptyBuffer()), _size(0), _capacity(0) {
        if (begin == nullptr || end == nullptr || end <= begin) {
            return;
        }

        _size = static_cast<size_t>(end - begin);
        _capacity = _size;
        _buffer = new char[_capacity + 1];
        copyData(_buffer, begin, _size);
        _buffer[_size] = '\0';
    }

    // string_view constructor (characters need not be NUL-terminated)
    // REQUIRES: Nothing
    // MODIFIES: *this
    // EFFECTS: Creates a string with the same sequence of characters as sv
    string(::string_view sv) : _buffer(emptyBuffer()), _size(sv.size()), _capacity(_size) {
        if (_size == 0) {
            return;
        }
        _buffer = new char[_capacity + 1];
        if (sv.data() != nullptr) {
            copyData(_buffer, sv.data(), _size);
        }
        _buffer[_size] = '\0';
    }

    // deep copy constructor
    string(const string &other)
        : _buffer(emptyBuffer()), _size(other._size), _capacity(other._size) {
        if (_size == 0) {
            return;
        }
        _buffer = new char[_capacity + 1];
        copyData(_buffer, other._buffer, _size);
        _buffer[_size] = '\0';
    }

    // assignment operator
    string &operator=(const string &other) {
        if (this != &other) {
            releaseBuffer();
            _size = other._size;
            _capacity = other._size;
            if (_size == 0) {
                _buffer = emptyBuffer();
            } else {
                _buffer = new char[_capacity + 1];
                copyData(_buffer, other._buffer, _size);
                _buffer[_size] = '\0';
            }
        }
        return *this;
    }

    // move constructor
    string(string &&other) noexcept
        : _buffer(other._buffer), _size(other._size), _capacity(other._capacity) {
        other._buffer = emptyBuffer();
        other._size = 0;
        other._capacity = 0;
    }

    string &operator=(string &&other) noexcept {
        if (this != &other) {
            releaseBuffer();
            _buffer = other._buffer;
            _size = other._size;
            _capacity = other._capacity;
            other._buffer = emptyBuffer();
            other._size = 0;
            other._capacity = 0;
        }
        return *this;
    }

    // destructor
    ~string() { releaseBuffer(); }

    // Size
    // REQUIRES: Nothing
    // MODIFIES: Nothing
    // EFFECTS: Returns the number of characters in the string
    size_t size() const { return _size; }
    size_t length() const { return _size; }
    size_t capacity() const { return _capacity; }
    bool empty() const { return _size == 0; }

    // C string Conversion
    // REQUIRES: Nothing
    // MODIFIES: Nothing
    // EFFECTS: Returns a pointer to a null terminated C string of *this
    const char *cstr() const { return _buffer; }
    const char *c_str() const { return _buffer; }

    const char *data() const { return _buffer; }
    char *data() { return _buffer; }

    // Iterator Begin
    // REQUIRES: Nothing
    // MODIFIES: Nothing
    // EFFECTS: Returns a random access iterator to the start of the string
    char *begin() { return _buffer; }
    const char *begin() const { return _buffer; }

    // Iterator End
    // REQUIRES: Nothing
    // MODIFIES: Nothing
    // EFFECTS: Returns a random access iterator to the end of the string
    char *end() { return _buffer + _size; }
    const char *end() const { return _buffer + _size; }

    // Element Access
    // REQUIRES: 0 <= i < size()
    // MODIFIES: Allows modification of the i'th element
    // EFFECTS: Returns the i'th character of the string
    char &operator[](size_t i) { return _buffer[i]; }

    // //I added in something for read only!
    const char &operator[](size_t i) const { return _buffer[i]; }

    // string Append
    // REQUIRES: Nothing
    // MODIFIES: *this
    // EFFECTS: Appends the contents of other to *this, resizing any
    //      memory at most once
    void operator+=(const string &other) {
        size_t other_size = other.size();
        growCapacity(_size + other_size);

        copyData(_buffer + _size, other._buffer, other_size);
        _size += other_size;
        _buffer[_size] = '\0';
    }

    void operator+=(char c) { pushBack(c); }

    void operator+=(const char *cstr) {
        if (cstr == nullptr) {
            return;
        }
        const size_t n = getLength(cstr);
        append(cstr, n);
    }

    void append(const char *data, size_t count) {
        growCapacity(_size + count);
        copyData(_buffer + _size, data, count);
        _size += count;
        _buffer[_size] = '\0';
    }

    size_t find(const char *substr) const {
        if (substr == nullptr || substr[0] == '\0') {
            return 0;
        }

        size_t sub_len = getLength(substr);

        if (sub_len > _size) {
            return npos;
        }

        for (size_t i = 0; i <= _size - sub_len; ++i) {
            size_t j = 0;
            while (j < sub_len && _buffer[i + j] == substr[j]) {
                ++j;
            }

            if (j == sub_len) {
                return i;
            }
        }

        return npos;
    }

    size_t find(const char *substr, size_t pos) const {
        if (substr == nullptr || substr[0] == '\0') {
            return pos;
        }

        size_t sub_len = getLength(substr);

        if (sub_len > _size) {
            return npos;
        }

        for (size_t i = pos; i <= _size - sub_len; ++i) {
            size_t j = 0;
            while (j < sub_len && _buffer[i + j] == substr[j]) {
                ++j;
            }

            if (j == sub_len) {
                return i;
            }
        }

        return npos;
    }

    size_t find(char c) const {
        for (size_t i = 0; i < _size; ++i) {
            if (_buffer[i] == c) {
                return i;
            }
        }
        return npos;
    }

    size_t find(char c, size_t pos) const {
        for (size_t i = pos; i < _size; ++i) {
            if (_buffer[i] == c) {
                return i;
            }
        }
        return npos;
    }

    // Finds the first character in *this that matches any character in set (NUL-terminated).
    size_t find_first_of(const char *set) const {
        if (set == nullptr || set[0] == '\0' || _size == 0) {
            return npos;
        }
        for (size_t i = 0; i < _size; ++i) {
            for (size_t j = 0; set[j] != '\0'; ++j) {
                if (_buffer[i] == set[j]) {
                    return i;
                }
            }
        }
        return npos;
    }

    // Returns the substring [pos, pos + count). If count == npos, extends to end of string.
    string substr(size_t pos, size_t count = npos) const {
        if (pos > _size) {
            throw std::out_of_range("string::substr");
        }
        size_t avail = _size - pos;
        size_t len = (count == npos) ? avail : count;
        if (len > avail) {
            len = avail;
        }
        return string(_buffer + pos, _buffer + pos + len);
    }

    // Push Back
    // REQUIRES: Nothing
    // MODIFIES: *this
    // EFFECTS: Appends c to the string
    void pushBack(char c) {
        if (_size == _capacity) {
            growCapacity(_size + 1);
        }
        _buffer[_size] = c;
        ++_size;
        _buffer[_size] = '\0';
    }

    // Pop Back
    // REQUIRES: string is not empty
    // MODIFIES: *this
    // EFFECTS: Removes the last charater of the string
    void popBack() {
        if (_size > 0) {
            --_size;
            _buffer[_size] = '\0';
        }
    }

    void pop_back() { popBack(); }

    void reserve(size_t new_cap) {
        if (new_cap <= _capacity) {
            return;
        }
        char *newBuffer = new char[new_cap + 1];
        copyData(newBuffer, _buffer, _size);
        newBuffer[_size] = '\0';
        releaseBuffer();
        _buffer = newBuffer;
        _capacity = new_cap;
    }

    void erase(size_t pos) {
        if (pos > _size) {
            throw std::out_of_range("string::erase");
        }
        _size = pos;
        _buffer[_size] = '\0';
    }

    void erase(size_t pos, size_t count) {
        if (pos > _size) {
            throw std::out_of_range("string::erase");
        }
        const size_t tail = _size - pos;
        if (count >= tail) {
            _size = pos;
        } else {
            for (size_t i = 0; i < tail - count; ++i) {
                _buffer[pos + i] = _buffer[pos + count + i];
            }
            _size -= count;
        }
        _buffer[_size] = '\0';
    }

    void replace(size_t pos, size_t len, const char *str, size_t n) {
        if (pos > _size) {
            throw std::out_of_range("string::replace");
        }
        const size_t tail_avail = _size - pos;
        if (len > tail_avail) {
            len = tail_avail;
        }
        const size_t suffix_len = _size - pos - len;
        const size_t new_size = _size - len + n;
        growCapacity(new_size);
        char *const p = _buffer + pos;
        memmove(p + n, p + len, suffix_len);
        memcpy(p, str, n);
        _size = new_size;
        _buffer[_size] = '\0';
    }

    void replace(size_t pos, size_t len, const char *cstr) {
        size_t n = 0;
        if (cstr != nullptr) {
            while (cstr[n] != '\0') {
                ++n;
            }
        }
        replace(pos, len, cstr, n);
    }

    char back() const {
        if (_size == 0) {
            throw std::out_of_range("string::back");
        }
        return _buffer[_size - 1];
    }

    // Equality Operator
    // REQUIRES: Nothing
    // MODIFIES: Nothing
    // EFFECTS: Returns whether all the contents of *this
    //    and other are equal
    bool operator==(const string &other) const {
        if (_size != other._size) {
            return false;
        }
        return compareStrings(_buffer, other._buffer) == 0;
    }

    bool operator==(const char *rhs) const {
        if (rhs == nullptr) {
            return _size == 0;
        }
        size_t i = 0;
        for (; i < _size && rhs[i] != '\0'; ++i) {
            if (_buffer[i] != rhs[i]) {
                return false;
            }
        }
        return i == _size && rhs[i] == '\0';
    }

    // Not-Equality Operator
    // REQUIRES: Nothing
    // MODIFIES: Nothing
    // EFFECTS: Returns whether at least one character differs between
    //    *this and other
    bool operator!=(const string &other) const { return !(*this == other); }

    // Less Than Operator
    // REQUIRES: Nothing
    // MODIFIES: Nothing
    // EFFECTS: Returns whether *this is lexigraphically less than other
    bool operator<(const string &other) const { return compareStrings(_buffer, other._buffer) < 0; }

    // Greater Than Operator
    // REQUIRES: Nothing
    // MODIFIES: Nothing
    // EFFECTS: Returns whether *this is lexigraphically greater than other
    bool operator>(const string &other) const { return compareStrings(_buffer, other._buffer) > 0; }

    // Less Than Or Equal Operator
    // REQUIRES: Nothing
    // MODIFIES: Nothing
    // EFFECTS: Returns whether *this is lexigraphically less or equal to other
    bool operator<=(const string &other) const { return !(*this > other); }

    // Greater Than Or Equal Operator
    // REQUIRES: Nothing
    // MODIFIES: Nothing
    // EFFECTS: Returns whether *this is lexigraphically less or equal to other
    bool operator>=(const string &other) const { return !(*this < other); }

  private:
    char *_buffer;
    size_t _size;
    size_t _capacity;

    static char *emptyBuffer() {
        static char empty = '\0';
        return &empty;
    }

    // strlen
    size_t getLength(const char *str) const {
        if (str == nullptr) {
            return 0;
        }

        size_t len = 0;
        while (str[len] != '\0') {
            ++len;
        }
        return len;
    }

    // memcpy
    void copyData(char *dest, const char *src, size_t count) {
        for (size_t i = 0; i < count; ++i) {
            dest[i] = src[i];
        }
    }

    // strcmp, returns -1 if str1 < str2, 0 if equal, 1 if str1 > str2
    int compareStrings(const char *str1, const char *str2) const {
        size_t i = 0;
        while (str1[i] != '\0' && str2[i] != '\0') {
            if (str1[i] != str2[i]) {
                return (str1[i] < str2[i]) ? -1 : 1;
            }
            ++i;
        }

        if (str1[i] == '\0' && str2[i] == '\0') {
            return 0;
        }

        return (str1[i] == '\0') ? -1 : 1;
    }

    void growCapacity(size_t minCapacity) {
        if (minCapacity <= _capacity) {
            return;
        }

        size_t newCapacity = (_capacity == 0) ? 1 : _capacity;
        while (newCapacity < minCapacity) {
            newCapacity *= 2;
        }

        char *newBuffer = new char[newCapacity + 1];
        copyData(newBuffer, _buffer, _size);
        newBuffer[_size] = '\0';
        releaseBuffer();

        _buffer = newBuffer;
        _capacity = newCapacity;
    }

    void releaseBuffer() {
        if (_buffer != emptyBuffer()) {
            delete[] _buffer;
        }
    }
};

inline std::ostream &operator<<(std::ostream &os, const ::string &s) {
    if (s.size() > 0) {
        os << s.cstr();
    }
    return os;
}

inline std::istream &operator>>(std::istream &is, ::string &s) {
    s = ::string();
    using traits = std::istream::traits_type;
    auto ch = is.get();
    while (ch != traits::eof() && std::isspace(static_cast<unsigned char>(ch))) {
        ch = is.get();
    }
    if (ch == traits::eof()) {
        is.setstate(std::ios::failbit);
        return is;
    }
    while (ch != traits::eof() && !std::isspace(static_cast<unsigned char>(ch))) {
        s.pushBack(static_cast<char>(ch));
        ch = is.get();
    }
    if (ch != traits::eof()) {
        is.unget();
    }
    return is;
}

inline ::string operator+(const ::string &lhs, const char *rhs) {
    ::string result(lhs);
    result += ::string(rhs);
    return result;
}

inline ::string operator+(const char *lhs, const ::string &rhs) {
    ::string result(lhs);
    result += rhs;
    return result;
}

inline ::string operator+(const ::string &lhs, const ::string &rhs) {
    ::string result(lhs);
    result += rhs;
    return result;
}

inline bool operator==(const char *lhs, const ::string &rhs) { return rhs == lhs; }

namespace std {
template <> struct hash<::string> {
    size_t operator()(const ::string &s) const noexcept {
        size_t h = 0;
        const char *p = s.cstr();
        while (p && *p) {
            h = h * 131u + static_cast<unsigned char>(*p++);
        }
        return h;
    }
};
} // namespace std
