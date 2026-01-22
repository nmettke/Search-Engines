// flip.cpp
#include "Utf8.h"
#include <iostream>

using namespace std;

// Print usage message to cerr
void printUsage() {
    cerr << "Usage: flip <filename>" << endl;
    cerr << "Recognize the input file as either Utf8 or Utf16, convert" << endl;
    cerr << "it to the opposite encoding, and write the result to stdout." << endl;
}

int main(int argc, char* argv[]){
    // Check arguments
    if(argc != 2){
        printUsage();
        return 1;
    }

    const char *filename = argv[1];

    // Open file in binary mode
    
    // Get file size

    // Read entire file into memory

    // Detect encoding by BOM

    return 0;
}