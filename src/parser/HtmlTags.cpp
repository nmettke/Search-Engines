#include "HtmlTags.h"
#include <algorithm>
#include <cassert>
#include <cstring>
#include <ctype.h>
#include <iostream>
#include <string>

// name points to beginning of the possible HTML tag name.
// nameEnd points to one past last character.
// Comparison is case-insensitive.
// Use a binary search.
// If the name is found in the TagsRecognized table, return
// the corresponding action.
// If the name is not found, return OrdinaryText.

DesiredAction LookupPossibleTag(const char *name, const char *nameEnd) {
    // Your code here.

    size_t length = nameEnd - name;

    if (length > 10) {
        return DesiredAction::OrdinaryText;
    }

    int left = 0;
    int right = NumberOfTags - 1;

    while (left <= right) {
        int mid = (right + left) / 2;

        int compare = strncasecmp(name, TagsRecognized[mid].Tag,
                                  std::max(length, strlen(TagsRecognized[mid].Tag)));

        if (compare == 0) {
            return TagsRecognized[mid].Action;
        } else if (compare < 0) {
            right = mid - 1;
        } else {
            left = mid + 1;
        }
    }

    return DesiredAction::OrdinaryText;
}