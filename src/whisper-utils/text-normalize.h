#ifndef TEXT_NORMALIZE_H
#define TEXT_NORMALIZE_H

#include <string>

// Normalization for matching only: the audio path and the recognized text
// itself (logs, timeline) stay untouched. Exactly two transforms are
// applied, because the filter is multilingual and must not change how
// patterns for other languages behave:
//   * case folding by code point (Latin, Cyrillic incl. Ukrainian/Greek,
//     Latin-1) instead of byte-wise ::tolower, which never worked for
//     UTF-8 and only ran for English before;
//   * yo -> e, so "ёжик" and "ежик" match the same pattern.
// Everything else - punctuation, spacing, repeated letters, masks '*',
// bytes that are not valid UTF-8 - is preserved exactly as is. Word
// boundaries and '*' expansion belong to the matcher, not here.
namespace pottymuter {

// Normalizes recognized text (see the comment above).
std::string normalize_text(const std::string &text);

// Normalizes the user-provided regex with the same rules, but leaves
// escaped sequences alone: lowercasing \W, \S, \D or \x41 would change the
// meaning, and the pattern must keep its semantics.
std::string normalize_pattern(const std::string &pattern);

} // namespace pottymuter

#endif // TEXT_NORMALIZE_H
