// src/lib/tokenizer.h
#pragma once

// #include "html_parser.h"
#include "parser/HtmlParser.h"
#include "types.h"

#include "utils/STL_rewrite/pair.hpp"
#include "utils/string.hpp"
#include "utils/vector.hpp"
#include <array>

struct TokenizedDocument {
    ::vector<TokenOutput> tokens;
    DocEndOutput doc_end;
};

class Tokenizer {
  public:
    Tokenizer(uint32_t base_location = 0) : next_location(base_location) {}
    TokenizedDocument processDocument(const HtmlParser &doc);
    ::vector<::string> processToken(const ::string &raw);

  private:
    static ::string makeLowerCase(::string s);
    static ::string stripPunc(const ::string &s);
    static bool isAlphaNumeric(unsigned char c);

    uint32_t next_location = 0;
};

class PorterStemmer {
    // following https://vijinimallawaarachchi.com/2017/05/09/porter-stemming-algorithm/
  public:
    static ::string stem(const ::string &token) {
        if (token.length() <= 2)
            return token;

        ::string w = token;

        step1(w);
        if (w.length() > 2) {
            step2(w);
            step3(w);
            step4(w);
            step5(w);
        }

        return w;
    }

  private:
    static constexpr bool isAsciiVowel(char c) {
        return c == 'a' || c == 'e' || c == 'i' || c == 'o' || c == 'u';
    }

    static bool ends_with(::string_view value, ::string_view ending) {
        if (ending.size() > value.size())
            return false;
        return value.compare(value.size() - ending.size(), ending.size(), ending) == 0;
    }

    static inline bool isVowel(::string_view w, size_t i) {
        char c = w[i];
        if (c == 'a' || c == 'e' || c == 'i' || c == 'o' || c == 'u')
            return true;
        if (c == 'y')
            return (i == 0) ? false : !isVowel(w, i - 1);
        return false;
    }

    static int getMeasure(::string_view w) {
        int m = 0;
        bool prev_is_vowel = false;
        bool in_vowel_run = false;
        for (size_t i = 0; i < w.size(); ++i) {
            const char c = w[i];
            const bool is_vowel = isAsciiVowel(c) || (c == 'y' && i > 0 && !prev_is_vowel);
            if (is_vowel) {
                in_vowel_run = true;
            } else if (in_vowel_run) {
                m++;
                in_vowel_run = false;
            }
            prev_is_vowel = is_vowel;
        }
        return m;
    }

    static bool containsVowel(::string_view w) {
        bool prev_is_vowel = false;
        for (size_t i = 0; i < w.size(); ++i) {
            const char c = w[i];
            const bool is_vowel = isAsciiVowel(c) || (c == 'y' && i > 0 && !prev_is_vowel);
            if (is_vowel)
                return true;
            prev_is_vowel = is_vowel;
        }
        return false;
    }

    static bool endsWithDoubleConsonant(::string_view w) {
        if (w.length() < 2)
            return false;
        return (w[w.length() - 1] == w[w.length() - 2] && !isVowel(w, w.length() - 1));
    }

    static bool ends_cvc(::string_view w) {
        size_t n = w.length();
        if (n < 3)
            return false;
        char c = w[n - 1];
        if (isVowel(w, n - 1) || !isVowel(w, n - 2) || isVowel(w, n - 3))
            return false;
        return (c != 'w' && c != 'x' && c != 'y');
    }

    static void step1(::string &w) {
        if (ends_with(w, "sses"))
            w.erase(w.length() - 2);
        else if (ends_with(w, "ies"))
            w.replace(w.length() - 3, 3, "i");
        else if (ends_with(w, "s") && !ends_with(w, "ss"))
            w.pop_back();

        bool do_cleanup = false;
        if (ends_with(w, "eed")) {
            if (getMeasure(::string_view(w).substr(0, w.length() - 3)) > 0)
                w.pop_back();
        } else if (ends_with(w, "ed")) {
            if (containsVowel(::string_view(w).substr(0, w.length() - 2))) {
                w.erase(w.length() - 2);
                do_cleanup = true;
            }
        } else if (ends_with(w, "ing")) {
            if (containsVowel(::string_view(w).substr(0, w.length() - 3))) {
                w.erase(w.length() - 3);
                do_cleanup = true;
            }
        }

        if (do_cleanup) {
            if (ends_with(w, "at") || ends_with(w, "bl") || ends_with(w, "iz"))
                w += "e";
            else if (endsWithDoubleConsonant(w) && !ends_with(w, "l") && !ends_with(w, "s") &&
                     !ends_with(w, "z"))
                w.pop_back();
            else if (getMeasure(w) == 1 && ends_cvc(w))
                w += "e";
        }
        if (ends_with(w, "y") && containsVowel(::string_view(w).substr(0, w.length() - 1)))
            w[w.length() - 1] = 'i';
    }

    static void step2(::string &w) {
        static constexpr std::array<::pair<::string_view, ::string_view>, 20> subs = {{
            {"ational", "ate"}, {"tional", "tion"}, {"enci", "ence"},   {"anci", "ance"},
            {"izer", "ize"},    {"abli", "able"},   {"alli", "al"},     {"entli", "ent"},
            {"eli", "e"},       {"ousli", "ous"},   {"ization", "ize"}, {"ation", "ate"},
            {"ator", "ate"},    {"alism", "al"},    {"iveness", "ive"}, {"fulness", "ful"},
            {"ousness", "ous"}, {"aliti", "al"},    {"iviti", "ive"},   {"biliti", "ble"},
        }};
        for (const auto &[suffix, rep] : subs) {
            if (ends_with(w, suffix)) {
                if (getMeasure(::string_view(w).substr(0, w.length() - suffix.length())) > 0) {
                    w.replace(w.length() - suffix.length(), suffix.length(), rep.data(),
                              rep.length());
                }
                break;
            }
        }
    }

    static void step3(::string &w) {
        static constexpr std::array<::pair<::string_view, ::string_view>, 7> subs = {{
            {"icate", "ic"},
            {"ative", ""},
            {"alize", "al"},
            {"iciti", "ic"},
            {"ical", "ic"},
            {"ful", ""},
            {"ness", ""},
        }};
        for (const auto &[suffix, rep] : subs) {
            if (ends_with(w, suffix)) {
                if (getMeasure(::string_view(w).substr(0, w.length() - suffix.length())) > 0) {
                    w.replace(w.length() - suffix.length(), suffix.length(), rep.data(),
                              rep.length());
                }
                break;
            }
        }
    }

    static void step4(::string &w) {
        static constexpr std::array<::string_view, 18> suffixes = {
            "al",   "ance", "ence", "er",  "ic",  "able", "ible", "ant", "ement",
            "ment", "ent",  "ou",   "ism", "ate", "iti",  "ous",  "ive", "ize"};
        for (::string_view s : suffixes) {
            if (ends_with(w, s)) {
                if (getMeasure(::string_view(w).substr(0, w.length() - s.length())) > 1)
                    w.erase(w.length() - s.length());
                return;
            }
        }
        if (ends_with(w, "ion")) {
            ::string_view stem = ::string_view(w).substr(0, w.length() - 3);
            if (getMeasure(stem) > 1 && (ends_with(stem, "s") || ends_with(stem, "t")))
                w.erase(w.length() - 3);
        }
    }

    static void step5(::string &w) {
        int m = getMeasure(::string_view(w).substr(0, w.length() - 1));
        if (ends_with(w, "e") &&
            (m > 1 || (m == 1 && !ends_cvc(::string_view(w).substr(0, w.length() - 1)))))
            w.pop_back();
        if (getMeasure(w) > 1 && endsWithDoubleConsonant(w) && ends_with(w, "l"))
            w.pop_back();
    }
};
