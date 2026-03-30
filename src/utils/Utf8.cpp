// Utf8.cpp
#include "Utf8.h"

// Helper function to check if a Unicode value is a forbidden character (surrogates, noncharacters,
// etc.)
static inline bool IsForbiddenCharacter(Unicode c) {
    // UTF-16 surrogates: 0xD800 to 0xDFFF
    if (c >= 0xD800 && c <= 0xDFFF)
        return true;
    // Noncharacters: 0xFDD0 to 0xFDEF
    if (c >= 0xFDD0 && c <= 0xFDEF)
        return true;
    // 0xFFFE and 0xFFFF only in the BMP
    if (c == 0xFFFE || c == 0xFFFF)
        return true;
    return false;
}

// SizeOfUtf8 tells the number of bytes it will take to encode the specified value as Utf8. Assumes
// values over 31 bits will be replaced.
size_t SizeOfUtf8(Unicode c) {
    if (c <= 0x7F)
        return 1;
    if (c <= 0x7FF)
        return 2;
    if (c <= 0xFFFF)
        return 3;
    if (c <= 0x1FFFFF)
        return 4;
    if (c <= 0x3FFFFFF)
        return 5;
    if (c <= 0x7FFFFFFF)
        return 6;
    // Values over 31 bits will be replaced with replacement character (3 bytes)
    return 3;
}

// SizeOfUtf16 tells the number of bytes it will take to encode the specified value as Utf16.
size_t SizeOfUtf16(Unicode c) {
    if (c <= 0xFFFF)
        return 2;
    if (c <= 0x10FFFF)
        return 4; // Surrogate pair
    // Values > 0x10FFFF will be replaced (2 bytes)
    return 2;
}

// IndicatedLength looks at the first byte of a Utf8 sequence and determines the expected length.
// Return 1 for an invalid first byte.
size_t IndicatedLength(const Utf8 *p) {
    Utf8 b = *p;

    if ((b & 0x80) == 0)
        return 1; // 0xxxxxxx - ASCII
    if ((b & 0xE0) == 0xC0)
        return 2; // 110xxxxx
    if ((b & 0xF0) == 0xE0)
        return 3; // 1110xxxx
    if ((b & 0xF8) == 0xF0)
        return 4; // 11110xxx
    if ((b & 0xFC) == 0xF8)
        return 5; // 111110xx
    if ((b & 0xFE) == 0xFC)
        return 6; // 1111110x
    // Invalid first byte (continuation byte or 0xFF/0xFE)
    return 1;
}

// Read the next Utf8 character beginning at **pp and bump *pp past the end of the character.
Unicode ReadUtf8(const Utf8 **pp, const Utf8 *bound) {
    const Utf8 *p = *pp;

    if (bound && p >= bound)
        return 0;

    Utf8 b0 = p[0];

    if (b0 < 0x80) {
        *pp = p + 1;
        return b0;
    }

    if (b0 < 0xC0) {
        *pp = p + 1;
        return ReplacementCharacter;
    }

    if (b0 < 0xE0) {
        if (bound && p + 2 > bound) {
            *pp = bound;
            return ReplacementCharacter;
        }
        if ((p[1] & 0xC0) != 0x80) {
            *pp = p + 1;
            return ReplacementCharacter;
        }
        Unicode c = ((b0 & 0x1F) << 6) | (p[1] & 0x3F);
        *pp = p + 2;
        if (c < 0x80)
            return ReplacementCharacter;
        return c;
    }

    if (b0 < 0xF0) {
        if (bound && p + 3 > bound) {
            *pp = bound;
            return ReplacementCharacter;
        }
        if ((p[1] & 0xC0) != 0x80) {
            *pp = p + 1;
            return ReplacementCharacter;
        }
        if ((p[2] & 0xC0) != 0x80) {
            *pp = p + 2;
            return ReplacementCharacter;
        }
        Unicode c = ((b0 & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F);
        *pp = p + 3;
        if (c < 0x800)
            return ReplacementCharacter; // Overlong

        if (c >= 0xD800 && c <= 0xDFFF)
            return ReplacementCharacter;
        if (c >= 0xFDD0 && c <= 0xFDEF)
            return ReplacementCharacter;
        if (c == 0xFFFE || c == 0xFFFF)
            return ReplacementCharacter;
        return c;
    }

    if (b0 < 0xF8) {
        if (bound && p + 4 > bound) {
            *pp = bound;
            return ReplacementCharacter;
        }
        if ((p[1] & 0xC0) != 0x80) {
            *pp = p + 1;
            return ReplacementCharacter;
        }
        if ((p[2] & 0xC0) != 0x80) {
            *pp = p + 2;
            return ReplacementCharacter;
        }
        if ((p[3] & 0xC0) != 0x80) {
            *pp = p + 3;
            return ReplacementCharacter;
        }
        Unicode c =
            ((b0 & 0x07) << 18) | ((p[1] & 0x3F) << 12) | ((p[2] & 0x3F) << 6) | (p[3] & 0x3F);
        *pp = p + 4;
        if (c < 0x10000)
            return ReplacementCharacter;
        return c;
    }

    if (b0 < 0xFC) {
        if (bound && p + 5 > bound) {
            *pp = bound;
            return ReplacementCharacter;
        }
        for (int i = 1; i < 5; i++) {
            if ((p[i] & 0xC0) != 0x80) {
                *pp = p + i;
                return ReplacementCharacter;
            }
        }
        Unicode c = ((Unicode)(b0 & 0x03) << 24) | ((Unicode)(p[1] & 0x3F) << 18) |
                    ((Unicode)(p[2] & 0x3F) << 12) | ((p[3] & 0x3F) << 6) | (p[4] & 0x3F);
        *pp = p + 5;
        if (c < 0x200000)
            return ReplacementCharacter;
        return c;
    }

    if (b0 < 0xFE) {
        if (bound && p + 6 > bound) {
            *pp = bound;
            return ReplacementCharacter;
        }
        for (int i = 1; i < 6; i++) {
            if ((p[i] & 0xC0) != 0x80) {
                *pp = p + i;
                return ReplacementCharacter;
            }
        }
        Unicode c = ((Unicode)(b0 & 0x01) << 30) | ((Unicode)(p[1] & 0x3F) << 24) |
                    ((Unicode)(p[2] & 0x3F) << 18) | ((Unicode)(p[3] & 0x3F) << 12) |
                    ((p[4] & 0x3F) << 6) | (p[5] & 0x3F);
        *pp = p + 6;
        if (c < 0x4000000)
            return ReplacementCharacter;
        return c;
    }

    // 0xFE or 0xFF - invalid first byte
    *pp = p + 1;
    return ReplacementCharacter;
}

// Scan backward for the first PREVIOUS byte which could be the start of a UTF-8 character.
const Utf8 *PreviousUtf8(const Utf8 *p, const Utf8 *bound) {
    p--;

    if (bound && p < bound)
        return bound;

    if ((*p & 0xC0) != 0x80)
        return p;

    while (true) {
        if (bound && p - 1 < bound) {
            return bound;
        }

        p--;

        if ((*p & 0xC0) != 0x80) {
            return p;
        }
    }
}

// Write a Unicode character in UTF-8, returning one past the last byte that was written.
Utf8 *WriteUtf8(Utf8 *p, Unicode c, Utf8 *bound) {
    if (bound && p >= bound)
        return p;

    if (c <= 0x7F) {
        p[0] = (Utf8)c;
        return p + 1;
    }

    if (c <= 0x7FF) {
        if (bound && p + 2 > bound)
            return p;
        p[0] = (Utf8)(0xC0 | (c >> 6));
        p[1] = (Utf8)(0x80 | (c & 0x3F));
        return p + 2;
    }

    if (c <= 0xFFFF) {
        if (bound && p + 3 > bound)
            return p;
        p[0] = (Utf8)(0xE0 | (c >> 12));
        p[1] = (Utf8)(0x80 | ((c >> 6) & 0x3F));
        p[2] = (Utf8)(0x80 | (c & 0x3F));
        return p + 3;
    }

    if (c <= 0x1FFFFF) {
        if (bound && p + 4 > bound)
            return p;
        p[0] = (Utf8)(0xF0 | (c >> 18));
        p[1] = (Utf8)(0x80 | ((c >> 12) & 0x3F));
        p[2] = (Utf8)(0x80 | ((c >> 6) & 0x3F));
        p[3] = (Utf8)(0x80 | (c & 0x3F));
        return p + 4;
    }

    if (c <= 0x3FFFFFF) {
        if (bound && p + 5 > bound)
            return p;
        p[0] = (Utf8)(0xF8 | (c >> 24));
        p[1] = (Utf8)(0x80 | ((c >> 18) & 0x3F));
        p[2] = (Utf8)(0x80 | ((c >> 12) & 0x3F));
        p[3] = (Utf8)(0x80 | ((c >> 6) & 0x3F));
        p[4] = (Utf8)(0x80 | (c & 0x3F));
        return p + 5;
    }

    if (c <= 0x7FFFFFFF) {
        if (bound && p + 6 > bound)
            return p;
        p[0] = (Utf8)(0xFC | (c >> 30));
        p[1] = (Utf8)(0x80 | ((c >> 24) & 0x3F));
        p[2] = (Utf8)(0x80 | ((c >> 18) & 0x3F));
        p[3] = (Utf8)(0x80 | ((c >> 12) & 0x3F));
        p[4] = (Utf8)(0x80 | ((c >> 6) & 0x3F));
        p[5] = (Utf8)(0x80 | (c & 0x3F));
        return p + 6;
    }

    if (bound && p + 3 > bound)
        return p;
    p[0] = 0xEF;
    p[1] = 0xBF;
    p[2] = 0xBD;
    return p + 3;
}

// Read a Utf16 little-endian character beginning at **pp and bump *pp past the end of the
// character.
Unicode ReadUtf16(const Utf16 **pp, const Utf16 *bound) {
    const Utf16 *p = *pp;

    if (bound && p >= bound)
        return 0;

    Utf16 first = p[0];

    if (first < 0xD800 || first > 0xDFFF) {
        *pp = p + 1;
        return first;
    }

    if (first <= 0xDBFF) {
        if (bound && p + 1 >= bound) {
            *pp = p + 1;
            return first;
        }
        Utf16 second = p[1];
        if (second >= 0xDC00 && second <= 0xDFFF) {
            *pp = p + 2;
            return 0x10000 + (((Unicode)(first - 0xD800) << 10) | (second - 0xDC00));
        }
        *pp = p + 1;
        return first;
    }

    // Check for unpaired low surrogate (0xDC00 - 0xDFFF)
    if (first >= 0xDC00 && first <= 0xDFFF) {
        *pp = p + 1;
        return first;
    }

    *pp = p + 1;
    return first;
}

// Write a Unicode character in UTF-16, returning one past the last byte that was written.
Utf16 *WriteUtf16(Utf16 *p, Unicode c, Utf16 *bound) {
    if (bound && p >= bound)
        return p;

    if (c > 0x10FFFF)
        c = ReplacementCharacter;

    if (c <= 0xFFFF) {
        *p++ = (Utf16)c;
    } else {
        if (bound && p + 1 >= bound)
            return p;

        c -= 0x10000;
        *p++ = (Utf16)(0xD800 + (c >> 10));   // High surrogate
        *p++ = (Utf16)(0xDC00 + (c & 0x3FF)); // Low surrogate
    }

    return p;
}

// Wrappers that read the character but don't advance the pointer.
Unicode GetUtf8(const Utf8 *p, const Utf8 *bound) { return ReadUtf8(&p, bound); }

Unicode GetUtf16(const Utf16 *p, const Utf16 *bound) { return ReadUtf16(&p, bound); }

// Wrappers that advance the pointer but throw away the value.
const Utf8 *NextUtf8(const Utf8 *p, const Utf8 *bound) {
    ReadUtf8(&p, bound);
    return p;
}

const Utf16 *NextUtf16(const Utf16 *p, const Utf16 *bound) {
    ReadUtf16(&p, bound);
    return p;
}