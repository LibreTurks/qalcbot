// qalcBot — a Discord calculator bot powered by libqalculate.
// Copyright (C) 2026 Kreato
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "qalculate_engine.h"

#include <libqalculate/Calculator.h>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <iostream>

namespace {

std::string trim(const std::string &s) {
    size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return s.substr(b, e - b);
}

// Drop embedded control characters (terminal color codes, zero-width
// characters, BOM) so the result is safe to embed in a Discord message.
void strip_control_chars(std::string &s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        if (c < 0x20 || c == 0x7F) continue;
        // Zero-width space/joiners, line/paragraph separators (E2 80 8B-8F, A8, A9)
        if (c == 0xE2 && i + 2 < s.size() &&
            static_cast<unsigned char>(s[i + 1]) == 0x80) {
            unsigned char t = static_cast<unsigned char>(s[i + 2]);
            if ((t >= 0x8B && t <= 0x8F) || t == 0xA8 || t == 0xA9) {
                i += 2;
                continue;
            }
        }
        // UTF-8 BOM (EF BB BF)
        if (c == 0xEF && i + 2 < s.size() &&
            static_cast<unsigned char>(s[i + 1]) == 0xBB &&
            static_cast<unsigned char>(s[i + 2]) == 0xBF) {
            i += 2;
            continue;
        }
        out.push_back(s[i]);
    }
    s = std::move(out);
}

} // namespace

QalculateEngine::QalculateEngine(bool quiet) : quiet_(quiet) {
    if (!quiet_) std::cout << "Initializing calculator engine..." << std::endl;

    // new Calculator() sets the global CALCULATOR pointer. It is never
    // terminated explicitly; it lives as long as the process.
    new Calculator();

    if (!quiet_) std::cout << "Updating currency rates..." << std::endl;
    // Exchange rates must be loaded before the global definitions, which
    // reference the currencies.
    CALCULATOR->loadExchangeRates();

    if (!quiet_) std::cout << "Loading definitions..." << std::endl;
    CALCULATOR->loadGlobalDefinitions();

    // Local (user) definitions are optional.
    if (!CALCULATOR->loadLocalDefinitions()) {
        if (!quiet_) std::cout << "No local definitions found." << std::endl;
    }
}

QalculateEngine::~QalculateEngine() = default;

std::string QalculateEngine::evaluate(const std::string &expr) {
    std::lock_guard<std::mutex> lock(calc_mutex_);
    if (!CALCULATOR) return {};

    // Collect parser errors instead of emitting the (garbage) result of a
    // failed parse, mirroring the qalc CLI's message handling: blocked
    // messages are discarded when at least one error occurred (as with
    // qalc's release threshold of MESSAGE_ERROR), otherwise they are
    // released and drained here.
    // Match the qalc CLI defaults: undefined symbols are parse errors, not
    // silent unknown variables, and intervals print with significant digits.
    EvaluationOptions eo = default_user_evaluation_options;
    eo.parse_options.unknowns_enabled = false;
    PrintOptions po = default_print_options;
    po.interval_display = INTERVAL_DISPLAY_SIGNIFICANT_DIGITS;
    CALCULATOR->beginTemporaryStopMessages();
    // calculateAndPrint() stops the control it starts before returning
    // ("if(msecs > 0) stopControl()" in Calculator-calculate.cc), which
    // resets Calculator::aborted() -- so a timeout must be detected via
    // the wall clock: the 2000 ms budget is shared by evaluation and
    // printing, and reaching it means some phase was truncated.
    const auto eval_start = std::chrono::steady_clock::now();
    std::string res = CALCULATOR->calculateAndPrint(expr, 2000, eo, po);
    const bool timed_out =
        std::chrono::steady_clock::now() - eval_start >= std::chrono::milliseconds(2000);
    const int error_count = CALCULATOR->endTemporaryStopMessages(nullptr, nullptr, MESSAGE_ERROR);

    bool had_error = timed_out || error_count > 0;
    while (CALCULATOR->message()) {
        // Only errors are fatal; warnings and information are ignored.
        if (CALCULATOR->message()->type() == MESSAGE_ERROR) had_error = true;
        CALCULATOR->nextMessage();
    }
    CALCULATOR->clearMessages();

    if (had_error) return {};

    strip_control_chars(res);
    return trim(res);
}

void QalculateEngine::refresh_exchange_rates() {
    std::lock_guard<std::mutex> lock(calc_mutex_);
    if (!CALCULATOR) return;

    if (!quiet_) std::cout << "Updating currency rates..." << std::endl;
    if (!CALCULATOR->checkExchangeRatesDate(7, false, false, -1)) {
        if (!CALCULATOR->fetchExchangeRates(15, -1) && !quiet_) {
            std::cout << "Failed to download updated exchange rates." << std::endl;
        }
        CALCULATOR->loadExchangeRates();
    }
    // fetchExchangeRates() queues a MESSAGE_ERROR on failure; there is no
    // terminal to display it, and without clearing it the next evaluate()
    // would pick it up and reject one innocent calculation as erroneous.
    CALCULATOR->clearMessages();
}

void QalculateEngine::start_rate_refresh_thread() {
    if (rate_thread_.joinable()) return; // already running
    refresh_exchange_rates();

    rate_thread_ = std::jthread([this](std::stop_token st) {
        std::unique_lock<std::mutex> lock(rate_mutex_);
        // Sleep for one hour (or until stopped), then refresh again.
        while (!rate_cv_.wait_for(lock, st, std::chrono::hours(1),
                                  [&st] { return st.stop_requested(); })) {
            lock.unlock();
            refresh_exchange_rates();
            lock.lock();
        }
    });
}
