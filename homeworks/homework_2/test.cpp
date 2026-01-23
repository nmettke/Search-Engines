#include <iostream>
#include "string.h"

void testStringClass() {
    string s1("Hello");
    string s2("World");
    string s3 = s1; // copy constructor
    s3 += s2; // operator+=
    std::cout << "s3: " << s3 << "\n"; // should print "HelloWorld"
    std::cout << "s3 size: " << s3.size() << "\n"; // should print 10

    s3.pushBack('!');
    std::cout << "s3 after pushBack: " << s3 << "\n"; // should print "HelloWorld!"

    s3.popBack();
    std::cout << "s3 after popBack: " << s3 << "\n"; // should print "HelloWorld"

    string a1("1234");
    string a2("1235");

    std::cout << "a1 == a2: " << (a1 == a2) << "\n"; // should print 0 (false)
    std::cout << "a1 != a2: " << (a1 != a2) << "\n"; // should print 1 (true)
    std::cout << "a1 < a2: " << (a1 < a2) << "\n"; // should print 1 (true)
    std::cout << "a1 > a2: " << (a1 > a2) << "\n"; // should print 0 (false)
}


int main() {
    testStringClass();
    return 0;
}
