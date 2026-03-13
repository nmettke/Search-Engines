// TopN.cpp
// Nicole Hamilton nham@umich.edu

// Given a hashtable, return a dynamically-allocated array
// of pointers to the top N pairs based on their values.
// Caller is responsible for deleting the array.

// Individual pairs are to be C-strings as keys and size_t's
// as values.

#include "TopN.h"
#include "HashTable.h"

using namespace std;

using Hash = HashTable<const char *, size_t>;
using Pair = Tuple<const char *, size_t>;

Pair **TopN(Hash *hashtable, int N) {
    // Find the top N pairs based on the values and return
    // as a dynamically-allocated array of pointers.  If there
    // are less than N pairs in the hash, remaining pointers
    // will be null.

    Pair **result = new Pair *[N];
    for (int i = 0; i < N; i++) {
        result[i] = nullptr;
    }

    for (auto it = hashtable->begin(); it != hashtable->end(); ++it) {
        Pair *current = &(*it);

        // Skip if the array is full and current is not bigger than the smallest.
        if (result[N - 1] != nullptr && current->value <= result[N - 1]->value)
            continue;

        for (int i = 0; i < N; i++) {
            if (result[i] == nullptr || current->value > result[i]->value) {
                for (int j = N - 1; j > i; j--)
                    result[j] = result[j - 1];

                result[i] = current;
                break;
            }
        }
    }

    return result;
}
