// RobotsTxt.cpp
//
// Nicole Hamilton, nham@umich.edu

// This is a starter file for an implementation of the RobotsTxt class
// described in RobotsTxt.h.
//
// The RobotsTxt class can compile a robots.txt file and then determine whether
// access is allowed or disallowed for a given user agent and URL or path.  It
// will also extract any Sitemaps.

// This starter file a little more "paint by numbers" than I'd have liked but I
// wanted to keep the task reasonable.  And, it's only a suggestion. You do not
// have to use any of this code to pass the Autograder, so long as your
// implementation matches the interface definition in RobotsTxt.h.

#include "RobotsTxt.h"
#include <cctype>
#include <cstdlib>
#include <cstring>


//
// Utilities
//

class RobotsParsedUrl {
  private:
    Utf8 *pathBuffer;

  public:
    const Utf8 *CompleteUrl;
    Utf8 *Service, *Host, *Port, *Path;

    RobotsParsedUrl(const Utf8 *url) {
        // Assumes url points to static text but
        // does not check.

        CompleteUrl = url;

        pathBuffer = new Utf8[strlen((const char *)url) + 1];
        const Utf8 *f;
        Utf8 *t;
        for (t = pathBuffer, f = url; *t++ = *f++;)
            ;

        Service = pathBuffer;

        const Utf8 Colon = ':', Slash = '/';
        Utf8 *p;
        for (p = pathBuffer; *p && *p != Colon; p++)
            ;

        if (*p) {
            // Mark the end of the Service.
            *p++ = 0;

            if (*p == Slash)
                p++;
            if (*p == Slash)
                p++;

            Host = p;

            for (; *p && *p != Slash && *p != Colon; p++)
                ;

            if (*p == Colon) {
                // Port specified.  Skip over the colon and
                // the port number.
                *p++ = 0;
                Port = p;
                for (; *p && *p != Slash; p++)
                    ;
            } else
                Port = p;

            if (*p)
                // Mark the end of the Host and Port.
                *p++ = 0;

            // Whatever remains is the Path.
            Path = p;
        } else
            Host = Path = p;
    }

    ~RobotsParsedUrl() { delete[] pathBuffer; }
};

int HexLiteralCharacter(char c) {
    // If c contains the Ascii code for a hex character, return the
    // binary value; otherwise, -1.

    int i;

    if ('0' <= c && c <= '9')
        i = c - '0';
    else if ('a' <= c && c <= 'f')
        i = c - 'a' + 10;
    else if ('A' <= c && c <= 'F')
        i = c - 'A' + 10;
    else
        i = -1;

    return i;
}

Utf8 *UrlDecode(const Utf8 *path) {
    // Unencode any %xx encodings of characters in the path but
    // this variation leaves %2f "/" encodings unchanged.

    // (Unencoding can only shorten a string or leave it unchanged.
    // It never gets longer.)

    // Caller responsibility to delete [ ] the returned string.

    Utf8 *decode = new Utf8[strlen((const char *)path) + 1], *t = decode, c, d;

    while ((c = *path++) != 0)
        if (c == '%') {
            c = *path;
            if (c) {
                d = *++path;
                if (d) {
                    int i, j, hex;
                    i = HexLiteralCharacter(c);
                    j = HexLiteralCharacter(d);
                    hex = (i << 4 | j);
                    if (hex >= 0 && hex != '/') {
                        path++;
                        *t++ = (Utf8)hex;
                    } else {
                        // If the two characters following the %
                        // aren't both hex digits, or if it's a %2f
                        // encoding of a /, treat as literal text.

                        *t++ = '%';
                        path--;
                    }
                }
            }
        } else
            *t++ = c;

    // Terminate the string.
    *t = 0;

    return decode;
}

//
// Robots.txt syntax
//

// <robots.txt> ::= <Line> { <Line > }
// <Line>  ::= { <blank line> | '#" <abitrary text> | <Rule> | <Sitemap> } <LineEnd>
// <Rule> ::= <UserAgent> { <UserAgent> } <Directive> { <Directive> }
// <UserAgent> ::= "User-agent:" <name>
// <Directive> ::= <Permission> <path> | <CrawlDelay>
// <Permission> = "Allow:" | "Disallow:"
// <wildcard> ::= <a string that may contain *>
// <Crawl-delay> ::= "Crawl-delay:" <integer>
// <Sitemap> ::= "Sitemap:" <path>

//
// Tokenizing
//

const Utf8 UserAgentString[] = "user-agent:", AllowString[] = "allow:",
           DisallowString[] = "disallow:", CrawlDelayString[] = "crawl-delay:",
           SitemapString[] = "sitemap:";

enum StatementType {
    InvalidStatement = 0,
    UserAgentStatement,
    AllowStatement,
    DisallowStatement,
    CrawlDelayStatement,
    SitemapStatement
};

bool MatchKeyword(const Utf8 *keyword, const Utf8 **start, const Utf8 *bound) {
    // Try to match and consume this keyword from the input pointed to by
    // *start.  If there's a  match, advance the start pointer to the next input
    // character after the matched keyword and return true.
    //
    // keyword is null-terminated string already in lower-case to be matched
    // case-independent against the start of the input text.

    // YOUR CODE HERE.
    const Utf8 *p = *start;
    const Utf8 *k = keyword;

    while (*k != '\0') {
        if (p >= bound) {
            return false;
        }

        if ('A' <= *p && *p <= 'Z') {
            if (*p - 'A' + 'a' != *k) {
                return false;
            }
        } else if (*p != *k) {
            return false;
        }

        p++;
        k++;
    }

    *start = p;
    return true;
}

StatementType Identify(const Utf8 **start, const Utf8 *bound) {
    // Recognize User-agent, Allow, Disallow, Crawl-delay and
    // Sitemap statements beginning at *start.  Reject blank lines,
    // comments and invalid lines as Invalid.  If a match is found,
    // the text is consumed and *start is updated  to just past the
    // match.

    // YOUR CODE HERE.
    if (*start >= bound) {
        return InvalidStatement;
    }

    char beginC = **start;
    if ('A' <= beginC && beginC <= 'Z') {
        beginC = beginC - 'A' + 'a';
    }

    switch (beginC) {
    case 'u':
        if (MatchKeyword((const Utf8 *)"user-agent:", start, bound)) {
            return UserAgentStatement;
        }

        break;

    case 'a':
        if (MatchKeyword((const Utf8 *)"allow:", start, bound)) {
            return AllowStatement;
        }

        break;

    case 'd':
        if (MatchKeyword((const Utf8 *)"disallow:", start, bound)) {
            return DisallowStatement;
        }

        break;

    case 's':
        if (MatchKeyword((const Utf8 *)"sitemap:", start, bound)) {
            return SitemapStatement;
        }

        break;

    case 'c':
        if (MatchKeyword((const Utf8 *)"crawl-delay:", start, bound)) {
            return CrawlDelayStatement;
        }

        break;
    }

    return InvalidStatement;
}

Utf8 *GetArgument(const Utf8 **start, const Utf8 *bound) {
    // Extract the argument from the input by skipping white space,
    // then taking anything not white space.  Return it as a string
    // on the heap.  Caller responsibility to delete.
    //
    // *start is updated to just past the last character consumed.

    // YOUR CODE HERE.

    const Utf8 *p = *start;

    while (p < bound && (*p == ' ' || *p == '\t')) {
        ++p;
    }

    const Utf8 *argStart = p;

    while (p < bound && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n') {
        ++p;
    }
    size_t len = p - argStart;

    Utf8 *argument = new Utf8[len + 1];

    memcpy(argument, argStart, len);

    argument[len] = '\0';

    *start = p;
    return argument;
}

const Utf8 *NextLine(const Utf8 **start, const Utf8 *bound) {
    // Consume everything up to the beginning of the next line,
    // updating *start, and return a pointer to it.

    // YOUR CODE HERE.

    while (*start < bound && !(**start == '\r' || **start == '\n')) {
        (*start)++;
    }

    (*start)++;

    if (*start < bound && **start == '\n') {
        (*start)++;
    }

    return *start;
}

//
// Statement parsing and evaluation
//

StatementType GetStatement(const Utf8 **start, const Utf8 *bound, vector<string> &sitemap) {
    // Find the next valid User-agent, Allow, Disallow, or Crawl-delay
    // statement and return the statement type.  Return InvalidStatement if
    // none are found. At entry, assume *start is pointing at the beginning of
    // a line.
    //
    // Skip over any comments, blank lines, or invalid statements as
    // necessary.
    //
    // Sitemap statements are recognized here and the argument URL is added to
    // the sitemap vector, but otherwise skipped because they're global, and
    // don't affect how rules are parsed.

    // YOUR CODE HERE.
    while (*start < bound) {

        while (*start < bound && (**start == ' ' || **start == '\t'))
            (*start)++;

        if (*start >= bound || **start == '\r' || **start == '\n' || **start == '#') {
            NextLine(start, bound);
            continue;
        }

        StatementType type = Identify(start, bound);

        if (type == SitemapStatement) {
            Utf8 *arg = GetArgument(start, bound);
            sitemap.pushBack(string((const char *)arg));
            delete[] arg;
            NextLine(start, bound);
            continue;
        }

        if (type != InvalidStatement)
            return type;

        NextLine(start, bound);
    }

    return InvalidStatement;
}

const Utf8 *FindSubstring(const Utf8 *a, const Utf8 *b) {
    // Look for an occurrence of string a in string b using case-independent
    // compare.  If a match is found, return a pointer to where in b.  If no
    // match is found, return nullptr.

    // YOUR CODE HERE.
    if (!a || !*a)
        return b;
    if (!b || !*b)
        return nullptr;

    for (const char *s = (const char *)b; *s; s++) {
        const char *sa = (const char *)a, *sb = s;

        while (*sa && *sb) {
            char ca = *sa, cb = *sb;

            if ('A' <= ca && ca <= 'Z')
                ca = ca - 'A' + 'a';
            if ('A' <= cb && cb <= 'Z')
                cb = cb - 'A' + 'a';

            if (ca != cb)
                break;

            sa++;
            sb++;
        }

        if (!*sa) {
            return (const Utf8 *)s;
        }
    }

    return nullptr;
}

class UserAgent {
  public:
    Utf8 *name;
    bool matchAny;

    bool Match(const Utf8 *user) {
        // Return true if the name appears anywhere in the string
        // pointed to by user. Use case-independent compare.

        // YOUR CODE HERE.
        return (matchAny || FindSubstring(name, user) != nullptr);
    }

    UserAgent(const Utf8 **start, const Utf8 *bound) {
        // The name is the argument immediately following on
        // the input. Note if it's an * match-any wildcard..

        // YOUR CODE HERE.
        name = GetArgument(start, bound);
        matchAny = (name[0] == '*' && name[1] == '\0');
    }

    ~UserAgent() { delete[] name; }
};

class Wildcard {
  private:
    bool Match(const Utf8 *wildcard, const Utf8 *path) {
        // Return true if the path matches the wildcard.
        //
        // Assumes the original URL has been parsed, service, host,
        // port, and any initial slash have been stripped away.
        // path is what remains and has been URL decoded already.
        //
        // If with either wildcard or path is null, return false.

        // YOUR CODE HERE.
        if (!wildcard || !path || !*wildcard)
            return false;

        const Utf8 *wBack = nullptr, *pBack = nullptr;

        while (*path) {

            if (*wildcard == '*') {
                while (*wildcard == '*') {
                    wildcard++;
                }

                if (!wildcard) {
                    return !endsInDollarSign || !*path;
                }

                wBack = wildcard;
                pBack = path;

            } else if (*wildcard == *path) {
                wildcard++;
                path++;
                if (!*wildcard && !endsInDollarSign) {
                    return true;
                }

            } else if (wBack && *pBack) {
                wildcard = wBack;

                path = ++pBack;

            } else {

                return false;
            }
        }

        while (*wildcard == '*')
            wildcard++;

        return !*wildcard;
    }

  public:
    Utf8 *wildcard; // URL-decoded version of the argument
    size_t length, literalBytes;
    bool endsInDollarSign;

    Wildcard(const Utf8 **start, const Utf8 *bound) {
        // Create a wildcard using the argument immediately
        // following in the input stream.
        //
        // 1. Get the argument.
        // 2. Discard any initial / and create a URL-decoded version.
        // 3. Count how many literal bytes there are.
        // 4. Note the length and whether it ends in $.
        // 5. Discard the original version as no longer needed.

        // YOUR CODE HERE.

        Utf8 *arg = GetArgument(start, bound);
        const Utf8 *parg = arg;

        if (*parg == '/')
            parg++;

        bool needsDecoding = false;
        for (const Utf8 *checkDecode = parg; *checkDecode; checkDecode++) {
            if (*checkDecode == '%') {
                needsDecoding = true;
                break;
            }
        }

        if (needsDecoding) {
            wildcard = UrlDecode(parg);
            delete[] arg;
        } else if (parg == arg) {
            wildcard = arg;
        } else {
            size_t length = strlen((const char *)parg);
            memmove(arg, parg, length + 1);
            wildcard = arg;
        }

        length = strlen((const char *)wildcard);
        endsInDollarSign = length > 0 && wildcard[length - 1] == '$';

        if (endsInDollarSign) {
            wildcard[--length] = '\0';
        }

        literalBytes = 0;
        for (size_t i = 0; i < length; i++)
            if (wildcard[i] != '*')
                literalBytes++;
    }

    ~Wildcard() { delete[] wildcard; }

    bool Match(const Utf8 *path) { return Match(wildcard, path); }
};

bool MoreSpecific(const Wildcard *a, const Wildcard *b) {
    // Return true if a is more specific than b.

    return a->literalBytes > b->literalBytes ||
           a->literalBytes == b->literalBytes && a->endsInDollarSign && !b->endsInDollarSign;
}

class Directive {
  public:
    StatementType type;
    Wildcard wildcard;

    Directive *Match(const Utf8 *path) { return wildcard.Match(path) ? this : nullptr; }

    // If more than one directive applies based on the path, choose the one
    // which is
    //
    // 1. More specific, i.e., matches more characters in the URL, which we
    //    will measure as the number of literal (non-wildcard) bytes in the path.
    // 2. Ends in a $.
    // 3. Less restrictive, i.e., choose allow over disallow.
    // 4. Matches a literal User-Agent name, not just *.
    //
    // This means they can be sorted on a list when compiling a rule for a given
    // set of User-Agents; we can take the first one that matches.  But they
    // will still need to be compared if the match different User-agents.

    Directive(const StatementType type, const Utf8 **start, const Utf8 *bound)
        : type(type), wildcard(start, bound) {}

    ~Directive() {}
};

bool TakesPriority(const Directive *a, const Directive *b) {
    // Return true if a takes priority over b, meaning a is more specific
    // or less restrictivethan b.

    return MoreSpecific(&a->wildcard, &b->wildcard) ||
           a->type == AllowStatement && b->type == DisallowStatement;
}

class Rule {
  public:
    vector<UserAgent *> UserAgents;
    vector<Directive *> Directives;
    int crawlDelay;

    Rule() : crawlDelay(0) {}

    ~Rule() {
        // Delete the user agents.
        for (auto u : UserAgents)
            delete u;

        // Delete the Directives.
        for (auto d : Directives)
            delete d;
    }

    void AddUserAgent(const Utf8 **start, const Utf8 *bound) {
        // A UserAgent statement has been recognized. Create one with
        // the name specified next on the input and add it to the list
        // for this rule.

        UserAgent *u = new UserAgent(start, bound);
        UserAgents.pushBack(u);
    }

    void AddDirective(const StatementType type, const Utf8 **start, const Utf8 *bound) {
        // An Allow or Disallow directive been found.  Create one with the
        // path specified as the next argument on the input and insertion sort
        // it into the list.

        // YOUR CODE HERE.
        // Directive *d = new Directive(type, start, bound);

        // auto it = Directives.begin();
        // while (it != Directives.end() && !TakesPriority(d, *it)) {
        //     it++;
        // }

        // Directives.insert(it, d);

        // testing w faster code below
        Directives.pushBack(new Directive(type, start, bound));
    }

    void AddCrawlDelay(const Utf8 **start, const Utf8 *bound) {
        // A Crawl-delay statement has been found.  Read the argument
        // and attempt to convert to aninteger with atoi( ).  If it's
        // larger than current delay, use the new value.

        // YOUR CODE HERE.
        Utf8 *arg = GetArgument(start, bound);
        int delay = atoi((const char *)arg);

        if (delay > crawlDelay) {
            crawlDelay = delay;
        }

        delete[] arg;
    }

    Directive *Match(const Utf8 *user, const Utf8 *path, UserAgent **agent) {
        // A rule applies if it matches one of the user agents and there is a
        // directive that matches the path.  Since directives are sorted,
        // we can stop at the first match.
        //
        // If a match is found, return a pointer to the directive. Optionally
        // report which UserAgent was matched.

        // YOUR CODE HERE.
        UserAgent *match = nullptr;
        for (auto u : UserAgents) {
            if (u->Match(user)) {
                match = u;

                break;
            }
        }

        if (!match) {
            return nullptr;
        }

        if (agent) {
            *agent = match;
        }

        for (auto dir : Directives) {
            if (dir->Match(path)) {
                return dir;
            }
        }

        return nullptr;
    }
};

//
// RobotsTxt methods.
//

// Constructor to parse the robots.txt file contents into a
// set of rules and sitemaps.

RobotsTxt::RobotsTxt(const Utf8 *robotsTxt, const size_t length) {
    // Read the file one line at a time until the end, parsing
    // it into a vector of rules.

    const Utf8 *p = robotsTxt, *bound = p + length;
    StatementType type;

    // Skip over any byte order mark.

    if (strncmp((const char *)Utf8BOMString, (const char *)p, sizeof(Utf8BOMString)) == 0)
        p += sizeof(Utf8BOMString);

    // Skip over everything until a User-agent: is found.

    while (p < bound && (type = GetStatement(&p, bound, sitemap)) != UserAgentStatement)
        p = NextLine(&p, bound);

    while (type == UserAgentStatement) {
        // 1. Create a new rule.
        // 2. Collect the user agents.
        // 3. Collect the directives.
        // 4. Add this rule to the list.

        // YOUR CODE HERE.
        // 1
        Rule *rule = new Rule();

        // 2
        while (type == UserAgentStatement) {
            rule->AddUserAgent(&p, bound);
            NextLine(&p, bound);
            if (p >= bound) {
                type = InvalidStatement;
                break;
            }
            type = GetStatement(&p, bound, sitemap);
        }

        // 3
        while (type == AllowStatement || type == DisallowStatement || type == CrawlDelayStatement) {
            if (type == CrawlDelayStatement) {
                rule->AddCrawlDelay(&p, bound);
            }

            else {
                rule->AddDirective(type, &p, bound);
            }

            NextLine(&p, bound);

            if (p >= bound) {
                type = InvalidStatement;
                break;
            }

            type = GetStatement(&p, bound, sitemap);
        }

        // sorting because we are not using insertion sort anymore
        for (size_t i = 1; i < rule->Directives.size(); i++) {
            for (size_t j = i; j > 0 && TakesPriority(rule->Directives[j], rule->Directives[j - 1]);
                 j--) {

                Directive *temp = rule->Directives[j];
                rule->Directives[j] = rule->Directives[j - 1];
                rule->Directives[j - 1] = temp;
            }
        }

        // 4
        rules.pushBack(rule);
    }
}

RobotsTxt::~RobotsTxt() {
    // Free up the rules.
    for (auto r : rules)
        delete r;

    // The sitemap vector will clean up itself.
}

Directive *RobotsTxt::FindDirective(const Utf8 *user, const Utf8 *path, int *crawlDelay,
                                    Rule **rule) {
    // Check the user and path against the rules in this robots.txt.  Return
    // the highest priority directive if one is found.
    //
    // Optionally report the containing rule and the highest value
    // crawl delay found among all the applicable rules.

    // 1. URL decode the path.
    // 2. Iterate over the rules, looking for one that best applies.

    // YOUR CODE HERE.
    Utf8 *decoded = UrlDecode(path);
    Directive *best = nullptr;
    UserAgent *bestAgent = nullptr;

    if (crawlDelay) {
        *crawlDelay = 0;
    }

    if (rule) {
        *rule = nullptr;
    }

    for (auto r : rules) {
        UserAgent *agent = nullptr;
        Directive *dir = r->Match(user, decoded, &agent);

        if (dir) {
            if (crawlDelay && r->crawlDelay > *crawlDelay) {
                *crawlDelay = r->crawlDelay;
            }

            if (!best) {
                best = dir;
                bestAgent = agent;
                if (rule) {
                    *rule = r;
                }

            } else {
                bool newLit = !agent->matchAny;
                bool oldLit = !bestAgent->matchAny;

                if (newLit && !oldLit) {
                    best = dir;
                    bestAgent = agent;
                    if (rule) {
                        *rule = r;
                    }

                } else if (newLit == oldLit && TakesPriority(dir, best)) {
                    best = dir;
                    bestAgent = agent;
                    if (rule) {
                        *rule = r;
                    }
                }
            }
        }
    }
    delete[] decoded;
    return best;
}

bool RobotsTxt::UrlAllowed(const Utf8 *user, const Utf8 *url, int *crawlDelay) {
    // Check a full URL against the rules in this robots.txt.
    // Return true if user is allowed to crawl the URL.
    // Optionally report any crawl delay.

    // Wrapper for PathAllowed that parses the URL first.

    RobotsParsedUrl parsedurl(url);
    return PathAllowed(user, parsedurl.Path, crawlDelay);
}

bool RobotsTxt::PathAllowed(const Utf8 *user, const Utf8 *path, int *crawlDelay) {
    // Check the path portion of a URL against the rules in this
    // robots.txt.  Return true if user is allowed to crawl the path.
    // Optionally report any crawl delay. Skip over any initial /
    // in the path.
    //
    // Acts as wrapper for FindDirective to avoid making the definition
    // of a Directive part of the public interface in RobotsTxt.h.

    Directive *d = FindDirective(user, *path == '/' ? path + 1 : path, crawlDelay);
    return !d || d->type == AllowStatement;
}

vector<string> RobotsTxt::Sitemap() { return sitemap; }