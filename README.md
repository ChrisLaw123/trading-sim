# Trading Sim

A single-player stock trading simulator built in C++ with a React frontend. Trade real US stocks with virtual money using live market data, and track your portfolio performance.

---

## Quick start

```bash
./run.sh
```

That is the whole thing. On a clean checkout it creates a private PostgreSQL
cluster, applies the migrations, writes a `.env` if you do not already have one,
builds if anything changed, seeds placeholder prices if you have no Alpaca keys,
and starts the server at <http://localhost:8080>. Every step is safe to repeat,
so `./run.sh` is also how you restart it.

If a `.env` already exists it is never overwritten, and it decides where the
database is. Point `DB_HOST`/`DB_PORT` at your own PostgreSQL and the script
will use it and leave it alone — it only creates and manages a cluster when
`.env` names the managed address (`127.0.0.1:55432`) or is absent entirely.

On Windows you can double-click **`run.bat`** instead; it forwards to `run.sh`
inside WSL and opens the browser once the server answers.

| Command | Does |
|---|---|
| `./run.sh` | Bring everything up and serve |
| `./run.sh stop` | Stop the server, and the database if the script started it |
| `./run.sh reset` | Delete the managed database so the next run starts from a fresh $100,000 |
| `./run.sh psql` | Open a psql shell against whichever database `.env` points at |

`stop` and `reset` only ever touch a cluster this script created — a database of
your own is left alone.

The database is created with `initdb` and runs on port 55432, so it needs no
root and never touches a system-wide PostgreSQL. It lives beside the code at
`.pgdata`, or under `~/.local/share/trading-sim` when the repo sits on a
filesystem that cannot hold the `0700` permissions PostgreSQL insists on, which
is the case for a Windows drive mounted into WSL. Set `TRADING_SIM_PGDATA` to
put it somewhere else.

For live prices and charts, add your Alpaca paper-trading keys to `.env` and
run `./run.sh` again. Without them the app still works; it just trades against
the placeholder prices it seeded.

Everything below is reference: the manual setup if you would rather wire it
up yourself or point the app at an existing PostgreSQL, and notes on how the
pieces fit together.

---

## What It Does

- **Live stock prices** pulled from Alpaca Markets API for 10 real US stocks
- **Buy and sell** stocks at real market prices with $100,000 of virtual cash
- **Candlestick chart** showing 100 hours of historical price data per stock
- **Portfolio tracking** — cash, equity, unrealised PnL updated in real time
- **Trade history** — full log of every executed trade
- **Market status** — whether the exchange is open, and when it next opens or closes
- **No signup, no login** — open the page and trade

---

## Market hours

Prices come from real exchanges, so they only move when those exchanges are
open: weekdays 09:30 to 16:00 Eastern. Outside those hours Alpaca keeps
returning the last trade of the previous session, and the app will sit
completely still. Over a weekend that is roughly 62 hours of a frozen screen.

That is normal, and the top bar says so:

```
Market closed · opens Mon 09:30 AM     dim dot
Market open   · closes 04:00 PM        lit dot
```

The status bar makes the same distinction for the data itself. **"Last close,
fetched 22:14"** means the price is Friday's closing print and we checked for a
newer one at 22:14 — two different times that are easy to confuse, and which
previously made a working app look broken.

The background thread keeps polling on its usual one-minute cycle regardless;
there is simply nothing new to record.

---

## Tech Stack

| Layer | Technology |
|---|---|
| Backend engine | C++20 |
| HTTP framework | Crow |
| Database | PostgreSQL 15+ |
| C++ DB driver | libpqxx |
| Market data | Alpaca Markets API |
| HTTP client | libcurl |
| JSON | nlohmann/json |
| Build system | CMake + Conan |
| Tests | GoogleTest |
| Frontend | React 18 (CDN, no build step) |
| Charts | Lightweight Charts (TradingView) |

---

## Architecture

```
Alpaca API
    ↓
C++ Price Feed (libcurl, one background thread)
    ↓
PostgreSQL (prices, users, holdings, trades)
    ↑
C++ REST API (Crow, pooled connections)
    ↑
React Frontend (browser)
```

The C++ server handles all business logic — trade validation and portfolio calculation. The frontend is a single HTML file served directly from the C++ server, so it talks to the API same-origin with no host to configure.

A single background thread is the only writer of the `prices` table; it refreshes prices and the market clock from Alpaca once a minute. Request handlers read prices from the database, so browser polling costs nothing upstream. Chart data is proxied through the server too, which keeps the Alpaca credentials out of the browser. The market clock is cached in memory by that same thread, so `/api/market` costs nothing per request.

---

## Single player

There is one account. It is created automatically the first time the app reads the
portfolio, funded with `STARTING_BALANCE`, and no route takes a player id.

The `users` table is deliberately still there, with one row in it. `holdings` and
`trades` keep their `user_id` foreign key. That costs nothing today and means adding
multiple players and a leaderboard later is a pure addition — new rows and new
endpoints — rather than a migration that has to put `user_id` back and backfill it.

What that future change would involve: put an id back on the portfolio, history and
trade routes, decide how a caller says which player it is, and add a `GROUP BY` query
ranking players by cash plus equity.

---

## Stock Universe

```
AAPL  MSFT  GOOGL  AMZN  TSLA
META  NVDA  JPM    BAC   WMT
```

Defined once in `src/Symbols.h` and shared by the price feed and the trade validator.

---

## API Endpoints

| Method | Endpoint | Description |
|---|---|---|
| GET | `/api/prices` | All current prices |
| GET | `/api/market` | Whether the market is open, and when it next opens or closes |
| GET | `/api/bars/:symbol` | Hourly candles, proxied from Alpaca |
| POST | `/api/trade` | Execute a buy or sell |
| GET | `/api/portfolio` | Cash, equity, PnL and open positions |
| GET | `/api/history` | Trade history |

`POST /api/trade` takes `symbol`, `side` and `shares`.

---

## Database Schema

```sql
users       — id, username, balance, created_at      (one row)
prices      — symbol (PK), price, updated_at
holdings    — id, user_id (FK), symbol, shares
trades      — id, user_id (FK), symbol, side, shares, price, total, executed_at
```

Foreign keys with `ON DELETE CASCADE`. Indexes on `user_id` and `executed_at DESC`.
Money and share quantities are `NUMERIC`, never floating point, and `CHECK` constraints
reject negative balances and negative share counts. Parameterized queries throughout.

A trade is a single transaction: the cash movement, the holdings change and the trade
record commit together or not at all. The account row is locked for the duration (the
upsert that fetches it performs a write, which takes the lock), and the balance and
share updates are relative and guarded (`WHERE balance + $1 >= 0`), so concurrent
requests cannot spend the same cash twice or sell into a negative position.

---

## Prerequisites

- Ubuntu / WSL2
- CMake 3.20+
- PostgreSQL 15+
- Conan 2.x
- GCC 11+
- A free [Alpaca Markets](https://alpaca.markets) paper trading account

---

## Manual setup

You do not need this section if you use `./run.sh`, which does all of it for you
with its own database. Follow these steps only to build by hand or to run against a
PostgreSQL you manage yourself.

### 1. Clone the repo

```bash
git clone https://github.com/ChrisLaw123/trading-sim
cd trading-sim
```

### 2. Install dependencies

```bash
sudo apt install -y cmake libpq-dev build-essential
pip install conan
conan profile detect --force
```

### 3. Set up PostgreSQL

```bash
sudo service postgresql start
sudo -u postgres psql
```

```sql
CREATE USER admin WITH PASSWORD 'your_password';
CREATE DATABASE trading_sim OWNER admin;
\q
```

Run migrations in order:

```bash
psql -U admin -d trading_sim -h localhost \
  -f sql/001_create_users.sql \
  -f sql/002_create_prices.sql \
  -f sql/003_create_holdings.sql \
  -f sql/004_create_trades.sql \
  -f sql/005_drop_password_hash.sql \
  -f sql/006_money_constraints.sql
```

`005` and `006` are idempotent and safe to re-run. They bring an older database up to
date; on a fresh one they are no-ops.

### 4. Configure environment

```bash
cp .env.example .env
```

Edit `.env`:

```
DB_HOST=localhost
DB_PORT=5432
DB_NAME=trading_sim
DB_USER=admin
DB_PASSWORD=your_password
ALPACA_API_KEY=your_key
ALPACA_SECRET_KEY=your_secret
ALPACA_BASE_URL=https://data.alpaca.markets
STARTING_BALANCE=100000.00
DB_POOL_SIZE=8
```

`STARTING_BALANCE` is the single source of truth for both the opening cash and the
baseline PnL is measured against. `DB_POOL_SIZE` is optional and defaults to 8. `ALPACA_TRADING_URL` is also
optional and defaults to `https://api.alpaca.markets` — Alpaca serves market data
and the market clock from two different hosts.

Get your free Alpaca API keys at [alpaca.markets](https://alpaca.markets) → Paper Trading → API Keys.

To start over with a fresh $100,000, clear the tables:

```bash
psql -U admin -d trading_sim -h localhost -c 'TRUNCATE trades, holdings, users CASCADE;'
```

### 5. Build

```bash
mkdir build && cd build
conan install .. --output-folder=. --build=missing -s build_type=Release
cmake .. -DCMAKE_TOOLCHAIN_FILE=conan_toolchain.cmake -DCMAKE_BUILD_TYPE=Release
cmake --build . -j$(nproc)
cd ..
```

### 6. Run the tests

```bash
ctest --test-dir build --output-on-failure
```

Unit tests cover input validation and `.env` parsing. The `run_tests` target is skipped
automatically if GoogleTest is not installed.

### 7. Run

```bash
sudo service postgresql start   # the system PostgreSQL from step 3
./build/trading_sim
```

This runs whatever is in `build/` and does not rebuild it, so run step 5 again after
pulling changes. `./run.sh` rebuilds automatically when sources change.

Open your browser at `http://localhost:8080`. The server looks for `.env` in the working
directory, so run it from wherever your `.env` lives; it finds `frontend/index.html`
whether you start from the project root or from `build/`.

---

## Security

This is a local single-player simulator and is built as one.

**What it does protect**

- Alpaca credentials never leave the server. Chart data is proxied through
  `/api/bars/:symbol` rather than called from the browser, so the keys are not in the
  page source.
- All SQL uses parameterized queries.
- Money and share quantities are `NUMERIC` with `CHECK` constraints, and trades are
  atomic and row-locked, so the books cannot be corrupted by concurrent requests.
- `.env` is in `.gitignore`.

**What it does not protect**

- There is no authentication, and nothing to authenticate — there is one account, and
  anyone who can reach the port can trade on it. That is the same trust boundary as
  handing someone the database.
- No TLS, no rate limiting, no CSRF protection.

**Do not expose this to the internet as-is.** Doing so safely means adding real accounts
first: a credential on the `users` table, a session store, and an ownership check on
every route.

Also note that the setup above makes `admin` the owner of the database, so the
application role can alter the schema. Splitting the migration role from the runtime
role would be the first step in hardening this.

---

## Project Structure

```
trading-sim/
├── src/
│   ├── main.cpp
│   ├── Config.h
│   ├── Symbols.h
│   ├── api/
│   │   ├── Router.h
│   │   └── Validator.h
│   ├── db/
│   │   ├── ConnectionPool.h
│   │   ├── Database.h
│   │   ├── UserRepository.h
│   │   ├── PriceRepository.h
│   │   ├── HoldingRepository.h
│   │   └── TradeRepository.h
│   ├── engine/
│   │   └── TradeEngine.h
│   ├── feed/
│   │   ├── MarketClock.h
│   │   ├── PriceFeed.h
│   │   └── PriceFeed.cpp
│   └── models/
│       ├── User.h
│       ├── Price.h
│       ├── Holding.h
│       └── Trade.h
├── sql/
│   ├── 001_create_users.sql
│   ├── 002_create_prices.sql
│   ├── 003_create_holdings.sql
│   ├── 004_create_trades.sql
│   ├── 005_drop_password_hash.sql
│   └── 006_money_constraints.sql
├── tests/
│   ├── test_config.cpp
│   └── test_validator.cpp
├── frontend/
│   └── index.html
├── CMakeLists.txt
├── conanfile.txt
├── run.sh                  one-command launcher
├── run.bat                 Windows double-click wrapper for run.sh
├── .env.example
└── README.md
```
