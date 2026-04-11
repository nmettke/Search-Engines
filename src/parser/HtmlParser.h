// HtmlParser.h
// Nicole Hamilton, nham@umich.edu

#pragma once

#include "HtmlTags.h"
#include "utils/Utf8.h"
#include "utils/string.hpp"
#include "utils/vector.hpp"
#include <cctype>
#include <cstring>
#include <stack>

// This is a simple HTML parser class.  Given a text buffer containing
// a presumed HTML page, the constructor will parse the text to create
// lists of words, title words and outgoing links found on the page.  It
// does not attempt to parse the entire the document structure.
//
// The strategy is to word-break at whitespace and HTML tags and discard
// most HTML tags.  Three tags require discarding everything between
// the opening and closing tag. Five tags require special processing.
//
// We will use the list of possible HTML element names found at
// https://developer.mozilla.org/en-US/docs/Web/HTML/Element +
// !-- (comment), !DOCTYPE and svg, stored as a table in HtmlTags.h.

// Here are the rules for recognizing HTML tags.
//
// 1. An HTML tag starts with either < if it's an opening tag or </ if it's
//    a closing token.  If it starts with < and ends with /> it is both.
//
// 2. The name of the tag must follow the < or </ immediately.  There can't
//    be any whitespace.
//
// 3. The name is terminated by whitespace, > or / and is case-insensitive.
//    The exception is <!--, which starts a comment and is not required
//    to be terminated.
//
// 4. If the name is terminated by whitepace, arbitrary text representing
//    various arguments may follow, terminated by a > or />.
//
// 5. If the name isn't on the list we recognize, we assume it's just
//    ordinary text.
//
// 6. Every token is taken as a word-break.
//
// 7. Most opening or closing tokens can simply be discarded.
//
// 8. <script>, <style>, and <svg> require discarding everything between the
//    opening and closing tag.  Unmatched closing tags are discarded.
//
// 9. <!--, <title>, <a>, <base> and <embed> require special processing.
//
//      <!-- is the beginng of a comment.  Everything up to the ending -->
//          is discarded.
//
//      <title> should cause all the words between the opening and closing
//          tags to be added to the titleWords vector rather than the default
//          words vector.  A closing </title> without an opening <title> is discarded.
//
//      <a> is expected to contain an href="...url..."> argument with the
//          URL inside the double quotes that should be added to the list
//          of links.  All the words in between the opening and closing tags
//          should be collected as anchor text associated with the link
//          in addition to being added to the words or titleWords vector,
//          as appropriate.  A closing </a> without an opening <a> is
//          discarded.
//
//     <base> may contain an href="...url..." parameter.  If present, it should
//          be captured as the base variable.  Only the first is recognized; any
//          others are discarded.
//
//     <embed> may contain a src="...url..." parameter.  If present, it should be
//          added to the links with no anchor text.

class Link {
  public:
    ::string URL;
    ::vector<::string> anchorText;

    Link(::string URL) : URL(URL) {}
};

class HtmlParser {
  public:
    ::vector<::string> words, titleWords;
    ::vector<Link> links;
    ::string base;

  private:
    // discard section tags
    std::stack<::string> openSections;
    bool inTitle = false;
    bool inAnchor = false;
    // current link being built
    Link *currentLink = nullptr;

    // Structural integrity signals for isBroken()
    bool sawBodyTag = false;
    bool sawCloseHtml = false;
    bool truncated = false;

    // Language detection: extracted from <html lang="...">
    ::string htmlLang;

    bool inDiscardSection() const { return !openSections.empty(); }

    // if the current position matches a specific closing tag -> return nullptr if not closing tag +
    // not the correct tag if not reutnr pointer to > + 1
    const char *matchClosingTag(const char *p, const char *end, const ::string &tagName) {

        // dont go past end this will fix it
        if (p + 2 + tagName.length() >= end) {
            return nullptr;
        }

        if (p[0] != '<' || p[1] != '/') {
            return nullptr;
        }

        // tag name shud match maybe do strncasecmp not sure
        if (strncasecmp(p + 2, tagName.cstr(), tagName.length()) != 0) {
            return nullptr;
        }

        // should be empty space then by > or only >
        const char *after = p + 2 + tagName.length();

        // skip spaces after tag name
        while (after < end && isspace(*after)) {
            after++;
        }
        if (after < end && *after == '>') {
            return after + 1;
        }

        return nullptr;
    }

    // add word to its container
    void addWord(const char *start, const char *end) {
        if (start >= end)
            return;

        // make the word

        ::string word(start, end);

        if (inTitle) {
            titleWords.push_back(word);
        } else {
            words.push_back(word);
        }

        if (inAnchor && currentLink) {
            currentLink->anchorText.push_back(word);
        }
    }

    // find the close
    // also hamdle quotes
    const char *findClose(const char *p, const char *end) {
        bool inQuote = false;
        char quoteChar = 0;
        // <div class="cell small-12 medium-6 large-3  medium-order-1 small-order-2"">
        bool justExitedQuote = false;

        while (p < end) {
            if (inQuote) {
                if (*p == quoteChar) {
                    inQuote = false;
                    justExitedQuote = true;
                }
            } else {
                if (*p == '"' || *p == '\'') {
                    if (!justExitedQuote) {
                        inQuote = true;
                        quoteChar = *p;
                    }
                } else if (*p == '>') {
                    return p;
                }
                if (justExitedQuote && (isspace(*p) || *p == '=' || *p == '>')) {
                    justExitedQuote = false;
                }
            }
            p++;
        }
        return nullptr;
    }

    const char *handleComment(const char *p, const char *end) {
        // skip <!--
        p += 4;

        // search -->
        while (p + 2 < end) {
            if (p[0] == '-' && p[1] == '-' && p[2] == '>') {
                return p + 3;
            }
            p++;
        }

        // comment never closes so just ignore everything after
        return end;
    }

    void handleDiscardSection(const char *tagStart, const char *tagEnd, bool isClosing,
                              bool isSelfClosing) {
        // <script/> type
        if (isSelfClosing) {
            return;
        }
        if (isClosing) {
            if (!openSections.empty()) {
                openSections.pop();
            }
        } else {
            // store tag name -. changed to lowercase for all
            ::string tagName;
            for (const char *p = tagStart; p < tagEnd; p++) {
                tagName += tolower(*p);
            }
            openSections.push(tagName);
        }
    }

    // anchor tag
    void handleAnchor(const char *attrStart, const char *attrEnd, bool isClosing,
                      bool isSelfClosing) {
        // closing anchor tag
        if (isClosing) {
            inAnchor = false;
            currentLink = nullptr;
            return;
        }

        ::string href = extractAttribute(attrStart, attrEnd, "href");
        if (!href.empty()) {
            // anchor with valid href closes prev anchor
            if (inAnchor) {
                inAnchor = false;
                currentLink = nullptr;
            }

            links.push_back(Link(href));

            if (!isSelfClosing) {
                inAnchor = true;
                currentLink = &links[links.size() - 1];
            }
        }
    }

    void handleBase(const char *attrStart, const char *attrEnd) {
        // only one base
        if (!base.empty())
            return;

        ::string href = extractAttribute(attrStart, attrEnd, "href");
        if (!href.empty()) {
            base = href;
        }
    }

    void handleEmbed(const char *attrStart, const char *attrEnd) {
        ::string src = extractAttribute(attrStart, attrEnd, "src");
        if (!src.empty()) {
            links.push_back(Link(src));
        }
    }

    ::string extractAttribute(const char *start, const char *end, const char *attrName) {
        size_t nameLen = strlen(attrName);

        for (const char *p = start; p + nameLen < end; p++) {

            if (strncasecmp(p, attrName, nameLen) == 0) {
                const char *afterName = p + nameLen;

                while (afterName < end && isspace(*afterName)) {
                    afterName++;
                }

                if (afterName >= end || *afterName != '=') {
                    continue;
                }
                afterName++;

                while (afterName < end && isspace(*afterName)) {
                    afterName++;
                }

                if (afterName >= end)
                    return "";
                char quote = *afterName;
                if (quote != '"' && quote != '\'') {

                    if (*afterName == '<') {
                        return "";
                    }

                    const char *valStart = afterName;

                    while (afterName < end && !isspace(*afterName) && *afterName != '>' &&
                           *afterName != '<') {
                        afterName++;
                    }
                    return ::string(valStart, afterName);
                }

                afterName++;

                // closing quote
                const char *valStart = afterName;
                while (afterName < end && *afterName != quote) {
                    afterName++;
                }
                if (afterName < end) {
                    return ::string(valStart, afterName);
                }
            }
        }

        return "";
    }

    // returns pointer past > if tag or the same position if not
    const char *processTag(const char *p, const char *end) {
        if (p >= end) {
            return p;
        }
        // if inside discard section, only look for closing tag
        if (inDiscardSection()) {
            const char *closePos = matchClosingTag(p, end, openSections.top());
            if (closePos) {
                openSections.pop();
                return closePos;
            }
            // discard this
            return p;
        }

        const char *tagStart = p + 1;
        bool isClosing = false;

        // check if closing tag
        if (tagStart < end && *tagStart == '/') {
            isClosing = true;
            tagStart++;
        }

        // find end of tag name could be whitespace, >, or /
        const char *tagEnd = tagStart;
        while (tagEnd < end && !isspace(*tagEnd) && *tagEnd != '>' && *tagEnd != '/') {
            tagEnd++;
        }

        // check for <>  </
        if (tagEnd == tagStart) {
            return p;
        }

        // comment can start without space
        if (tagEnd - tagStart >= 3 && tagStart[0] == '!' && tagStart[1] == '-' &&
            tagStart[2] == '-') {
            return handleComment(p, end);
        }

        DesiredAction action = LookupPossibleTag(tagStart, tagEnd);

        if (action == DesiredAction::OrdinaryText) {
            return p;
        }

        if (action == DesiredAction::Comment) {
            return handleComment(p, end);
        }

        const char *close = findClose(tagEnd, end);
        // just normal word
        if (!close) {
            return p;
        }

        bool isSelfClosing = (close > p && *(close - 1) == '/');

        switch (action) {
        case DesiredAction::DiscardSection:
            handleDiscardSection(tagStart, tagEnd, isClosing, isSelfClosing);
            break;
        case DesiredAction::Title:
            if (isClosing)
                inTitle = false;
            else if (!isSelfClosing)
                inTitle = true;
            break;
        case DesiredAction::Anchor:
            handleAnchor(tagEnd, close, isClosing, isSelfClosing);
            break;
        case DesiredAction::Base:
            handleBase(tagEnd, close);
            break;
        case DesiredAction::Embed:
            handleEmbed(tagEnd, close);
            break;
        case DesiredAction::Discard: {
            size_t tagLen = tagEnd - tagStart;
            if (tagLen == 4 && strncasecmp(tagStart, "body", 4) == 0 && !isClosing)
                sawBodyTag = true;
            if (tagLen == 4 && strncasecmp(tagStart, "html", 4) == 0) {
                if (isClosing)
                    sawCloseHtml = true;
                else if (htmlLang.empty())
                    htmlLang = extractAttribute(tagEnd, close, "lang");
            }
            break;
        }
        default:
            break;
        }

        return close + 1;
    }

  public:
    HtmlParser() = default;

    // The constructor is given a buffer and length containing
    // presumed HTML.  It will parse the buffer, stripping out
    // all the HTML tags and producing the list of words in body,
    // words in title, and links found on the page.

    HtmlParser(const char *buffer, size_t length) {
        const char *p = buffer;
        const char *end = buffer + length;
        // start of the current word
        const char *wordStart = nullptr;

        while (p < end) {

            if (*p == '<') {
                // save old state
                bool wasInTitle = inTitle;
                bool wasInAnchor = inAnchor;
                Link *wasCurrentLink = currentLink;
                bool wasInDiscard = inDiscardSection();

                // try to find a tag
                const char *tagStart = p;
                const char *newP = processTag(p, end);

                if (newP != tagStart) {
                    // put last word to correct place
                    if (wordStart && !wasInDiscard) {

                        bool savedTitle = inTitle;
                        bool savedAnchor = inAnchor;
                        Link *savedLink = currentLink;
                        inTitle = wasInTitle;
                        inAnchor = wasInAnchor;
                        currentLink = wasCurrentLink;

                        addWord(wordStart, p);

                        // Restore new state
                        inTitle = savedTitle;
                        inAnchor = savedAnchor;
                        currentLink = savedLink;
                    }
                    wordStart = nullptr;
                    p = newP;
                } else {
                    // simple character
                    if (!wordStart && !wasInDiscard) {
                        wordStart = p;
                    }
                    p++;
                }
            } else if (isspace(static_cast<unsigned char>(*p))) {
                if (wordStart && !inDiscardSection()) {
                    addWord(wordStart, p);
                }
                wordStart = nullptr;
                p++;
            } else {
                if (!wordStart && !inDiscardSection()) {
                    wordStart = p;
                }
                p++;
            }
        }
        if (wordStart && !inDiscardSection()) {
            addWord(wordStart, end);
        }

        // Check for truncation: scan backward for last '<' and see if it was closed
        if (length > 0) {
            for (const char *scan = end - 1; scan >= buffer; --scan) {
                if (*scan == '>')
                    break;
                if (*scan == '<') {
                    truncated = true;
                    break;
                }
            }
        }
    }

    HtmlParser(std::initializer_list<::string> words, std::initializer_list<::string> titleWords,
               std::initializer_list<Link> links, ::string base)
        : words(words), titleWords(titleWords), links(links), base(std::move(base)) {}

    // Returns true if page has too many broken-HTML signals to be worth indexing.
    // Fires when 2+ of 5 signals are present to avoid false positives.
    bool isBroken() const {
        int score = 0;
        if (!openSections.empty())
            ++score; // unclosed <script>/<style>/<svg>
        if (words.size() < 20)
            ++score; // near-empty page
        if (!sawBodyTag)
            ++score; // no <body> tag found
        if (!sawCloseHtml)
            ++score; // no </html> — likely truncated
        if (truncated)
            ++score; // buffer ends mid-tag
        return score >= 2;
    }

    // Returns true if the page text is predominantly Latin-script (English).
    // Uses ReadUtf8 from Utf8.h to decode words into Unicode codepoints,
    // then checks ratio of Latin alphabetic chars to total alphabetic chars.
    bool isEnglish(double threshold = 0.6) const;
};
