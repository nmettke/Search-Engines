// Utf8.cpp
#include "Utf8.h"

// Helper function to check if a Unicode value is a forbidden character (surrogates, noncharacters, etc.)
static bool IsForbiddenCharacter(Unicode c){
    // UTF-16 surrogates: 0xD800 to 0xDFFF
    if(c >= 0xD800 && c <= 0xDFFF){
        return true;
    }
    // Noncharacters: 0xFDD0 to 0xFDEF
    if(c >= 0xFDD0 && c <= 0xFDEF){
        return true;
    }
    // 0xFFFE and 0xFFFF only (per assignment spec)
    if(c == 0xFFFE || c == 0xFFFF){
        return true;
    }
    return false;
}

// SizeOfUtf8 tells the number of bytes it will take to encode the specified value as Utf8. Assumes values over 31 bits will be replaced.
size_t SizeOfUtf8(Unicode c){
    if(c <= 0x7F) return 1;
    if(c <= 0x7FF) return 2;
    if(c <= 0xFFFF) return 3;
    if(c <= 0x1FFFFF) return 4;
    if(c <= 0x3FFFFFF) return 5;
    if(c <= 0x7FFFFFFF) return 6;
    // Values over 31 bits will be replaced with replacement character (3 bytes)
    return 3;
}

// SizeOfUtf16 tells the number of bytes it will take to encode the specified value as Utf16.
size_t SizeOfUtf16(Unicode c){
    if(c <= 0xFFFF) return 2;
    if(c <= 0x10FFFF) return 4; // Surrogate pair
    // Values > 0x10FFFF will be replaced (2 bytes)
    return 2;
}

// IndicatedLength looks at the first byte of a Utf8 sequence and determines the expected length. Return 1 for an invalid first byte.
size_t IndicatedLength(const Utf8 *p){
    Utf8 b = *p;

    if((b & 0x80) == 0) return 1; // 0xxxxxxx - ASCII
    if((b & 0xE0) == 0xC0) return 2; // 110xxxxx
    if((b & 0xF0) == 0xE0) return 3; // 1110xxxx
    if((b & 0xF8) == 0xF0) return 4; // 11110xxx
    if((b & 0xFC) == 0xF8) return 5; // 111110xx
    if((b & 0xFE) == 0xFC) return 6; // 1111110x
    // Invalid first byte (continuation byte or 0xFF/0xFE)
    return 1;
}

// Read the next Utf8 character beginning at **pp and bump *pp past the end of the character.
Unicode ReadUtf8(const Utf8 **pp, const Utf8 *bound){
    const Utf8 *p = *pp;

    // Check if already at or past bound
    if(bound && p >= bound) return 0;

    Utf8 first = *p;
    size_t len = IndicatedLength(p);

    // Check if we have enough bytes
    if(bound && p + len > bound){
        // Not enough bytes so consume what we can and return replacement
        *pp = bound;
        return ReplacementCharacter;
    }

    Unicode c;

    // Decode based on length
    if(len == 1){
        if((first & 0x80) == 0){
            // Valid ASCII
            c = first;
            *pp = p + 1;
            return c;
        }
        else{
            // Invalid first byte (continuation byte)
            *pp = p + 1;
            return ReplacementCharacter;
        }
    }

    // Verify continuation bytes
    for(size_t i = 1; i < len; i++){
        if((p[i] & 0xC0) != 0x80){
            // Invalid continuation byte, consume only up to this point
            *pp = p + i;
            return ReplacementCharacter;
        }
    }

    // Decode the character
    if(len == 2){
        c = ((first & 0x1F) << 6) | (p[1] & 0x3F);
        // Check for overlong encoding
        if(c < 0x80){
            *pp = p + 2;
            return ReplacementCharacter;
        }
    }
    else if(len == 3){
        c = ((first & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F);
        // Check for overlong encoding
        if(c < 0x800){
            *pp = p + 3;
            return ReplacementCharacter;
        }
    }
    else if (len == 4){
        c = ((first & 0x07) << 18) | ((p[1] & 0x3F) << 12) | ((p[2] & 0x3F) << 6) | (p[3] & 0x3F);
        // Check for overlong encoding
        if(c < 0x10000){
            *pp = p + 4;
            return ReplacementCharacter;
        }
    }
    else if(len == 5){
        c = ((Unicode)(first & 0x03) << 24) | ((Unicode)(p[1] & 0x3F) << 18) | ((Unicode)(p[2] & 0x3F) << 12) | ((p[3] & 0x3F) << 6) | (p[4] & 0x3F);
        // Check for overlong encoding
        if (c < 0x200000) {
            *pp = p + 5;
            return ReplacementCharacter;
        }
    }
    else if (len == 6){
        c = ((Unicode)(first & 0x01) << 30) | ((Unicode)(p[1] & 0x3F) << 24) | ((Unicode)(p[2] & 0x3F) << 18) | ((Unicode)(p[3] & 0x3F) << 12) | ((p[4] & 0x3F) << 6) | (p[5] & 0x3F);
        // Check for overlong encoding
        if (c < 0x4000000) {
            *pp = p + 6;
            return ReplacementCharacter;
        }
    }
    else{
        *pp = p + 1;
        return ReplacementCharacter;
    }

    *pp = p + len;

    // Check for forbidden characters
    if(IsForbiddenCharacter(c)) return ReplacementCharacter;

    return c;
}

// Scan backward for the first PREVIOUS byte which could be the start of a UTF-8 character.
const Utf8 *PreviousUtf8(const Utf8 *p, const Utf8 *bound){
    // Move back at least one byte
    p--;

    // Check bound
    if(bound && p < bound)
        return bound;

    // Scan backward for a non-continuation byte (not 10xxxxxx) but limit to 6 bytes (max UTF-8 sequence length)
    int count = 0;
    while(count < 6 && (!bound || p >= bound) && (*p & 0xC0) == 0x80){
        p--;
        count++;
    }

    // Check bound again
    if(bound && p < bound) return bound;

    return p;
}

// Write a Unicode character in UTF-8, returning one past the last byte that was written.
Utf8 *WriteUtf8(Utf8 *p, Unicode c, Utf8 *bound){
    // Check if at or past bound
    if(bound && p >= bound) return p;

    // Handle values > 31 bits
    if(c > 0x7FFFFFFF) c = ReplacementCharacter;

    size_t len = SizeOfUtf8(c);

    // Check if we have enough space
    if (bound && p + len > bound) return p;

    if(len == 1){
        *p++ = (Utf8)c;
    }
    else if (len == 2){
        *p++ = (Utf8)(0xC0 | (c >> 6));
        *p++ = (Utf8)(0x80 | (c & 0x3F));
    }
    else if (len == 3){
        *p++ = (Utf8)(0xE0 | (c >> 12));
        *p++ = (Utf8)(0x80 | ((c >> 6) & 0x3F));
        *p++ = (Utf8)(0x80 | (c & 0x3F));
    }
    else if (len == 4){
        *p++ = (Utf8)(0xF0 | (c >> 18));
        *p++ = (Utf8)(0x80 | ((c >> 12) & 0x3F));
        *p++ = (Utf8)(0x80 | ((c >> 6) & 0x3F));
        *p++ = (Utf8)(0x80 | (c & 0x3F));
    }
    else if (len == 5){
        *p++ = (Utf8)(0xF8 | (c >> 24));
        *p++ = (Utf8)(0x80 | ((c >> 18) & 0x3F));
        *p++ = (Utf8)(0x80 | ((c >> 12) & 0x3F));
        *p++ = (Utf8)(0x80 | ((c >> 6) & 0x3F));
        *p++ = (Utf8)(0x80 | (c & 0x3F));
    }
    else if (len == 6){
        *p++ = (Utf8)(0xFC | (c >> 30));
        *p++ = (Utf8)(0x80 | ((c >> 24) & 0x3F));
        *p++ = (Utf8)(0x80 | ((c >> 18) & 0x3F));
        *p++ = (Utf8)(0x80 | ((c >> 12) & 0x3F));
        *p++ = (Utf8)(0x80 | ((c >> 6) & 0x3F));
        *p++ = (Utf8)(0x80 | (c & 0x3F));
    }

    return p;
}

// Read a Utf16 little-endian character beginning at **pp and bump *pp past the end of the character.
Unicode ReadUtf16(const Utf16 **pp, const Utf16 *bound){
    const Utf16 *p = *pp;

    // Check if at or past bound
    if(bound && p >= bound) return 0;

    Utf16 first = *p;

    // Check for high surrogate (0xD800 - 0xDBFF)
    if(first >= 0xD800 && first <= 0xDBFF){
        // Need a low surrogate to follow
        if(bound && p + 1 >= bound){
            // No room for low surrogate - return literal value
            *pp = p + 1;
            return first;
        }

        Utf16 second = p[1];

        // Check for valid low surrogate (0xDC00 - 0xDFFF)
        if(second >= 0xDC00 && second <= 0xDFFF){
            // Valid surrogate pair
            Unicode c = 0x10000 + (((Unicode)(first - 0xD800) << 10) | (second - 0xDC00));
            *pp = p + 2;
            return c;
        }

        // Unpaired high surrogate - return literal value
        *pp = p + 1;
        return first;
    }

    // Check for unpaired low surrogate (0xDC00 - 0xDFFF)
    if (first >= 0xDC00 && first <= 0xDFFF) {
        // Unpaired low surrogate - return literal value
        *pp = p + 1;
        return first;
    }

    // Regular BMP character
    *pp = p + 1;
    return first;
}

// Write a Unicode character in UTF-16, returning one past the last byte that was written.
Utf16 *WriteUtf16(Utf16 *p, Unicode c, Utf16 *bound){
    // Check if at or past bound
    if(bound && p >= bound) return p;

    // Handle values > 21 bits
    if(c > 0x10FFFF) c = ReplacementCharacter;

    if(c <= 0xFFFF){
        // BMP character, single 16-bit value
        *p++ = (Utf16)c;
    }
    else{
        // Need surrogate pair
        if(bound && p + 1 >= bound) return p; // Not enough space

        c -= 0x10000;
        *p++ = (Utf16)(0xD800 + (c >> 10)); // High surrogate
        *p++ = (Utf16)(0xDC00 + (c & 0x3FF)); // Low surrogate
    }

    return p;
}

// Wrappers that read the character but don't advance the pointer.
Unicode GetUtf8(const Utf8 *p, const Utf8 *bound){
    return ReadUtf8(&p, bound);
}

Unicode GetUtf16(const Utf16 *p, const Utf16 *bound){
    return ReadUtf16(&p, bound);
}

// Wrappers that advance the pointer but throw away the value.
const Utf8 *NextUtf8(const Utf8 *p, const Utf8 *bound){
    ReadUtf8(&p, bound);
    return p;
}

const Utf16 *NextUtf16(const Utf16 *p, const Utf16 *bound){
    ReadUtf16(&p, bound);
    return p;
}