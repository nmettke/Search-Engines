// flip.cpp
#include "Utf8.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <iostream>

using namespace std;

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

// function to read and write 1 character to a file
// code that 


void OutputBytesUtf16(Utf16 *buffer, Utf16 *end) {
    streamsize byteCount = (end - buffer) * sizeof(Utf16);
    cout.write(reinterpret_cast<const char*>(buffer), byteCount);
}

void OutputBytesUtf8(Utf8 *buffer, Utf8 *end) {
    streamsize byteCount = (end - buffer) * sizeof(Utf8);
    cout.write(reinterpret_cast<const char*>(buffer), byteCount);
}

int main(int argc, char* argv[]){
    // Check arguments
    if(argc != 2){
        printUsage();
        return 1;
    }


    // Open file in binary mode, (via lecture code below. See Lec5 Slide 39 for explanation)
    // We're using mapped files for loading in content, to avoid buffers

    int f = open( argv[ 1 ], O_RDONLY );

    if ( f != -1 ) {
    
        size_t fileSize = FileSize( f );
        const Utf8 *map = (Utf8 *)mmap( nullptr, fileSize, PROT_READ, MAP_PRIVATE, f, 0 );

        
        
        if ( map != MAP_FAILED ) {
            if (*map == 0xFF && *(map+1) == 0xFE){//UTF16, small endian

                const Utf16 *map16 = (Utf16 *)(map+2);
                const Utf16 *end = map16 + (fileSize-2) / sizeof(Utf16);

                Utf8 buffer[4096];
                Utf8 *newChars = buffer;
                Utf8 *bound = buffer + 4096;

                //While there's bytes left, read a byte via UTF16 and write as UTF8 (to stdout)
                while (map16 < end){
                    Unicode c = ReadUtf16(&map16, end);


                    Utf8 *nextChar = WriteUtf8(newChars, c, bound);
                    if (nextChar == newChars) { //if buffer is full, flush and write again
                        OutputBytesUtf8(buffer, newChars);
                        
                        newChars = buffer;
                        nextChar = WriteUtf8(newChars, c, bound);
                    }

                    newChars = nextChar;
                }
                OutputBytesUtf8(buffer, newChars);

            } else if(*map == 0xFE && *(map+1) == 0xFF){ //UTF16, bigEndian
                const Utf16 *map16 = (Utf16 *)(map+2);
                const Utf16 *end = map16 + (fileSize-2) / sizeof(Utf16);

                Utf8 buffer[4096];
                Utf8 *newChars = buffer;
                Utf8 *bound = buffer + 4096;

                //While there's bytes left, read a byte via UTF16 and write as UTF8 (to stdout)
                while (map16 < end){
                    
                    Unicode c = ReadUtf16((&map16), end);
                    
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
            else{ //UTF8!

                //Print BOM
                Utf16 bom = (Utf16) ByteOrderMark;
                cout.write(reinterpret_cast<const char*>(&bom), sizeof(bom));

                const Utf8 *end = map + fileSize;

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
        }
    } else{
        cout << "Unable to open file. Errno: " << errno;
    }
   
    close(f);

    return 0;
}