## qalcBot

A Discord bot that uses [libqalculate](https://github.com/Qalculate/libqalculate) to evaluate mathematical expressions and return nicely formatted results.

It answers plain math messages (e.g. `1+1`, `142*5/2`), unit conversions (`10 km to miles`) and live currency conversions (`1 usd to try`) automatically, and offers a `/calculate` slash command that bypasses the message filters.

### Usage

- **Configure**: Set the `DISCORD_BOT_TOKEN` environment variable to your bot token. Enable the privileged **MESSAGE CONTENT INTENT** for the application in the [Discord Developer Portal](https://discord.com/developers/applications) (Bot -> Privileged Gateway Intents), or the bot cannot read messages to answer automatically.
- **Run**: Build and run the executable target; the bot will connect to Discord and respond to messages with evaluated results.
- **Docker**: `docker build --platform linux/amd64 -t qalcbot . && docker run --rm -e DISCORD_BOT_TOKEN=<token> qalcbot`

Currency exchange rates are downloaded at startup and refreshed hourly.

### Development

The CMake project needs libqalculate (via pkg-config) and D++ (dpp) 10.x; when no dpp package is installed, CMake fetches a pinned dpp and links it statically.

```
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Two offline test modes are built in, neither of which talks to Discord:

- `qalcbot --selftest` runs the full filter + evaluation pipeline against a table of known inputs.
- `qalcbot --eval "expression"` evaluates a single expression and prints the result.

### License

qalcBot is licensed under the **GPL-3.0-or-later**. This is not arbitrary: it links [libqalculate](https://github.com/Qalculate/libqalculate) (GPL-2.0-or-later), so distributed binaries — including the container image — must carry GPL-compatible terms, and the Apache-2.0 license of [D++](https://dpp.dev) is one-way compatible with the GPL v3 only. See [LICENSE](LICENSE).

### Credits

- **libqalculate**: This project builds on the excellent work in the open-source [`Qalculate/libqalculate`](https://github.com/Qalculate/libqalculate) library and its CLI, `qalc`, whose engine defaults, message handling and exchange-rate update logic this bot mirrors.
- **D++ (dpp)**: For the Discord gateway and HTTP client.
