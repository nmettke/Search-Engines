// string.h
// 
// Starter file for a string template


#include <cstddef>   // for size_t
#include <iostream>  // for ostream


// IMPORTANT: I did not count '\0' in size or capacity
class string {

public:  

   // Default Constructor
   // REQUIRES: Nothing
   // MODIFIES: *this
   // EFFECTS: Creates an empty string
   string(): _buffer(nullptr), _size(0), _capacity(0) {
      _buffer = new char[1];  // for the null terminator
      _buffer[0] = '\0';
   }

   // string Literal / C string Constructor
   // REQUIRES: cstr is a null terminated C style string
   // MODIFIES: *this
   // EFFECTS: Creates a string with equivalent contents to cstr
   string(const char *cstr)
      : _buffer(nullptr),
        _size(getLength(cstr)),
        _capacity(_size)
   {
      _buffer = new char[_capacity + 1];
      copyData(_buffer, cstr, _size);
      _buffer[_size] = '\0';
   }

   // deep copy constructor
   string(const string &other) 
      : _buffer(nullptr),
        _size(other._size),
        _capacity(other._capacity)
   {
      _buffer = new char[_capacity + 1];
      copyData(_buffer, other._buffer, _size);
      _buffer[_size] = '\0';
   }

   // assignment operator
   string& operator=(const string &other) {
      if (this != &other) {
         delete[] _buffer;
         _size = other._size;
         _capacity = other._capacity;
         _buffer = new char[_capacity + 1];
         copyData(_buffer, other._buffer, _size);
         _buffer[_size] = '\0';
      }
      return *this;
   }

   // move constructor
   string(string&& other) noexcept 
      : _buffer(other._buffer),
        _size(other._size),
        _capacity(other._capacity)
   {
      other._buffer = nullptr;
      other._size = 0;
      other._capacity = 0;
   }

   // destructor
   ~string() {
      delete[] _buffer;
   }

   // Size
   // REQUIRES: Nothing
   // MODIFIES: Nothing
   // EFFECTS: Returns the number of characters in the string
   size_t size() const {
      return _size;
   }

   // C string Conversion
   // REQUIRES: Nothing
   // MODIFIES: Nothing
   // EFFECTS: Returns a pointer to a null terminated C string of *this
   const char *cstr() const {
      return _buffer;
   }

   // Iterator Begin
   // REQUIRES: Nothing
   // MODIFIES: Nothing
   // EFFECTS: Returns a random access iterator to the start of the string
   const char *begin() const {
      return _buffer;
   }
   
   // Iterator End
   // REQUIRES: Nothing
   // MODIFIES: Nothing
   // EFFECTS: Returns a random access iterator to the end of the string
   const char *end() const {
      return _buffer + _size;
   }

   // Element Access
   // REQUIRES: 0 <= i < size()
   // MODIFIES: Allows modification of the i'th element
   // EFFECTS: Returns the i'th character of the string
   char &operator[]( size_t i ) {
      return _buffer[i];
   }

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

   // Not-Equality Operator
   // REQUIRES: Nothing
   // MODIFIES: Nothing
   // EFFECTS: Returns whether at least one character differs between
   //    *this and other
   bool operator!=(const string &other) const {
      return !(*this == other);
   }

   // Less Than Operator
   // REQUIRES: Nothing
   // MODIFIES: Nothing
   // EFFECTS: Returns whether *this is lexigraphically less than other
   bool operator<(const string &other) const {
      return compareStrings(_buffer, other._buffer) < 0;
   }

   // Greater Than Operator
   // REQUIRES: Nothing
   // MODIFIES: Nothing
   // EFFECTS: Returns whether *this is lexigraphically greater than other
   bool operator>(const string &other) const {
      return compareStrings(_buffer, other._buffer) > 0;
   }

   // Less Than Or Equal Operator
   // REQUIRES: Nothing
   // MODIFIES: Nothing
   // EFFECTS: Returns whether *this is lexigraphically less or equal to other
   bool operator<=(const string &other) const {
      return !(*this > other);
   }

   // Greater Than Or Equal Operator
   // REQUIRES: Nothing
   // MODIFIES: Nothing
   // EFFECTS: Returns whether *this is lexigraphically less or equal to other
   bool operator>=(const string &other) const {
      return !(*this < other);
   }

private:
   char *_buffer;
   size_t _size;
   size_t _capacity;

   // strlen
   size_t getLength(const char* str) const {
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
   void copyData(char* dest, const char* src, size_t count) {
      for (size_t i = 0; i < count; ++i) {
         dest[i] = src[i];
      }
   }

   // strcmp, returns -1 if str1 < str2, 0 if equal, 1 if str1 > str2
   int compareStrings(const char* str1, const char* str2) const {
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

      char* newBuffer = new char[newCapacity];
      copyData(newBuffer, _buffer, _size);
      delete[] _buffer;

      _buffer = newBuffer;
      _capacity = newCapacity;
   }
};


std::ostream &operator<<(std::ostream &os, const string &s) {
   if (s.size() > 0) {
      os << s.cstr();
   }
   return os;
}
