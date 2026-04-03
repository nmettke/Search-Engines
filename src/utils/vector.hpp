#pragma once
#include <cstddef>
#include <utility>
template <typename T> class vector {
  public:
    // Default Constructor
    // REQUIRES: Nothing
    // MODIFIES: *this
    // EFFECTS: Constructs an empty vector with capacity 0
    vector() : data_(nullptr), size_(0), capacity_(0) {};

    // Destructor
    // REQUIRES: Nothing
    // MODIFIES: Destroys *this
    // EFFECTS: Performs any neccessary clean up operations
    ~vector() {
        destroyRange(begin(), end());
        ::operator delete(data_);
    }

    // Resize Constructor
    // REQUIRES: Nothing
    // MODIFIES: *this
    // EFFECTS: Constructs a vector with size num_elements,
    //    all default constructed
    vector(size_t num_elements) {
        size_ = num_elements;
        capacity_ = num_elements;
        data_ = static_cast<T *>(::operator new(sizeof(T) * capacity_));

        T *p = data_;
        T *e = data_ + size_;

        for (; p != e; ++p) {
            new (p) T();
        }
    }

    // Fill Constructor
    // REQUIRES: Capacity > 0
    // MODIFIES: *this
    // EFFECTS: Creates a vector with size num_elements, all assigned to val
    vector(size_t num_elements, const T &val)
        : data_(nullptr), size_(num_elements), capacity_(num_elements) {
        data_ = allocate(capacity_);

        T *p = data_;
        T *e = data_ + size_;

        for (; p != e; ++p)
            new (p) T(val);
    }

    // Copy Constructor
    // REQUIRES: Nothing
    // MODIFIES: *this
    // EFFECTS: Creates a clone of the vector other
    vector(const vector<T> &other) : data_(nullptr), size_(other.size_), capacity_(other.size_) {
        data_ = allocate(capacity_);
        copyRange(data_, other.data_, other.data_ + other.size_);
    }

    // Assignment operator
    // REQUIRES: Nothing
    // MODIFIES: *this
    // EFFECTS: Duplicates the state of other to *this
    vector operator=(const vector<T> &other) {
        if (this == &other)
            return *this;

        destroyRange(data_, data_ + size_);
        ::operator delete(data_);

        size_ = other.size_;
        capacity_ = other.size_;
        data_ = allocate(capacity_);

        copyRange(data_, other.data_, other.data_ + other.size_);

        return *this;
    }

    // Move Constructor
    // REQUIRES: Nothing
    // MODIFIES: *this, leaves other in a default constructed state
    // EFFECTS: Takes the data from other into a newly constructed vector
    vector(vector<T> &&other) { steal(other); }

    // Move Assignment Operator
    // REQUIRES: Nothing
    // MODIFIES: *this, leaves otherin a default constructed state
    // EFFECTS: Takes the data from other in constant time
    vector operator=(vector<T> &&other) {
        if (this == &other)
            return *this;

        destroyRange(data_, data_ + size_);
        ::operator delete(data_);

        steal(other);

        return *this;
    }

    // REQUIRES: new_capacity > capacity( )
    // MODIFIES: capacity( )
    // EFFECTS: Ensures that the vector can contain size( ) = new_capacity
    //    elements before having to reallocate
    void reserve(size_t newCapacity) {
        if (newCapacity <= capacity_)
            return;

        T *tmp = allocate(newCapacity);

        T *src = data_;
        T *dst = tmp;
        T *end = data_ + size_;

        for (; src != end; ++src, ++dst) {
            new (dst) T(*src);
            src->~T();
        }

        ::operator delete(data_);

        data_ = tmp;
        capacity_ = newCapacity;
    }

    // REQUIRES: Nothing
    // MODIFIES: Nothing
    // EFFECTS: Returns the number of elements in the vector
    size_t size() const { return size_; }

    // REQUIRES: Nothing
    // MODIFIES: Nothing
    // EFFECTS: Returns the maximum size the vector can attain before resizing
    size_t capacity() const { return capacity_; }

    // REQUIRES: 0 <= i < size( )
    // MODIFIES: Allows modification of data[i]
    // EFFECTS: Returns a mutable reference to the i'th element
    T &operator[](size_t i) { return data_[i]; }

    // REQUIRES: 0 <= i < size( )
    // MODIFIES: Nothing
    // EFFECTS: Get a const reference to the ith element
    const T &operator[](size_t i) const { return data_[i]; }

    // REQUIRES: Nothing
    // MODIFIES: this, size( ), capacity( )
    // EFFECTS: Appends the element x to the vector, allocating
    //    additional space if neccesary
    void pushBack(const T &x) {
        if (size_ == capacity_) {
            reserve(capacity_ == 0 ? 8 : capacity_ * 3);
        }

        new (data_ + size_) T(x);
        size_++;
    }

    // TODO
    template <typename... Args> void emplaceBack(Args &&...args) {
        if (size_ == capacity_)
            reserve(capacity_ == 0 ? 8 : capacity_ * 3);

        new (data_ + size_) T(std::forward<Args>(args)...);
        size_++;
    }

    // REQUIRES: Nothing
    // MODIFIES: this, size( )
    // EFFECTS: Removes the last element of the vector,
    //    leaving capacity unchanged
    void popBack() {
        data_[size_ - 1].~T();
        size_--;
    }

    // REQUIRES: Nothing
    // MODIFIES: Allows mutable access to the vector's contents
    // EFFECTS: Returns a mutable random access iterator to the
    //    first element of the vector
    T *begin() { return data_; }

    // REQUIRES: Nothing
    // MODIFIES: Allows mutable access to the vector's contents
    // EFFECTS: Returns a mutable random access iterator to
    //    one past the last valid element of the vector
    T *end() { return data_ + size_; }

    // REQUIRES: Nothing
    // MODIFIES: Nothing
    // EFFECTS: Returns a random access iterator to the first element of the vector
    const T *begin() const { return data_; }

    // REQUIRES: Nothing
    // MODIFIES: Nothing
    // EFFECTS: Returns a random access iterator to
    //    one past the last valid element of the vector
    const T *end() const { return data_ + size_; }

  private:
    T *data_;
    size_t size_;
    size_t capacity_;

    void copyRange(T *dest, const T *src, const T *src_end) {
        for (; src != src_end; ++src, ++dest)
            new (dest) T(*src);
    }

    void destroyRange(T *first, T *last) {
        for (; first != last; ++first)
            first->~T();
    }

    void steal(vector &other) {
        data_ = other.data_;
        size_ = other.size_;
        capacity_ = other.capacity_;

        other.data_ = nullptr;
        other.size_ = 0;
        other.capacity_ = 0;
    }

    T *allocate(size_t n) { return static_cast<T *>(::operator new(sizeof(T) * n)); }
};
