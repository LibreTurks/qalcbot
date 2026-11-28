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

// Thin thread-safe wrapper around the global libqalculate CALCULATOR
// instance. libqalculate is not thread-safe, while D++ dispatches events
// from multiple threads, so every call into CALCULATOR is serialized by
// an internal mutex.

#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>

class QalculateEngine {
public:
    // Constructs the global CALCULATOR and loads definitions.
    // Pass quiet = true to suppress progress output (CLI modes).
    explicit QalculateEngine(bool quiet = false);
    ~QalculateEngine();

    QalculateEngine(const QalculateEngine &) = delete;
    QalculateEngine &operator=(const QalculateEngine &) = delete;

    // Evaluates an expression and returns the plain-text result.
    // Returns an empty string when the evaluation failed or was aborted.
    std::string evaluate(const std::string &expr);

    // True when every word of the expression is something libqalculate
    // knows by that exact name: a unit (optionally with a prefix, "km"),
    // a variable/constant, a function or an operator keyword ("to").
    // libqalculate otherwise splits an unknown word into one-letter units,
    // prefixes and constants and multiplies them: "60hz icin" evaluates to
    // "0.6iz h*in" and "5 elma" to "13.59 a*lm".
    bool all_words_known(const std::string &expr);

    // Mirrors the qalc CLI: re-download exchange rates when they are
    // older than a week.
    void refresh_exchange_rates();

    // Refreshes the rates once now, then once per hour on a background
    // thread that is stopped on destruction.
    void start_rate_refresh_thread();

private:
    bool quiet_;
    std::mutex calc_mutex_;

    std::mutex rate_mutex_;
    std::condition_variable_any rate_cv_;
    // Declared last so it is destroyed (and joined) first: the thread
    // body waits on rate_cv_/rate_mutex_ above, which must still be
    // alive when the join completes. calc_mutex_ is declared before the
    // rate members, so it is destroyed last. OK.
    std::jthread rate_thread_;
};
