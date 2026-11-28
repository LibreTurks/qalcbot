// qalcBot — a Discord calculator bot powered by libqalculate.
// Copyright (C) 2026 Kreato
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// Heuristic message filter: decides whether a cleaned Discord message
// looks like something worth calculating. It is a port of the heuristic
// used by the original Swift calcBot. Matching is performed on decoded
// UTF-32 code points so that Unicode operators (multiplication and
// division signs) and non-ASCII letters behave correctly.

#include <cstdint>
#include <string>
#include <vector>

namespace qalcbot {

enum class Verdict {
    Calculate,
    Skip,
};

// Decodes a UTF-8 string into code points. Invalid bytes are dropped.
inline std::vector<uint32_t> utf8_to_codepoints(const std::string &s) {
    std::vector<uint32_t> out;
    out.reserve(s.size());
    uint32_t cp = 0;
    int remaining = 0;
    for (unsigned char c : s) {
        if (remaining > 0 && (c & 0xC0) == 0x80) {
            cp = (cp << 6) | (c & 0x3F);
            if (--remaining == 0) out.push_back(cp);
        } else if ((c & 0x80) == 0) {
            out.push_back(c);
            remaining = 0;
        } else if ((c & 0xE0) == 0xC0) {
            cp = c & 0x1F;
            remaining = 1;
        } else if ((c & 0xF0) == 0xE0) {
            cp = c & 0x0F;
            remaining = 2;
        } else if ((c & 0xF8) == 0xF0) {
            cp = c & 0x07;
            remaining = 3;
        } else {
            remaining = 0;
        }
    }
    return out;
}

// Approximate alphabetic classification for a code point. ASCII is exact;
// for higher planes this covers the common letter blocks (Latin-1/Ext,
// Greek, Cyrillic, Hebrew, Arabic, Indic, Thai, CJK, Kana, Hangul...),
// which is enough for the sentence-detection heuristic.
inline bool is_letter_cp(uint32_t cp) {
    if (cp < 0x80) return (cp >= 'a' && cp <= 'z') || (cp >= 'A' && cp <= 'Z');
    if (cp == 0xAA || cp == 0xB5 || cp == 0xBA || cp == 0xBB) return true;
    if (cp >= 0xC0 && cp <= 0x2C1) return cp != 0xD7 && cp != 0xF7; // Latin-1 + Latin Ext-A/B + IPA (minus × and ÷)
    if (cp >= 0x370 && cp <= 0x3FF) return true;                    // Greek
    if (cp >= 0x400 && cp <= 0x52F) return true;                    // Cyrillic
    if (cp >= 0x531 && cp <= 0x58F) return true;                    // Armenian
    if (cp >= 0x590 && cp <= 0x6FF) return true;                    // Hebrew + Arabic
    if (cp >= 0x900 && cp <= 0x97F) return true;                    // Devanagari
    if (cp >= 0xE00 && cp <= 0xE7F) return true;                    // Thai
    if (cp >= 0x1E00 && cp <= 0x1FFF) return true;                  // Latin Ext Additional + Greek Ext
    if (cp >= 0x2E80 && cp <= 0x9FFF) return true;                  // CJK radicals, Kana, CJK unified
    if (cp >= 0xF900 && cp <= 0xFAFF) return true;                  // CJK compatibility
    if ((cp >= 0xFF21 && cp <= 0xFF3A) || (cp >= 0xFF41 && cp <= 0xFF5A)) return true; // Fullwidth
    if (cp >= 0xFF66 && cp <= 0xFF9F) return true;                  // Halfwidth Kana
    return false;
}

// Strips fenced code blocks, inline code and Discord custom emoji tokens
// from a message. Used before the filter sees the text.
std::string clean_message(const std::string &content);

// Trims ASCII whitespace from both ends.
std::string trim_copy(const std::string &s);

// The heuristic itself: should this cleaned expression be calculated?
Verdict should_calculate(const std::string &cleaned);

// The filter accepts "5 kg in lb" as a conversion, but libqalculate reads
// that "in" as inches ("2.27 in*kg^2"). Rewrites the separator to "to";
// any other input is returned unchanged.
std::string normalize_conversion(const std::string &cleaned);

// Extracts the identifiers ("words") of an expression: a letter followed by
// letters, digits or underscores, e.g. "km", "sqrt", "log10", "icin".
// The exponent marker of scientific notation ("1e3", "2.5E-4") is not a
// word. Each word is returned UTF-8 encoded.
std::vector<std::string> extract_words(const std::string &expr);

// Sanity checks on an evaluation result, applied after the engine ran:
// rejects empty results, echoed input, decimal-shifted echoes and
// number/gibberish mixtures that are not real expressions.
bool result_is_sane(const std::string &cleaned_expression, const std::string &result);

} // namespace qalcbot
