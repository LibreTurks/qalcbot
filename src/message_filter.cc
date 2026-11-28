// qalcBot — a Discord calculator bot powered by libqalculate.
// Copyright (C) 2026 Kreato
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "message_filter.h"

#include <cctype>
#include <regex>

namespace qalcbot {
namespace {

constexpr uint32_t MUL = 0x00D7; // ×
constexpr uint32_t DIV = 0x00F7; // ÷

bool is_ws(uint32_t cp) {
    return cp == ' ' || cp == '\t' || cp == '\n' || cp == '\r' || cp == '\v' || cp == '\f';
}

bool is_digit_cp(uint32_t cp) { return cp >= '0' && cp <= '9'; }

// Operator set used by the filter patterns: + - * / % ^ × ÷
bool is_math_op(uint32_t cp) {
    return cp == '+' || cp == '-' || cp == '*' || cp == '/' || cp == '%' ||
           cp == '^' || cp == MUL || cp == DIV;
}

// hasMathOperator in the original heuristic: % deliberately excluded.
bool is_math_op_no_percent(uint32_t cp) {
    return cp == '+' || cp == '-' || cp == '*' || cp == '/' ||
           cp == '^' || cp == MUL || cp == DIV;
}

// Character class shared by the "expression followed by a message" rule
// and the simple-math rule: digits, operators, dots, parentheses, spaces.
bool is_math_class(uint32_t cp) {
    return is_math_op(cp) || is_digit_cp(cp) || cp == '(' || cp == ')' ||
           cp == '.' || is_ws(cp);
}

// Pattern 1 from the heuristic: "1 usd to try", "100 TRY to USD".
const std::regex &currency_re() {
    static const std::regex re(
        R"(^\s*[-+]?(?:\d{1,3}(?:[.,]\d{3})+|\d+)(?:[.,]\d+)?\s+[a-zA-Z]{3}\s+(to|in)\s+[a-zA-Z]{3}\s*$)",
        std::regex::icase | std::regex::ECMAScript);
    return re;
}

// Generalized conversion shape ("N word to word"), which also covers unit
// conversions such as "10 km to miles" that the currency-only pattern
// would otherwise let the sentence rule swallow.
bool is_unit_conversion(const std::vector<uint32_t> &s) {
    size_t i = 0, n = s.size();
    auto skip_ws = [&] { while (i < n && is_ws(s[i])) ++i; };
    skip_ws();
    if (i < n && (s[i] == '+' || s[i] == '-')) ++i;
    size_t start = i;
    while (i < n && is_digit_cp(s[i])) ++i;
    if (i == start) return false;
    // Optional thousands groups ([.,]\d{3}) or decimal part ([.,]\d+).
    while (i < n && (s[i] == '.' || s[i] == ',')) {
        size_t k = i + 1;
        int digits = 0;
        while (k < n && is_digit_cp(s[k])) { ++digits; ++k; }
        if (digits == 3) i = k;
        else if (digits > 0) { i = k; break; }
        else break;
    }
    skip_ws();
    auto word = [&](size_t &j) {
        size_t w = j;
        while (j < n && ((s[j] >= 'a' && s[j] <= 'z') || (s[j] >= 'A' && s[j] <= 'Z'))) ++j;
        return j > w;
    };
    if (!word(i)) return false;
    skip_ws();
    if (i + 1 >= n) return false;
    bool to = (s[i] == 't' || s[i] == 'T') && (s[i + 1] == 'o' || s[i + 1] == 'O');
    bool in = (s[i] == 'i' || s[i] == 'I') && (s[i + 1] == 'n' || s[i + 1] == 'N');
    if (!to && !in) return false;
    i += 2;
    skip_ws();
    if (!word(i)) return false;
    skip_ws();
    return i == n;
}

bool is_conversion(const std::string &expr, const std::vector<uint32_t> &cps) {
    if (std::regex_match(expr, currency_re())) return true;
    return is_unit_conversion(cps);
}

// Pattern 2: an expression followed by a natural-language message, e.g.
// "1+1 that was easy". std::regex has no \p{L}, so this is matched
// manually over code points.
bool expression_followed_by_message(const std::vector<uint32_t> &s) {
    size_t i = 0, n = s.size();
    while (i < n && is_ws(s[i])) ++i;
    if (i < n && (s[i] == '+' || s[i] == '-')) ++i;
    if (i >= n || !(is_digit_cp(s[i]) || s[i] == '(')) return false;
    ++i; // after the first [0-9(]

    // Lookahead: an operator exists whose left context is entirely inside
    // the math character class.
    bool op_found = false;
    for (size_t j = i; j < n && is_math_class(s[j]); ++j) {
        if (is_math_op(s[j])) { op_found = true; break; }
    }
    if (!op_found) return false;

    // [class]* [0-9)] \s+ <letter> — with backtracking, so every digit or
    // closing parenthesis inside the run is tried as the terminator.
    for (size_t j = i; j < n && is_math_class(s[j]); ++j) {
        if (!(is_digit_cp(s[j]) || s[j] == ')')) continue;
        size_t k = j + 1;
        if (k < n && is_ws(s[k])) {
            while (k < n && is_ws(s[k])) ++k;
            if (k < n && is_letter_cp(s[k])) return true;
        }
    }
    return false;
}

bool is_simple_math(const std::vector<uint32_t> &s) {
    for (uint32_t cp : s) if (!is_math_class(cp)) return false;
    return true;
}

// "sqrt(2)+1" style input starts with letters but is still a calculation:
// allow an identifier followed by '(' as the first token.
bool starts_with_function_call(const std::vector<uint32_t> &s) {
    size_t i = 0, n = s.size();
    while (i < n && is_ws(s[i])) ++i;
    if (i >= n || !is_letter_cp(s[i])) return false;
    ++i;
    while (i < n && (is_letter_cp(s[i]) || is_digit_cp(s[i]) || s[i] == '_')) ++i;
    while (i < n && is_ws(s[i])) ++i;
    return i < n && s[i] == '(';
}

std::vector<std::vector<uint32_t>> split_tokens(const std::vector<uint32_t> &s) {
    std::vector<std::vector<uint32_t>> tokens;
    size_t i = 0, n = s.size();
    while (i < n) {
        while (i < n && is_ws(s[i])) ++i;
        size_t start = i;
        while (i < n && !is_ws(s[i])) ++i;
        if (i > start) tokens.emplace_back(s.begin() + start, s.begin() + i);
    }
    return tokens;
}

bool token_has_letter(const std::vector<uint32_t> &t) {
    for (uint32_t cp : t) if (is_letter_cp(cp)) return true;
    return false;
}

int token_letter_count(const std::vector<uint32_t> &t) {
    int count = 0;
    for (uint32_t cp : t) if (is_letter_cp(cp)) ++count;
    return count;
}

void append_utf8(std::string &out, uint32_t cp) {
    if (cp < 0x80) {
        out += static_cast<char>(cp);
    } else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

// "12:30", "9:05:59" -- a clock time, not a calculation.
bool is_clock_time(const std::string &s) {
    static const std::regex re(R"(^\s*\d{1,2}:\d{2}(?::\d{2})?\s*$)", std::regex::ECMAScript);
    return std::regex_match(s, re);
}

// Chat idioms that happen to be valid arithmetic: "50/50", "10/10", "1-1",
// "3-0" (scores), "7/24" and "24/7" (around the clock).
bool is_chat_idiom(const std::string &s) {
    static const std::regex re(R"(^\s*(\d+)\s*([-/])\s*(\d+)\s*$)", std::regex::ECMAScript);
    std::smatch m;
    if (!std::regex_match(s, m, re)) return false;
    const std::string a = m.str(1), op = m.str(2), b = m.str(3);
    if (a == b) return true;
    if (op == "-" && b == "0") return true;
    return op == "/" && ((a == "7" && b == "24") || (a == "24" && b == "7"));
}

} // namespace

std::string normalize_conversion(const std::string &cleaned) {
    static const std::regex re(R"(^(\s*[-+]?[\d.,]+\s*[A-Za-z]+\s+)in(\s+[A-Za-z]+\s*)$)",
                               std::regex::icase | std::regex::ECMAScript);
    return std::regex_replace(cleaned, re, "$1to$2");
}

std::vector<std::string> extract_words(const std::string &expr) {
    const std::vector<uint32_t> s = utf8_to_codepoints(expr);
    std::vector<std::string> words;
    size_t i = 0, n = s.size();
    while (i < n) {
        if (!is_letter_cp(s[i])) { ++i; continue; }
        const size_t start = i;
        while (i < n && (is_letter_cp(s[i]) || is_digit_cp(s[i]) || s[i] == '_')) ++i;

        // Scientific notation: a digit, then e/E, then digits or a signed
        // exponent ("1e3", "1E+3", "2.5e-4").
        if (start > 0 && (is_digit_cp(s[start - 1]) || s[start - 1] == '.') &&
            (s[start] == 'e' || s[start] == 'E')) {
            size_t k = start + 1;
            while (k < i && is_digit_cp(s[k])) ++k;
            const bool digits_only = k == i && i > start + 1;
            const bool signed_exp = i == start + 1 && i + 1 < n &&
                                    (s[i] == '+' || s[i] == '-') && is_digit_cp(s[i + 1]);
            if (digits_only || signed_exp) continue;
        }

        std::string word;
        for (size_t k = start; k < i; ++k) append_utf8(word, s[k]);
        words.push_back(std::move(word));
    }
    return words;
}

std::string trim_copy(const std::string &s) {
    size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return s.substr(b, e - b);
}

std::string clean_message(const std::string &content) {
    // ECMAScript std::regex has no \Z anchor (libstdc++ parses it as a literal
    // 'Z'); '$' matches end of input because ECMAScript_MULTILINE is off.
    static const std::regex fenced(R"(```[\s\S]*?(?:```|$))", std::regex::ECMAScript);
    static const std::regex inline_code(R"(`[^`\n]*`)", std::regex::ECMAScript);
    static const std::regex custom_emoji(R"(<a?:[A-Za-z0-9_]+:[0-9]+>)", std::regex::ECMAScript);
    // Replace with a space so that adjacent tokens cannot merge into a new number.
    std::string s = std::regex_replace(content, fenced, " ");
    s = std::regex_replace(s, inline_code, " ");
    s = std::regex_replace(s, custom_emoji, " ");
    return s;
}

Verdict should_calculate(const std::string &cleaned) {
    const std::vector<uint32_t> cps = utf8_to_codepoints(cleaned);
    const std::string trimmed = trim_copy(cleaned);
    if (trimmed.empty()) return Verdict::Skip;

    // 1. Currency conversion ("1 usd to try") is always calculable.
    const bool currency = std::regex_match(cleaned, currency_re());
    const bool conversion = currency || is_unit_conversion(cps);

    // 2. An expression followed by a sentence ("1+1 that was easy") is not.
    if (expression_followed_by_message(cps)) return Verdict::Skip;

    // A clock time ("12:30") or a chat idiom ("50/50", "3-0", "7/24").
    if (is_clock_time(cleaned) || is_chat_idiom(cleaned)) return Verdict::Skip;

    // 3. Only math characters: a bare number is not worth replying to.
    // A leading sign alone ("+1", "-1") does not make it an expression.
    if (is_simple_math(cps)) {
        size_t first = 0;
        while (first < cps.size() && is_ws(cps[first])) ++first;
        if (first < cps.size() && (cps[first] == '+' || cps[first] == '-')) ++first;
        bool has_op = false;
        for (size_t k = first; k < cps.size(); ++k) {
            if (is_math_op(cps[k])) { has_op = true; break; }
        }
        if (!has_op) return Verdict::Skip;
    } else {
        // 4. Not a plain math expression.
        // a. Leading letters mean prose, unless it is a function call.
        size_t first = 0;
        while (first < cps.size() && is_ws(cps[first])) ++first;
        if (first < cps.size() && is_letter_cp(cps[first]) && !starts_with_function_call(cps)) {
            return Verdict::Skip;
        }

        const auto tokens = split_tokens(cps);
        size_t alpha_words = 0;      // words containing at least one letter
        size_t long_alpha_words = 0; // alpha words with >= 4 letters
        bool has_math_operator = false;
        for (const auto &t : tokens) {
            if (token_has_letter(t)) ++alpha_words;
            if (token_letter_count(t) >= 4) ++long_alpha_words;
            for (uint32_t cp : t) if (is_math_op_no_percent(cp)) { has_math_operator = true; break; }
        }
        bool has_percent = cleaned.find('%') != std::string::npos;

        // b. A lone percentage word ("100% test") is prose, not math.
        if (has_percent && !has_math_operator && alpha_words > 0 && !conversion) {
            return Verdict::Skip;
        }
        // c. Whole sentences ("30 kirven var +1 kivren eklendi kacti oldu").
        if ((long_alpha_words >= 2 || alpha_words >= 3) && !conversion) {
            return Verdict::Skip;
        }
    }

    // 5. A trailing operator means the expression is incomplete.
    if (!cps.empty()) {
        size_t last = cps.size();
        while (last > 0 && is_ws(cps[last - 1])) --last;
        if (last > 0 && is_math_op(cps[last - 1])) return Verdict::Skip;
    }

    return Verdict::Calculate;
}

namespace {

// First number matching [-+]?\d*[.,]?\d+.
std::string first_number(const std::string &s) {
    static const std::regex re(R"([-+]?\d*[.,]?\d+)", std::regex::ECMAScript);
    std::smatch m;
    if (std::regex_search(s, m, re)) return m.str();
    return {};
}

std::vector<std::string> all_numbers(const std::string &s) {
    static const std::regex re(R"([-+]?\d*[.,]?\d+)", std::regex::ECMAScript);
    std::vector<std::string> out;
    auto begin = std::sregex_iterator(s.begin(), s.end(), re);
    for (auto it = begin; it != std::sregex_iterator(); ++it) out.push_back(it->str());
    return out;
}

// Normalizes a number string: '+' dropped, ',' decimal separator becomes
// '.' when no '.' is present.
std::string normalize_number(std::string n) {
    if (!n.empty() && n[0] == '+') n.erase(0, 1);
    if (n.find('.') == std::string::npos) {
        std::replace(n.begin(), n.end(), ',', '.');
    }
    return n;
}

// Digit string with separators and signs removed: "9.294" -> "9294".
std::string digit_string(const std::string &n) {
    std::string out;
    for (char c : n) if (c >= '0' && c <= '9') out += c;
    return out;
}

// A whitespace token consisting of only 1-3 letters, located between the
// first and last number of the expression: the "2026 djd 7" pattern.
bool has_gibberish_token(const std::string &expr) {
    static const std::regex re(R"([-+]?\d*[.,]?\d+)", std::regex::ECMAScript);
    std::smatch first_m, last_m;
    if (!std::regex_search(expr, first_m, re)) return false;
    for (auto it = std::sregex_iterator(expr.begin(), expr.end(), re); it != std::sregex_iterator(); ++it) {
        last_m = *it;
    }
    if (last_m.empty() || last_m.position() <= first_m.position() + first_m.length()) return false;

    size_t lo = first_m.position() + first_m.length();
    size_t hi = last_m.position();
    if (hi > expr.size()) return false;
    std::string middle = expr.substr(lo, hi - lo);
    for (const auto &tok : split_tokens(utf8_to_codepoints(middle))) {
        if (tok.empty() || tok.size() > 3) continue;
        bool all_letters = true;
        for (uint32_t cp : tok) if (!is_letter_cp(cp)) { all_letters = false; break; }
        if (all_letters) return true;
    }
    return false;
}

} // namespace

bool result_is_sane(const std::string &cleaned_expression, const std::string &result) {
    const std::string expr = trim_copy(cleaned_expression);
    const std::string res = trim_copy(result);
    if (res.empty()) return false;
    if (res == expr) return false; // library could not parse it

    bool has_op = false;
    for (char c : expr) {
        if (is_math_op(static_cast<unsigned char>(c))) { has_op = true; break; }
    }
    if (has_op) return true;

    // Echoed input: the first number of the result equals the first number
    // of the expression ("1 klavye" -> "1 ...").
    std::string en = first_number(expr), rn = first_number(res);
    if (!en.empty() && !rn.empty()) {
        if (normalize_number(en) == normalize_number(rn)) return false;
        if (en.find(',') == std::string::npos && en.find('.') == std::string::npos &&
            rn.find(',') == std::string::npos && rn.find('.') == std::string::npos) {
            try {
                if (std::stod(en) == std::stod(rn)) return false;
            } catch (...) {}
        }
    }

    // Decimal-shifted echo: "9294" evaluated as "9.294".
    for (const auto &a : all_numbers(expr)) {
        const std::string da = digit_string(a);
        for (const auto &b : all_numbers(res)) {
            if (digit_string(b) == da) return false;
        }
    }

    // A short letter-only token sitting between two numbers is gibberish.
    if (all_numbers(expr).size() >= 2 && has_gibberish_token(expr)) return false;

    return true;
}

} // namespace qalcbot
