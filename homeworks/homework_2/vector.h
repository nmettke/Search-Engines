// vector.h
//
// Starter file for a vector template

#pragma once
#include <cstddef>
template <typename T>
class vector
{
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
   ~vector()
   {
      delete[] data_;
   }

   // Resize Constructor
   // REQUIRES: Nothing
   // MODIFIES: *this
   // EFFECTS: Constructs a vector with size num_elements,
   //    all default constructed
   vector(size_t num_elements)
   {
      size_ = num_elements;
      capacity_ = num_elements;
      data_ = new T[num_elements];
   }

   // Fill Constructor
   // REQUIRES: Capacity > 0
   // MODIFIES: *this
   // EFFECTS: Creates a vector with size num_elements, all assigned to val
   vector(size_t num_elements, const T &val)
   {
      size_ = num_elements;
      capacity_ = num_elements;
      data_ = new T[num_elements];
      T *p = data_;
      T *e = data_ + num_elements;
      for (; p != e; ++p)
      {
         *p = val;
      }
   }

   // Copy Constructor
   // REQUIRES: Nothing
   // MODIFIES: *this
   // EFFECTS: Creates a clone of the vector other
   vector(const vector<T> &other) : data_(nullptr), size_(other.size_), capacity_(other.capacity_)
   {
      data_ = new T[capacity_];
      T *p = data_;
      T *q = other.data_;
      const T *q_end = other.data_ + other.size_;

      for (; q != q_end; ++q)
      {
         *p++ = *q;
      }
   }

   // Assignment operator
   // REQUIRES: Nothing
   // MODIFIES: *this
   // EFFECTS: Duplicates the state of other to *this
   vector operator=(const vector<T> &other)
   {
      delete[] data_;

      size_ = other.size_;
      capacity_ = other.capacity_;
      data_ = new T[capacity_];

      T *p = data_;
      const T *q = other.data_;
      const T *e = other.data_ + other.size_;

      for (; q != e; ++q)
      {
         *p++ = *q;
      }

      return *this;
   }

   // Move Constructor
   // REQUIRES: Nothing
   // MODIFIES: *this, leaves other in a default constructed state
   // EFFECTS: Takes the data from other into a newly constructed vector
   vector(vector<T> &&other)
   {
      data_ = other.data_;
      capacity_ = other.capacity_;
      size_ = other.size_;

      other.data_ = nullptr;
      other.capacity_ = 0;
      other.size_ = 0;
   }

   // Move Assignment Operator
   // REQUIRES: Nothing
   // MODIFIES: *this, leaves otherin a default constructed state
   // EFFECTS: Takes the data from other in constant time
   vector operator=(vector<T> &&other)
   {
      delete[] data_;

      data_ = other.data_;
      capacity_ = other.capacity_;
      size_ = other.size_;

      other.data_ = nullptr;
      other.capacity_ = 0;
      other.size_ = 0;
   }

   // REQUIRES: new_capacity > capacity( )
   // MODIFIES: capacity( )
   // EFFECTS: Ensures that the vector can contain size( ) = new_capacity
   //    elements before having to reallocate
   void reserve(size_t newCapacity)
   {
      T *tmp = new T[newCapacity];
      T *start = tmp;
      T *end = tmp + size_;
      T *it_ptr = data_;

      for (; start != end; ++start)
      {
         *start = *it_ptr++;
      }

      delete[] data_;
      data_ = tmp;
      capacity_ = newCapacity;
   }

   // REQUIRES: Nothing
   // MODIFIES: Nothing
   // EFFECTS: Returns the number of elements in the vector
   size_t size() const
   {
      return size_;
   }

   // REQUIRES: Nothing
   // MODIFIES: Nothing
   // EFFECTS: Returns the maximum size the vector can attain before resizing
   size_t capacity() const
   {
      return capacity_;
   }

   // REQUIRES: 0 <= i < size( )
   // MODIFIES: Allows modification of data[i]
   // EFFECTS: Returns a mutable reference to the i'th element
   T &operator[](size_t i)
   {
      return data_[i];
   }

   // REQUIRES: 0 <= i < size( )
   // MODIFIES: Nothing
   // EFFECTS: Get a const reference to the ith element
   const T &operator[](size_t i) const
   {
      return data_[i];
   }

   // REQUIRES: Nothing
   // MODIFIES: this, size( ), capacity( )
   // EFFECTS: Appends the element x to the vector, allocating
   //    additional space if neccesary
   void pushBack(const T &x)
   {
      if (size_ == capacity_)
      {
         size_t new_capacity = capacity_ == 0 ? 1 : capacity_ * 2;
         T *tmp = new T[new_capacity];
         T *start = tmp;
         T *end = tmp + size_;
         T *data_ptr = data_;

         for (; start != end; ++start)
         {
            *start = *data_ptr++;
         }

         delete[] data_;
         data_ = tmp;
         capacity_ = new_capacity;
      }

      *(data_ + size_) = x;
      size_++;
   }

   // REQUIRES: Nothing
   // MODIFIES: this, size( )
   // EFFECTS: Removes the last element of the vector,
   //    leaving capacity unchanged
   void popBack()
   {
      size_--;
   }

   // REQUIRES: Nothing
   // MODIFIES: Allows mutable access to the vector's contents
   // EFFECTS: Returns a mutable random access iterator to the
   //    first element of the vector
   T *begin()
   {
      return data_;
   }

   // REQUIRES: Nothing
   // MODIFIES: Allows mutable access to the vector's contents
   // EFFECTS: Returns a mutable random access iterator to
   //    one past the last valid element of the vector
   T *end()
   {
      return data_ + size_;
   }

   // REQUIRES: Nothing
   // MODIFIES: Nothing
   // EFFECTS: Returns a random access iterator to the first element of the vector
   const T *begin() const
   {
      return data_;
   }

   // REQUIRES: Nothing
   // MODIFIES: Nothing
   // EFFECTS: Returns a random access iterator to
   //    one past the last valid element of the vector
   const T *end() const
   {
      return data_ + size_;
   }

private:
   T *data_;
   size_t size_;
   size_t capacity_;
   // TODO
};
