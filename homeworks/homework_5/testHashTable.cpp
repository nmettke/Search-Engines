#include "HashTable.h"
#include "iostream"

using namespace std;

int main() {
    HashTable<int, int> table([](const int a, const int b) { return a == b; },
                              [](const int key) { return static_cast<uint64_t>(key); });

    table.Find(1, 10);
    table.Find(2, 20);

    cout << "Value for key 1: " << table.Find(1)->value << endl; // Should print 10
    cout << "Value for key 2: " << table.Find(2)->value << endl; // Should print 20

    if (table.Find(3) == nullptr) {
        cout << "Key 3 not found, as expected." << endl;
    }

    table.Find(1, 15)->value = 15; // Update value for key 1

    cout << "Updated value for key 1: " << table.Find(1)->value << endl; // Should print 15

    for (auto it = table.begin(); it != table.end(); ++it) {
        cout << "Key: " << it->key << ", Value: " << it->value << endl;
    }
    return 0;
}