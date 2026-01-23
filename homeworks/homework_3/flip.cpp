// flip.cpp
#include "Utf8.h"
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <iostream>

using namespace std;

enum Encoding {
    UTF8_encoding,
    UTF16_encoding,
    UTF16BE_encoding,
};

Encoding identifyEncoding(const Utf8 *map){
    if (*map == 0xFF && *(map+1) == 0xFE){
        return UTF16_encoding;
    }
    if(*map == 0xFE && *(map+1) == 0xFF){
        return UTF16BE_encoding;
    }
    return UTF8_encoding;
}

// Print usage message to cerr
void printUsage() {
    cerr << "Usage: flip <filename>" << endl;
    cerr << "Recognize the input file as either Utf8 or Utf16, convert" << endl;
    cerr << "it to the opposite encoding, and write the result to stdout." << endl;
}

size_t FileSize( int f )
{
    struct stat fileInfo;
    fstat( f, &fileInfo );
    return fileInfo.st_size;
}


void OutputBytesUtf16(Utf16 *buffer, Utf16 *end) {
    streamsize byteCount = (end - buffer) * sizeof(Utf16);
    cout.write(reinterpret_cast<const char*>(buffer), byteCount);
}

void OutputBytesUtf8(Utf8 *buffer, Utf8 *end) {
    streamsize byteCount = (end - buffer) * sizeof(Utf8);
    cout.write(reinterpret_cast<const char*>(buffer), byteCount);
}


void Utf16To8(const Utf16 *map, const Utf16 *end){
    //buffers used to store output from writeutf8, then flush out to stdout periodically
    Utf8 buffer[4096];
    Utf8 *newChars = buffer;
    Utf8 *bound = buffer + 4096;

    //While there's bytes left, read a byte via UTF16 and write as UTF8 (to stdout)
    while (map < end){
        Unicode c = ReadUtf16(&map, end);


        Utf8 *nextChar = WriteUtf8(newChars, c, bound);
        if (nextChar == newChars) { //if buffer is full, flush and write again
            OutputBytesUtf8(buffer, newChars);
            
            newChars = buffer;
            nextChar = WriteUtf8(newChars, c, bound);
        }

        newChars = nextChar;
    }
    OutputBytesUtf8(buffer, newChars);
}

void Utf8To16(const Utf8 *map, const Utf8 *end){
    //buffers used to store output from writeutf8, then flush out to stdout periodically
    Utf16 buffer[4096];
    Utf16 *newChars = buffer;
    Utf16 *bound = buffer + 4096;

    //While there's bytes left, read a byte via UTF8 and write as UTF16 (to stdout)
    while (map < end){
        Unicode c = ReadUtf8(&map, end);
        
        Utf16 *nextChar = WriteUtf16(newChars, c, bound);
        if (nextChar == newChars) { //if buffer is full, flush and write again
            OutputBytesUtf16(buffer, newChars);
            
            newChars = buffer;
            nextChar = WriteUtf16(newChars, c, bound);
        }

        newChars = nextChar;
    }
    OutputBytesUtf16(buffer, newChars);
}

int main(int argc, char* argv[]){
    // Check arguments
    if(argc != 2){
        printUsage();
        return 1;
    }

    // We're using mapped files for loading in content, to avoid buffers (via lecture slides 05 p 39)

    int f = open( argv[ 1 ], O_RDONLY );
    if ( f == -1 ) {
        cerr << "Unable to open file. Errno: " << errno;
        close(f);
        return 1;
    }
    
    size_t fileSize = FileSize( f );

    const Utf8 *map = (Utf8 *)mmap( nullptr, fileSize, PROT_READ, MAP_PRIVATE, f, 0 );
    if ( map == MAP_FAILED ) {
        cerr << "Mappign failed";
        return 1;
    }

    //Which encoding is this file in?
    Encoding enc = identifyEncoding(map);

    if (enc == UTF16_encoding){ //little endian 16UTF convert to 8 case
        const Utf16 *map16 = (Utf16 *)(map+2);
        const Utf16 *end = map16 + (fileSize-2) / sizeof(Utf16);

        Utf16To8(map16, end);
    } 
    else if (enc == UTF16BE_encoding) { //Big endian 16UTF convert to 8 case
        const Utf16 *bigEndian = (Utf16*) (map + 2);
        size_t fileCount = (fileSize-2) / sizeof(Utf16);
        Utf16 *buf16 = new Utf16[fileCount];

        //Flipping endians
        for (size_t i = 0; i < fileCount; ++i) {
            buf16[i] = __builtin_bswap16(bigEndian[i]);
        }

        const Utf16 *map16 = buf16;
        const Utf16 *end = map16 + fileCount;

        Utf16To8(map16, end);

        delete[] buf16;
    } 
    else { //UTF8 to 16 case
        Utf16 bom = (Utf16) ByteOrderMark;
        cout.write(reinterpret_cast<const char*>(&bom), sizeof(bom));

        const Utf8 *end = map + fileSize;

        Utf8To16(map, end);
    }
            
    close(f);

    return 0;
}