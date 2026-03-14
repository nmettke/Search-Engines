#ifndef BLOOMFILTER_H
#define BLOOMFILTER_H

#include <vector>
#include <cmath>
#include <string>
#include <string.h>
#include <openssl/md5.h>

class Bloomfilter
   {
   public:
      Bloomfilter( int num_objects, double false_positive_rate )
         {
         // Determine the size of bits of our data vector, and resize.
         m_size_in_bits = -(num_objects * log(false_positive_rate)) / (pow(log(2), 2));
         v.resize(m_size_in_bits);

         // Determine number of hash functions to use.
         k_num_hash_function = m_size_in_bits / num_objects * log(2);
          
         }

      void insert( const std::string &s)
         {
         // Hash the string into two unique hashes.
         std::pair<uint64_t, uint64_t> result = hash(s);
         uint64_t h1 = result.first;
         uint64_t h2 = result.second;

         // Use double hashing to get unique bit, and repeat for each hash function.
         for(int i = 1; i<=k_num_hash_function; i++){
            size_t index = (h1 + i*h2) % v.size();
            v[index] = true;
            }
         }

      bool contains( const std::string &s )
         {
         // Hash the string into two unqiue hashes.

         std::pair<uint64_t, uint64_t> result = hash(s);
         uint64_t h1 = result.first;
         uint64_t h2 = result.second; 

         // Use double hashing to get unique bit, and repeat for each hash function.
         // If bit is false, we know for certain this unique string has not been inserted.
         
         // If all bits were true, the string is likely inserted, but false positive is possible.
         for(int i = 1; i<=k_num_hash_function; i++){
            size_t index = (h1 + i*h2) % v.size();
            if (v[index] == false) return false;
            }
         // This line is for compiling, replace this with own code.
         return true;
         }

   private:
      // Add any private member variables that may be neccessary.
      size_t m_size_in_bits;
      uint32_t k_num_hash_function;
      std::vector<bool> v;


      std::pair<uint64_t, uint64_t> hash( const std::string &datum )
         {
         //Use MD5 to hash the string into one 128 bit hash, and split into 2 hashes.
         uint64_t h1;
         uint64_t h2;

         unsigned char buffer[MD5_DIGEST_LENGTH];
         MD5((const unsigned char*)datum.c_str(), datum.size(), buffer);

         memcpy(&h1, buffer, 8);
         memcpy(&h2, buffer+8, 8);
         //This line is for compiling, replace this with own code.
         return {h1,h2};
         }
   };

#endif