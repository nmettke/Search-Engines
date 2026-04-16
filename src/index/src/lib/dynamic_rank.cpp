#include "dynamic_rank.h"
#include "../../../utils/vector.hpp"
#include <algorithm>
#include <limits>
#include <utility>

namespace {

enum class StreamKind { Anchor, Title, Body };

struct StreamFeatures {
    bool active = false;
    size_t stream_length = 0;
    size_t present_terms = 0;
    size_t total_occurrences = 0; // total occurence of all terms in doc
    bool exact_phrase = false;
    bool short_span = false;
    bool ordered_span = false;
    bool short_ordered_span = false;
    bool has_double = false;
    bool has_triple = false;
    bool near_top = false;     // span is near top
    bool too_frequent = false; // occurence exceed cap
    size_t earliest_span_start = std::numeric_limits<size_t>::max();
    int rarest_term_index = -1;
    vector<vector<uint32_t>> positions; // vector of positions of each term
};

size_t shortSpanLimit(const DynamicRankConfig &config, StreamKind kind, size_t term_count) {
    // Define short span length based on unique term count of query
    size_t multiplier = config.short_span_multiplier_body;
    if (kind == StreamKind::Anchor) {
        multiplier = config.short_span_multiplier_anchor;
    } else if (kind == StreamKind::Title) {
        multiplier = config.short_span_multiplier_title;
    }
    return std::max<size_t>(term_count, term_count * multiplier);
}

size_t nearTopLimit(const DynamicRankConfig &config, StreamKind kind) {
    // Return near top definition based on stream type
    if (kind == StreamKind::Anchor) {
        return config.near_top_limit_anchor;
    }
    if (kind == StreamKind::Title) {
        return config.near_top_limit_title;
    }
    return config.near_top_limit_body;
}

double streamWeight(const DynamicRankConfig &config, StreamKind kind) {
    // Return our weighting for different part of text
    if (kind == StreamKind::Anchor) {
        return config.anchor_stream_weight;
    }
    if (kind == StreamKind::Title) {
        return config.title_stream_weight;
    }
    return config.body_stream_weight;
}

bool hasExactSequence(const vector<vector<uint32_t>> &positions, const vector<int> &term_indexes) {
    // given an vector of indices that represent exact phrase
    // we check whether they appear together in the whole doc
    if (term_indexes.empty()) {
        return false;
    }

    const vector<uint32_t> &first_positions = positions[term_indexes[0]];
    for (uint32_t start : first_positions) {
        uint32_t expected = start;
        bool matches = true;
        for (size_t i = 1; i < term_indexes.size(); ++i) {
            ++expected;
            const vector<uint32_t> &next_positions = positions[term_indexes[i]];
            if (!std::binary_search(next_positions.begin(), next_positions.end(), expected)) {
                matches = false;
                break;
            }
        }
        if (matches) {
            return true;
        }
    }
    return false;
}

bool findOrderedSpan(const vector<vector<uint32_t>> &positions, size_t &best_width,
                     size_t &best_start) {
    // returns whether a ordered span exists
    // edits best_width and best start to smallest width
    // and corresponding start loc of that span
    if (positions.empty() || positions[0].empty()) {
        return false;
    }

    bool found = false;
    best_width = std::numeric_limits<size_t>::max();
    best_start = std::numeric_limits<size_t>::max();

    for (uint32_t start : positions[0]) {
        // Use first location of first term as starting
        uint32_t current = start;
        bool matched = true;
        for (size_t i = 1; i < positions.size(); ++i) {
            const vector<uint32_t> &next_positions = positions[i];
            // if any subsequent word does not have a location after current, no match
            auto it = std::lower_bound(next_positions.begin(), next_positions.end(), current);
            if (it == next_positions.end()) {
                matched = false;
                break;
            }
            // move current correspondingly
            current = *it;
        }

        if (!matched) {
            continue;
        }

        found = true;
        size_t width = static_cast<size_t>(current - start + 1);
        // See if this width is better
        if (width < best_width || (width == best_width && start < best_start)) {
            best_width = width;
            best_start = start;
        }
    }

    return found;
}

bool findUnorderedSpan(const vector<vector<uint32_t>> &positions, size_t &best_width,
                       size_t &best_start) {
    // similar to ordered span ..... but unordered!
    // We use a two pointer approach on a sorted positions array (essentially the body but only
    // query terms)
    struct Event {
        uint32_t loc;
        int term_index;
    };

    vector<Event> events;
    for (size_t i = 0; i < positions.size(); ++i) {
        for (uint32_t loc : positions[i]) {
            events.pushBack({loc, static_cast<int>(i)});
        }
    }
    if (events.empty()) {
        return false;
    }

    std::sort(events.begin(), events.end(),
              [](const Event &lhs, const Event &rhs) { return lhs.loc < rhs.loc; });

    vector<size_t> counts(positions.size(), 0);
    size_t covered = 0;
    size_t left = 0;
    bool found = false;
    best_width = std::numeric_limits<size_t>::max();
    best_start = std::numeric_limits<size_t>::max();

    for (size_t right = 0; right < events.size(); ++right) {
        if (counts[events[right].term_index]++ == 0) {
            ++covered;
        }

        while (covered == positions.size() && left <= right) {
            // We covered all term at least once
            // We start seeing if we can shorten the left side
            found = true;
            size_t width = static_cast<size_t>(events[right].loc - events[left].loc + 1);
            size_t start = static_cast<size_t>(events[left].loc);
            if (width < best_width || (width == best_width && start < best_start)) {
                best_width = width;
                best_start = start;
            }

            if (--counts[events[left].term_index] == 0) {
                // We no longer cover all terms
                --covered;
            }
            ++left;
        }
    }

    return found;
}

bool hasNearbyOccurrence(const vector<uint32_t> &term_positions, uint32_t anchor_loc, size_t gap) {
    // Whether the postions for term is within a circle of diameter gap centered at anchor loc
    if (term_positions.empty()) {
        return false;
    }

    // first iterator pass begin
    auto it = std::lower_bound(term_positions.begin(), term_positions.end(), anchor_loc);
    uint32_t max_width = static_cast<uint32_t>(gap + 2);
    if (it != term_positions.end()) {
        uint32_t left = anchor_loc;
        uint32_t right = *it;
        if (right - left + 1 <= max_width) {
            return true;
        }
    }
    if (it != term_positions.begin()) {
        --it;
        uint32_t left = *it;
        uint32_t right = anchor_loc;
        if (right - left + 1 <= max_width) {
            return true;
        }
    }
    return false;
}

StreamFeatures extractStreamFeatures(const DiskChunkReader &reader, const QueryProfile &profile,
                                     const ::vector<::string> &stream_terms, uint32_t start,
                                     uint32_t end, StreamKind kind,
                                     const DynamicRankConfig &config) {
    // Compile all features, will be multiplied by weight later
    StreamFeatures features;
    if (start > end || profile.unique_terms.empty()) {
        return features;
    }

    features.active = true;
    features.stream_length = static_cast<size_t>(end - start + 1);
    features.positions.reserve(stream_terms.size());

    uint64_t rarest_frequency = std::numeric_limits<uint64_t>::max();
    for (size_t i = 0; i < stream_terms.size(); ++i) {
        // populate locations, add simple stuff such as rarest index and num present terms
        vector<uint32_t> positions;
        std::unique_ptr<ISRWord> isr = reader.createISR(stream_terms[i]);
        if (isr) {
            uint32_t loc = isr->seek(start);
            while (loc != ISRSentinel && loc <= end) {
                positions.push_back(loc);
                loc = isr->next();
            }
        }

        features.total_occurrences += positions.size();
        if (!positions.empty()) {
            ++features.present_terms;
            if (std::optional<TermInfo> info = reader.getTermInfo(stream_terms[i])) {
                if (info->collection_frequency < rarest_frequency) {
                    rarest_frequency = info->collection_frequency;
                    features.rarest_term_index = static_cast<int>(i);
                }
            } else if (features.rarest_term_index < 0) {
                features.rarest_term_index = static_cast<int>(i);
            }
        }
        features.positions.push_back(std::move(positions));
    }

    // We stop early if no term match
    if (features.present_terms == 0) {
        return features;
    }

    for (const QueryPhrase &phrase : profile.phrases) {
        if (phrase.terms.size() < 2) {
            continue;
        }

        // indices of phrase term in query
        vector<int> indexes;
        bool complete = true;
        for (const ::string &term : phrase.terms) {
            int found_index = -1;
            for (size_t i = 0; i < profile.unique_terms.size(); ++i) {
                if (profile.unique_terms[i] == term) {
                    found_index = static_cast<int>(i);
                    break;
                }
            }
            if (found_index < 0 || features.positions[found_index].empty()) {
                // missing location, no exact match
                complete = false;
                break;
            }
            indexes.push_back(found_index);
        }

        // check whether we have exact phrase match
        if (complete && hasExactSequence(features.positions, indexes)) {
            features.exact_phrase = true;
            break;
        }
    }

    // If query terms found in document matches the number of unique term in query
    if (features.present_terms == profile.unique_terms.size()) {
        size_t best_unordered_width = 0;
        size_t best_unordered_start = 0;
        if (findUnorderedSpan(features.positions, best_unordered_width, best_unordered_start)) {
            features.short_span =
                best_unordered_width <= shortSpanLimit(config, kind, profile.unique_terms.size());
            features.earliest_span_start =
                std::min(features.earliest_span_start, best_unordered_start);
        }

        size_t best_ordered_width = 0;
        size_t best_ordered_start = 0;
        if (findOrderedSpan(features.positions, best_ordered_width, best_ordered_start)) {
            features.ordered_span = true;
            features.short_ordered_span =
                best_ordered_width <= shortSpanLimit(config, kind, profile.unique_terms.size());
            features.earliest_span_start =
                std::min(features.earliest_span_start, best_ordered_start);
            if (best_ordered_width == profile.unique_terms.size()) {
                features.exact_phrase = true;
            }
        }

        if (features.earliest_span_start != std::numeric_limits<size_t>::max()) {
            size_t offset = features.earliest_span_start - start;
            features.near_top = offset <= nearTopLimit(config, kind);
        }
    }

    if (features.rarest_term_index >= 0 && profile.unique_terms.size() >= 2) {
        const vector<uint32_t> &anchor_positions = features.positions[features.rarest_term_index];
        // Where rarest terms occur
        for (uint32_t anchor_loc : anchor_positions) {
            vector<int> nearby_terms;
            for (size_t i = 0; i < features.positions.size(); ++i) {
                if (static_cast<int>(i) == features.rarest_term_index ||
                    features.positions[i].empty()) {
                    continue;
                }
                // there's a term nearBy the rarest term
                if (hasNearbyOccurrence(features.positions[i], anchor_loc,
                                        config.max_gap_for_double)) {
                    nearby_terms.push_back(static_cast<int>(i));
                }
            }
            if (!nearby_terms.empty()) {
                features.has_double = true;
            }
            if (nearby_terms.size() >= 2) {
                features.has_triple = true;
                break;
            }
        }
    }

    if (features.total_occurrences > profile.unique_terms.size() * config.max_counted_occurrences) {
        // total occurence of terms greater than capped per term
        // or one term occurred more than capped per term
        features.too_frequent = true;
    } else {
        for (const vector<uint32_t> &positions : features.positions) {
            if (positions.size() > config.max_counted_occurrences) {
                features.too_frequent = true;
                break;
            }
        }
    }

    return features;
}

double scoreStreamFeatures(const StreamFeatures &features, const QueryProfile &profile,
                           StreamKind kind, const DynamicRankConfig &config) {
    // general weight * feature calculation after compiling all features
    if (!features.active || features.present_terms == 0 || profile.unique_terms.empty()) {
        return 0.0;
    }

    double score = 0.0;
    if (features.present_terms == profile.unique_terms.size()) {
        score += config.all_words_bonus;
    } else if (features.present_terms >= profile.unique_terms.size() * config.most_words_percent) {
        score += config.most_words_bonus;
    } else {
        score += config.some_words_bonus;
    }

    if (features.exact_phrase) {
        score += config.exact_phrase_bonus;
    }
    if (features.short_span) {
        score += config.short_span_bonus;
    }
    if (features.ordered_span) {
        score += config.ordered_span_bonus;
    }
    if (features.short_ordered_span) {
        score += config.short_ordered_span_bonus;
    }
    if (features.has_double) {
        score += config.double_bonus;
    }
    if (features.has_triple) {
        score += config.triple_bonus;
    }
    if (features.near_top) {
        score += config.near_top_bonus;
    }

    score +=
        config.occurrence_bonus *
        static_cast<double>(std::min(features.total_occurrences, config.max_counted_occurrences));

    if (features.too_frequent) {
        score -= config.too_frequent_penalty;
    }

    return score * streamWeight(config, kind);
}

} // namespace

double DynamicRankScorer::score(const DiskChunkReader &body_reader,
                                const DiskChunkReader *anchor_reader, const QueryProfile &profile,
                                uint32_t doc_id, const DocumentRecord &body_doc,
                                double static_score, double min_competitive_score) const {
    double total = config_.static_weight * static_score;
    double remaining = maxDynamicScore(profile, true, anchor_reader != nullptr);

    if (anchor_reader != nullptr) {
        // we score anchor score first
        if (std::optional<DocumentRecord> anchor_doc = anchor_reader->getDocument(doc_id)) {
            StreamFeatures anchor_features = extractStreamFeatures(
                *anchor_reader, profile, profile.unique_terms, anchor_doc->start_location,
                anchor_doc->end_location, StreamKind::Anchor, config_);
            total += scoreStreamFeatures(anchor_features, profile, StreamKind::Anchor, config_);
        }
        // Max remaining is now max bodyTitle + current score
        remaining -=
            streamWeight(config_, StreamKind::Anchor) *
            (config_.all_words_bonus + config_.exact_phrase_bonus + config_.short_span_bonus +
             config_.ordered_span_bonus + config_.short_ordered_span_bonus + config_.triple_bonus +
             config_.near_top_bonus + config_.occurrence_bonus * config_.max_counted_occurrences);

        if (total + remaining <= min_competitive_score) {
            // if this score is not competitive we can skip the rest
            return total;
        }
    }

    if (body_doc.title_word_count > 0) {
        uint32_t title_start = body_doc.start_location + body_doc.word_count;
        uint32_t title_end = title_start + body_doc.title_word_count - 1;
        ::vector<::string> title_terms;
        for (const ::string &term : profile.unique_terms) {
            title_terms.pushBack("$" + term);
        }
        StreamFeatures title_features = extractStreamFeatures(
            body_reader, profile, title_terms, title_start, title_end, StreamKind::Title, config_);
        total += scoreStreamFeatures(title_features, profile, StreamKind::Title, config_);
    }

    remaining -=
        streamWeight(config_, StreamKind::Title) *
        (config_.all_words_bonus + config_.exact_phrase_bonus + config_.short_span_bonus +
         config_.ordered_span_bonus + config_.short_ordered_span_bonus + config_.triple_bonus +
         config_.near_top_bonus + config_.occurrence_bonus * config_.max_counted_occurrences);
    // Similarly we score title and skip if score is bad
    if (total + remaining <= min_competitive_score) {
        return total;
    }

    if (body_doc.word_count > 0) {
        uint32_t body_end = body_doc.start_location + body_doc.word_count - 1;
        StreamFeatures body_features =
            extractStreamFeatures(body_reader, profile, profile.unique_terms,
                                  body_doc.start_location, body_end, StreamKind::Body, config_);
        total += scoreStreamFeatures(body_features, profile, StreamKind::Body, config_);
    }

    return total;
}

double DynamicRankScorer::maxDynamicScore(const QueryProfile &profile, bool include_body_title,
                                          bool include_anchor) const {
    // return max possible dynamic score based on what streams we include
    if (profile.unique_terms.empty()) {
        return 0.0;
    }

    double stream_cap = config_.all_words_bonus + config_.exact_phrase_bonus +
                        config_.short_span_bonus + config_.ordered_span_bonus +
                        config_.short_ordered_span_bonus + config_.double_bonus +
                        config_.triple_bonus + config_.near_top_bonus +
                        config_.occurrence_bonus * config_.max_counted_occurrences;

    double total = 0.0;
    if (include_body_title) {
        total += stream_cap * config_.title_stream_weight;
        total += stream_cap * config_.body_stream_weight;
    }
    if (include_anchor) {
        total += stream_cap * config_.anchor_stream_weight;
    }
    return total;
}
