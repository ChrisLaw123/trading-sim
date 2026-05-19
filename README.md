# Trading Sim

A full-stack stock trading simulator built in C++ with a React frontend. Trade real US stocks using live market data with virtual money, compete on a leaderboard, and track your portfolio performance.
---

## What It Does

- **Live stock prices** pulled from Alpaca Markets API for 10 real US stocks
- **Buy and sell** stocks at real market prices with $100,000 virtual starting balance
- **Candlestick chart** showing 100 hours of historical price data per stock
- **Portfolio tracking** — cash, equity, unrealised PnL updated in real time
- **Trade history** — full log of every executed trade
- **Leaderboard** — all players ranked by total account value
- **Authentication** — register and log back in with a password

---

## Tech Stack

| Layer | Technology |
|---|---|
| Backend engine | C++20 |
| HTTP framework | Crow |
| Database | PostgreSQL 15 |
| C++ DB driver | libpqxx |
| Market data | Alpaca Markets API |
| HTTP client | libcurl |
| JSON | nlohmann/json |
| Build system | CMake + Conan |
| Frontend | React 18 (CDN, no build step) |
| Charts | Lightweight Charts (TradingView) |

---

## Architecture

```
Alpaca API
    ↓
C++ Price Feed (libcurl)
    ↓
PostgreSQL (prices, users, holdings, trades)
    ↑
C++ REST API (Crow)
    ↑
React Frontend (browser)
```

The C++ server handles all business logic — trade validation, portfolio calculations, leaderboard ranking. The frontend is a single HTML file served directly from the C++ server.

---

## Stock Universe

```
AAPL  MSFT  GOOGL  AMZN  TSLA
META  NVDA  JPM    BAC   WMT
```

---

## API Endpoints

| Method | Endpoint | Description |
|---|---|---|
| POST | `/api/users/register` | Create account |
| POST | `/api/login` | Log in |
| GET | `/api/prices` | All current prices |
| POST | `/api/trade` | Execute buy or sell |
| GET | `/api/portfolio/:id` | Portfolio + holdings |
| GET | `/api/history/:id` | Trade history |
| GET | `/api/leaderboard` | All players ranked |
| GET | `/api/config` | Alpaca keys for frontend chart |

---

## Database Schema

```sql
users       — id, username, password_hash, balance, created_at
prices      — symbol (PK), price, updated_at
holdings    — id, user_id (FK), symbol, shares
trades      — id, user_id (FK), symbol, side, shares, price, total, executed_at
```

Foreign keys with `ON DELETE CASCADE`. Indexes on `user_id` and `executed_at DESC`. Parameterized queries throughout — no SQL injection possible.

---

## Prerequisites

- Ubuntu / WSL2
- CMake 3.20+
- PostgreSQL 15
- Conan 2.x
- GCC 11+
- A free [Alpaca Markets](https://alpaca.markets) paper trading account

---

## Setup

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
GRANT ALL PRIVILEGES ON DATABASE trading_sim TO admin;
\q
```

Run migrations:

```bash
psql -U admin -d trading_sim -h localhost \
  -f sql/001_create_users.sql \
  -f sql/002_create_prices.sql \
  -f sql/003_create_holdings.sql \
  -f sql/004_create_trades.sql
```

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
```

Get your free Alpaca API keys at [alpaca.markets](https://alpaca.markets) → Paper Trading → API Keys.

### 5. Build

```bash
mkdir build && cd build
conan install .. --output-folder=. --build=missing -s build_type=Release
cmake .. -DCMAKE_TOOLCHAIN_FILE=conan_toolchain.cmake -DCMAKE_BUILD_TYPE=Release
cmake --build . -j$(nproc)
cd ..
```

### 6. Run

```bash
sudo service postgresql start
./build/trading_sim
```

Open your browser at `http://localhost:8080`.

---

## Security

- Passwords hashed before storage — never stored in plain text
- All SQL uses parameterized queries — injection impossible
- API keys stored in `.env` — never committed to version control
- Database user has only `SELECT, INSERT, UPDATE, DELETE` — no `DROP` or `ALTER`
- `.env` is in `.gitignore`

---

## Project Structure

```
trading-sim/
├── src/
│   ├── main.cpp
│   ├── Config.h
│   ├── api/
│   │   ├── Router.h
│   │   └── Validator.h
│   ├── db/
│   │   ├── Database.h
│   │   ├── UserRepository.h
│   │   ├── PriceRepository.h
│   │   ├── HoldingRepository.h
│   │   └── TradeRepository.h
│   ├── engine/
│   │   └── TradeEngine.h
│   ├── feed/
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
│   └── 004_create_trades.sql
├── frontend/
│   └── index.html
├── CMakeLists.txt
├── conanfile.txt
├── .env.example
└── README.md
```
