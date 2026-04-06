#pragma once
#include <cstddef>
#include <new>
#include <utility>
template <typename T> class vector {
  public:
    // Default Constructor
    // REQUIRES: Nothing
    // MODIFIES: *this
    // EFFECTS: Constructs an empty vector with capacity 0
    vector() : data_(nullptr), size_(0), capacity_(0) {}

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

    vector(std::initializer_list<T> init)
        : data_(nullptr), size_(init.size()), capacity_(init.size()) {
        data_ = allocate(capacity_);

        T *p = data_;
        for (const T &value : init) {
            new (p) T(value);
            ++p;
        }
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
    vector &operator=(const vector<T> &other) {
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
    vector &operator=(vector<T> &&other) {
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
            new (dst) T(std::move(*src));
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
    // MODIFIES: Nothing
    // EFFECTS: Returns a pointer to the underlying element array (may be nullptr if empty)
    T *data() { return data_; }

    // REQUIRES: Nothing
    // MODIFIES: Nothing
    // EFFECTS: Returns a const pointer to the underlying element array
    const T *data() const { return data_; }

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

    void pushBack(T &&x) {
        if (size_ == capacity_) {
            reserve(capacity_ == 0 ? 8 : capacity_ * 3);
        }

        new (data_ + size_) T(std::move(x));
        size_++;
    }

    void push_back(const T &x) {
        if (size_ == capacity_) {
            reserve(capacity_ == 0 ? 8 : capacity_ * 3);
        }

        new (data_ + size_) T(x);
        size_++;
    }

    void push_back(T &&x) {
        if (size_ == capacity_) {
            reserve(capacity_ == 0 ? 8 : capacity_ * 3);
        }

        new (data_ + size_) T(std::move(x));
        size_++;
    }

    // TODO
    template <typename... Args> void emplaceBack(Args &&...args) {
        if (size_ == capacity_)
            reserve(capacity_ == 0 ? 8 : capacity_ * 3);

        new (data_ + size_) T(std::forward<Args>(args)...);
        size_++;
    }

    // REQUIRES: begin() <= pos <= end()
    // MODIFIES: this, size( ), capacity( ) if reallocation is needed
    // EFFECTS: Inserts a copy of x before the element at pos (or at end if pos == end()),
    //    returns a pointer to the new element
    T *insert(T *pos, const T &x) {
        const size_t index = static_cast<size_t>(pos - begin());
        if (index > size_)
            return end();
        if (index == size_) {
            pushBack(x);
            return data_ + index;
        }
        if (size_ == capacity_)
            reserve(capacity_ == 0 ? 8 : capacity_ * 3);

        const size_t old_size = size_;
        new (data_ + old_size) T(std::move(data_[old_size - 1]));
        for (ptrdiff_t i = static_cast<ptrdiff_t>(old_size) - 1; i > static_cast<ptrdiff_t>(index);
             --i) {
            data_[static_cast<size_t>(i)] = std::move(data_[static_cast<size_t>(i) - 1]);
        }
        data_[index] = x;
        ++size_;
        return data_ + index;
    }

    // REQUIRES: begin() <= pos <= end()
    // MODIFIES: this, size( ), capacity( ) if reallocation is needed
    // EFFECTS: Inserts x before pos using move semantics where supported
    T *insert(T *pos, T &&x) {
        const size_t index = static_cast<size_t>(pos - begin());
        if (index > size_)
            return end();
        if (index == size_) {
            emplaceBack(std::move(x));
            return data_ + index;
        }
        if (size_ == capacity_)
            reserve(capacity_ == 0 ? 8 : capacity_ * 3);

        const size_t old_size = size_;
        new (data_ + old_size) T(std::move(data_[old_size - 1]));
        for (ptrdiff_t i = static_cast<ptrdiff_t>(old_size) - 1; i > static_cast<ptrdiff_t>(index);
             --i) {
            data_[static_cast<size_t>(i)] = std::move(data_[static_cast<size_t>(i) - 1]);
        }
        data_[index] = std::move(x);
        ++size_;
        return data_ + index;
    }

    // REQUIRES: begin() <= pos <= end(), [first, last) is a valid range
    // MODIFIES: this, size( ), capacity( ) if reallocation is needed
    // EFFECTS: Inserts copies of the elements in [first, last) before pos;
    //    returns a pointer to the first inserted element (or pos if the range is empty)
    template <typename Iter> T *insert(T *pos, Iter first, Iter last) {
        const size_t index = static_cast<size_t>(pos - begin());
        if (index > size_)
            return end();
        size_t count = 0;
        for (Iter it = first; it != last; ++it)
            ++count;
        if (count == 0)
            return data_ + index;

        if (index == size_) {
            for (Iter it = first; it != last; ++it)
                pushBack(*it);
            return data_ + index;
        }

        if (size_ + count > capacity_) {
            size_t new_cap = capacity_;
            if (new_cap == 0)
                new_cap = 8;
            while (new_cap < size_ + count)
                new_cap *= 3;
            reserve(new_cap);
        }

        for (ptrdiff_t i = static_cast<ptrdiff_t>(size_) - 1; i >= static_cast<ptrdiff_t>(index);
             --i) {
            new (data_ + static_cast<size_t>(i) + count)
                T(std::move(data_[static_cast<size_t>(i)]));
            data_[static_cast<size_t>(i)].~T();
        }

        size_t j = index;
        for (Iter it = first; it != last; ++it, ++j) {
            new (data_ + j) T(*it);
        }
        size_ += count;
        return data_ + index;
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
    // MODIFIES: this, size( ), capacity( ) if count exceeds capacity( )
    // EFFECTS: Replaces all elements with count copies of value
    void assign(size_t count, const T &value) {
        destroyRange(begin(), end());
        size_ = 0;
        if (count > capacity_) {
            ::operator delete(data_);
            data_ = allocate(count);
            capacity_ = count;
        }
        for (size_t i = 0; i < count; ++i) {
            new (data_ + i) T(value);
        }
        size_ = count;
    }

    // REQUIRES: [first, last) is a valid range
    // MODIFIES: this, size( ), capacity( ) if the range length exceeds capacity( )
    // EFFECTS: Replaces all elements with copies of the elements in [first, last)
    template <typename Iter> void assign(Iter first, Iter last) {
        size_t count = 0;
        for (Iter it = first; it != last; ++it) {
            ++count;
        }
        destroyRange(begin(), end());
        size_ = 0;
        if (count > capacity_) {
            ::operator delete(data_);
            data_ = allocate(count);
            capacity_ = count;
        }
        size_t i = 0;
        for (Iter it = first; it != last; ++it, ++i) {
            new (data_ + i) T(*it);
        }
        size_ = count;
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

    bool empty() const { return size_ == 0; }

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
