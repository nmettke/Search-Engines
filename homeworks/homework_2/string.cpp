#include "string.h"

class string
{
    public:
        // Default to a full string
        // Note: size is number of actual chars, excluding the terminator
        string(): _data(new char[1]), _size(0), _capacity(1){
            _data[0] = '\0';
        }

        string(const char *cstr){
            size_t cstr_len = 0;

            // increment cstr_len until reaching terminator 
            while(cstr[cstr_len]){ ++cstr_len; }

            _capacity = cstr_len + 1;
            _size = cstr_len;
            _data = new char[_capacity];

            // fill the data in
            for (size_t i; i < cstr_len ; ++i){
                _data[i] = cstr[i];
            }

            _data[_size] = '\0';

        }

        // Implementing a destructor since we are using dynamic mem
        ~string(){
            delete[] _data;
        }

        size_t size() const{
            return _size;
        }

        const char *cstr() const{
            return _data;
        }

        const char *begin() const{
            return _data;
        }

        const char *end() const{
            return _data + _size;
        }

        char &operator [](size_t i){
            return _data[i];
        }

        char &operator [](size_t i) const{
            return _data[i];
        }

        void operator+=(const string &other){
            size_t other_size = other.size();

            if (_size + other_size < _capacity){
                // no need to expand
                for (size_t i; i < other_size; ++i){
                    // Note: Not sure if this is best practice to access private of another instance
                    _data[_size+i] = other._data[i];
                }

                _data[_size+other_size] = '\0';
                _size += other_size;

                return;
            }

            // Need to reserve new _data
            // Reserve double until enough space
            // Note: Not sure if this reservation is best practive
            while (_capacity <= _size + other_size) {_capacity *= 2;}
            char* old_data = _data;
            _data = new char[_capacity];

            for (size_t i = 0; i < _size; ++i){
                _data[i] = old_data[i];
            }
            
            for (size_t i = 0; i < other_size; ++i){
                _data[_size+i] = other._data[i];
            }
            
            _data[_size+other_size] = '\0';
            _size += other_size;
            
            delete[] old_data;
        }

        void pushBack(char c){

            if (!_size + 1 == _capacity){
                _data[_size] = c;
                _data[_size+1] = '\0';
                ++_size;
                return;
            }

            _capacity *= 2;
            char* old_data = _data;
            _data = new char[_capacity];

            for (size_t i = 0; i < _size; ++i){
                _data[i] = old_data[i];
            }

            _data[_size] = c;
            _data[_size+1] = '\0';
            ++_size;
            delete[] old_data;

        }

        void popBack(){
            _data[_size-1] = '\0';
            --_size;
        }

        bool operator==(const string &other) const{
            if (_size != other.size()){return false;}

            for (size_t i = 0; i < _size; ++i){
                if (_data[i] != other._data[i]){return false;}
            }

            return true;
        }

        bool operator!=( const string &other ) const{
            return !(*this == other);
        }

        bool operator<( const string &other ) const{
            size_t smaller_size = _size;
            bool this_shorter = true;

            if (_size > other.size()){
                smaller_size = other.size();
                this_shorter = false;
            }

            for (size_t i=0; i < smaller_size; ++i){
                if (_data[i] < other._data[i]){
                    return true;
                }
                else if (_data[i] > other._data[i]){
                    return false;
                }
            }

            // equal up to shorter len
            return this_shorter;
        }

        bool operator>( const string &other ) const{
            return !(*this<other || *this==other);
        }

        bool operator<=( const string &other ) const{
            return (*this<other || *this==other);
        }

        bool operator>=( const string &other ) const{
            return (*this>other || *this==other);
        }

   private:
        char* _data;
        size_t _capacity;
        size_t _size;

};

std::ostream &operator<<( std::ostream &os, const string &s ){
    for (size_t i=0; i < s.size(); ++i){
        os << s[i];
    }
    return os;
}