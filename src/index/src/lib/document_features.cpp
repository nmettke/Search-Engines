#include "document_features.h"
#include "url_features.h"
#include <climits>

DocumentFeatures extractDocumentFeatures(const HtmlParser &doc) {
    const ::string &document_url = doc.documentUrl();
    ParsedUrl parsed = parseUrl(document_url);

    DocumentFeatures features;
    features.flags = kFeaturesPresent;
    if (urlHasHttps(document_url)) {
        features.flags |= kHttps;
    }
    if (doc.sawBodyTag()) {
        features.flags |= kSawBodyTag;
    }
    if (doc.sawCloseHtmlTag()) {
        features.flags |= kSawCloseHtmlTag;
    }
    if (doc.wasTruncated()) {
        features.flags |= kHtmlTruncated;
    }
    if (doc.hasOpenDiscardSection()) {
        features.flags |= kHasOpenDiscardSection;
    }

    features.base_domain_length = urlBaseDomainLength(parsed);
    features.url_length =
        static_cast<uint16_t>(std::min(document_url.size(), static_cast<size_t>(UINT16_MAX)));
    features.path_length = urlPathLength(parsed);
    features.path_depth = urlPathDepth(parsed);
    features.query_param_count = urlQueryParamCount(parsed);
    features.numeric_path_char_count = urlNumericPathCharCount(parsed);
    features.domain_hyphen_count = urlDomainHyphenCount(parsed);
    features.outgoing_link_count =
        static_cast<uint16_t>(std::min(doc.links.size(), static_cast<size_t>(UINT16_MAX)));
    features.outgoing_anchor_word_count = static_cast<uint16_t>(
        std::min(static_cast<uint32_t>(UINT16_MAX), doc.outgoingAnchorWordCount()));
    features.raw_tld = parsed.tld;

    return features;
}
