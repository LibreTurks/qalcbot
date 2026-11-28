// qalcBot — a Discord calculator bot powered by libqalculate.
// Copyright (C) 2026 Kreato
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <dpp/dpp.h>

#include <cstdlib>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "message_filter.h"
#include "qalculate_engine.h"

namespace {

std::unique_ptr<QalculateEngine> g_engine;

constexpr uint32_t kEmbedColor = 0x3498db; // blue

dpp::snowflake invoker_id(const dpp::interaction &cmd) {
    return cmd.usr.id ? cmd.usr.id : cmd.member.user_id;
}

// The shared reply shape: an embed with the calculation plus a Delete
// button that only the original author (or message managers) may use.
dpp::message build_result_message(dpp::snowflake channel_id,
                                  const std::string &expression,
                                  const std::string &result,
                                  dpp::snowflake author_id) {
    dpp::embed embed;
    embed.set_title("Calculation Result")
        .set_description("```\n" + expression + "\n= " + result + "\n```")
        .set_color(kEmbedColor)
        .set_timestamp(time(nullptr))
        .set_footer(dpp::embed_footer().set_text("libqalculate"));

    dpp::component button;
    button.set_type(dpp::cot_button)
        .set_style(dpp::cos_danger)
        .set_label("Delete")
        .set_id("delete:" + std::to_string(static_cast<uint64_t>(author_id)));

    dpp::message msg;
    msg.set_channel_id(channel_id)
        .add_embed(embed)
        .add_component(dpp::component().add_component(button));
    return msg;
}

void run_eval(const std::string &expression) {
    g_engine = std::make_unique<QalculateEngine>(true);
    std::cout << g_engine->evaluate(expression) << std::endl;
}

void run_selftest() {
    g_engine = std::make_unique<QalculateEngine>(true);

    struct Case {
        const char *input;
        const char *expect_filter; // "pass" or "skip"
        const char *expect_result; // exact result, "any" (non-empty), "" (no reply)
    };

    static const Case cases[] = {
        {"1+1",                                        "pass", "2"},
        {"(2 + 3) * 4",                                "pass", "20"},
        {"2077",                                       "skip", ""},   // bare number
        {"1 usd to try",                               "pass", "any"}, // currency
        {"100 TRY to USD",                             "pass", "any"},
        {"1 klavye",                                   "pass", ""},   // garbage parse -> engine error
        {"9294",                                       "skip", ""},   // bare number / decimal shift
        {"100% test",                                  "skip", ""},   // percent + prose
        {"30 kirven var +1 kivren eklendi kacti oldu", "skip", ""},   // sentence
        {"2026 djd 7",                                 "pass", ""},   // gibberish between numbers
        {"1+1 that was easy",                          "skip", ""},   // expression followed by message
        {"322-",                                       "skip", ""},   // trailing operator
        {"hello world",                                "skip", ""},   // leading letters
        {"5% of 200",                                  "skip", ""},   // percent + prose
        {"sqrt(2)+1",                                  "pass", "any"},
        {"10 km to miles",                             "pass", "any"}, // unit conversion
        {"sin(30)+2^10",                               "pass", "any"},
        {"142*5/2",                                    "pass", "355"},
        // Unknown words split into one-letter units/constants.
        {"60hz icin",                                  "pass", ""},   // "0.6iz h*in"
        {"5 elma",                                     "pass", ""},   // "13.59 a*lm"
        {"1 tane",                                     "pass", ""},   // tan(e)
        {"512gb ssd",                                  "pass", ""},
        {"10 mb",                                      "pass", ""},   // millibarn
        {"3 ad",                                       "pass", ""},   // atto-day
        {"2 mi",                                       "pass", ""},   // Turkish particle, not miles
        {"3rd",                                        "pass", ""},   // ordinal, not rods
        {"1e3+1",                                      "pass", "1001"},
        {"3 GB to MB",                                 "pass", "any"},
        {"5 mg to g",                                  "pass", "any"},
        {"2 hours + 30 min",                           "pass", "any"},
        // Chat idioms, clock times and bare signed numbers.
        {"12:30",                                      "skip", ""},
        {"50/50",                                      "skip", ""},
        {"7/24",                                       "skip", ""},
        {"3-0",                                        "skip", ""},
        {"+1",                                         "skip", ""},
        {"-1",                                         "skip", ""},
        {"1/2",                                        "pass", "any"},
        {"5-3",                                        "pass", "2"},
    };

    int failures = 0;
    for (const auto &c : cases) {
        const std::string input = c.input;
        const bool filter_pass =
            qalcbot::should_calculate(qalcbot::trim_copy(input)) == qalcbot::Verdict::Calculate;

        // Full pipeline: only a filter pass even reaches evaluation, and a
        // result that fails the sanity checks is never sent to the channel.
        std::string result;
        if (filter_pass && g_engine->all_words_known(input)) {
            result = g_engine->evaluate(qalcbot::normalize_conversion(input));
            if (!result.empty() && !qalcbot::result_is_sane(input, result)) result.clear();
        }

        std::cout << input << " | filter=" << (filter_pass ? "pass" : "skip")
                  << " | result=" << result << std::endl;

        std::string problem;
        const std::string want_filter = c.expect_filter;
        if ((want_filter == "pass") != filter_pass) {
            problem = "filter expected " + want_filter;
        } else if (std::string(c.expect_result) == "any" && result.empty()) {
            problem = "expected a result";
        } else if (*c.expect_result != '\0' && std::string(c.expect_result) != "any" &&
                   result != c.expect_result) {
            problem = "expected result \"" + std::string(c.expect_result) + "\"";
        } else if (*c.expect_result == '\0' && !result.empty()) {
            problem = "expected no reply";
        }
        if (!problem.empty()) {
            ++failures;
            std::cerr << "FAIL: \"" << input << "\": " << problem << std::endl;
        }
    }

    std::cout << "selftest: " << (failures == 0 ? "all checks passed" : std::to_string(failures) + " check(s) failed")
              << std::endl;
    if (failures != 0) exit(1);
}

int run_bot(const char *token) {
    g_engine = std::make_unique<QalculateEngine>(false);

    // Refresh the exchange rates once now, then once per hour.
    g_engine->start_rate_refresh_thread();

    dpp::cluster bot(token,
                     dpp::i_guilds | dpp::i_guild_messages | dpp::i_message_content);
    bot.set_presence(dpp::presence(dpp::ps_online, dpp::at_game,
                                   "Calculating how much \U0001F5FF to send"));

    bot.on_ready([&bot](const dpp::ready_t &) {
        // Register the global slash command exactly once.
        static std::once_flag command_registered;
        std::call_once(command_registered, [&bot] {
            dpp::slashcommand cmd("calculate", "Force a calculation (bypassing filters)",
                                  bot.me.id);
            cmd.add_option(dpp::command_option(dpp::co_string, "expression",
                                               "The expression to calculate", true));
            bot.global_command_create(cmd);
            std::cout << "Slash command registered." << std::endl;
        });
    });

    bot.on_slashcommand([](const dpp::slashcommand_t &event) {
        if (event.command.get_command_name() != "calculate") return;

        std::string expression;
        const dpp::command_value parameter = event.get_parameter("expression");
        if (const std::string *value = std::get_if<std::string>(&parameter)) {
            expression = *value;
        }
        if (expression.empty()) {
            event.reply("Please provide an expression to calculate.");
            return;
        }

        event.thinking();
        std::string result = g_engine->evaluate(expression);
        if (result.empty()) {
            event.edit_response("Could not calculate the expression.");
            return;
        }
        event.edit_response(
            build_result_message(0, expression, result, invoker_id(event.command)));
    });

    bot.on_button_click([&bot](const dpp::button_click_t &event) {
        if (event.custom_id.rfind("delete:", 0) != 0) return; // not ours

        const dpp::snowflake author_id{event.custom_id.substr(7)};
        const dpp::snowflake clicker = invoker_id(event.command);

        bool may_delete = (clicker == author_id);
        if (!may_delete) {
            if (dpp::guild *g = dpp::find_guild(event.command.guild_id)) {
                if (dpp::channel *ch = dpp::find_channel(event.command.channel_id)) {
                    const dpp::permission perms = g->permission_overwrites(event.command.member, *ch);
                    may_delete = perms.has(dpp::permissions::p_manage_messages);
                }
            }
        }
        if (!may_delete) {
            dpp::message m;
            m.content = "You don't have permission to delete this message.";
            m.set_flags(dpp::m_ephemeral);
            event.reply(dpp::ir_channel_message_with_source, m);
            return;
        }

        // Acknowledge the interaction, then delete the message the button
        // was attached to.
        event.reply();
        bot.message_delete(event.command.msg.id, event.command.channel_id);
    });

    bot.on_message_create([&bot](const dpp::message_create_t &event) {
        // a. Never react to bots (including our own replies).
        if (event.msg.author.is_bot()) return;

        // b. + c. Strip code blocks, inline code and custom emoji.
        std::string cleaned = qalcbot::clean_message(event.msg.content);
        cleaned = qalcbot::trim_copy(cleaned);

        // d. Nothing left to calculate.
        if (cleaned.empty()) return;

        // e. Heuristic filter.
        if (qalcbot::should_calculate(cleaned) != qalcbot::Verdict::Calculate) return;

        // Every word must be a real unit/variable/function name; otherwise
        // libqalculate would split it into one-letter units ("icin" ->
        // "i*c*i*n") and reply with nonsense.
        if (!g_engine->all_words_known(cleaned)) return;

        // f. Evaluate, sanity-check, and reply with the original text.
        std::string result = g_engine->evaluate(qalcbot::normalize_conversion(cleaned));
        if (result.empty() || !qalcbot::result_is_sane(cleaned, result)) {
            // g. Failure is silent.
            return;
        }
        bot.message_create(build_result_message(event.msg.channel_id, event.msg.content,
                                               result, event.msg.author.id));
    });

    std::cout << "Connecting to Discord..." << std::endl;
    bot.start(dpp::st_wait);
    return 0;
}

} // namespace

int main(int argc, char **argv) {
    const std::vector<std::string> args(argv + 1, argv + argc);
    if (args.size() == 2 && args[0] == "--eval") {
        run_eval(args[1]);
        return 0;
    }
    if (args.size() == 1 && args[0] == "--selftest") {
        run_selftest();
        return 0;
    }
    if (!args.empty()) {
        std::cerr << "usage: qalcbot [--eval \"expression\" | --selftest]\n";
        return 2;
    }

    const char *token = std::getenv("DISCORD_BOT_TOKEN");
    if (!token || !*token) {
        std::cerr << "error: DISCORD_BOT_TOKEN environment variable is not set.\n"
                     "Set it to your Discord application bot token before starting qalcbot.\n";
        return 1;
    }
    return run_bot(token);
}
