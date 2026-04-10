#include "content_features.h"

double contentWordCountScore(uint32_t word_count) {
    if (word_count <= 50) {
        return (word_count / 50.0) * 0.3;
    }
    if (word_count <= 200) {
        return 0.3 + ((word_count - 50) / 150.0) * 0.7;
    }
    if (word_count <= 2000) {
        return 1.0;
    }
    if (word_count <= 10000) {
        return 1.0 - ((word_count - 2000) / 8000.0) * 0.5;
    }
    return 0.5;
}

double contentTitleScore(uint16_t title_word_count) {
    if (title_word_count == 0) {
        return 0.0;
    }
    if (title_word_count <= 2) {
        return 0.5;
    }
    if (title_word_count <= 10) {
        return 1.0;
    }
    if (title_word_count <= 15) {
        return 0.7;
    }
    return 0.3;
}
