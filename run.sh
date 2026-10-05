#!/usr/bin/env bash
#
# One command to get the simulator running.
#
#   ./run.sh          bring everything up and start the server
#   ./run.sh stop     stop the server, and the database if this script started it
#   ./run.sh reset    stop, then delete the managed database for a fresh start
#   ./run.sh psql     open a psql shell against whichever database .env points at
#
# If .env already exists it is the source of truth for where the database is,
# and this script works with that. Only when .env is absent, or points at the
# managed address below, does it create and run a private PostgreSQL cluster -
# made with initdb, so it needs no root and never touches a system-wide server.
#
# Every step is safe to repeat.

set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")"

APP_PORT=8080

# The address this script will manage a cluster at. Anything else in .env is
# assumed to be a database you look after yourself.
MANAGED_HOST=127.0.0.1
MANAGED_PORT=55432
MANAGED_USER=postgres
MANAGED_DB=trading_sim

step()  { printf '  %-12s %s\n' "$1" "$2"; }
note()  { printf '  %-12s %s\n' "" "$1"; }
die()   { printf '\n  %-12s %s\n\n' "error" "$1" >&2; exit 1; }

# ------------------------------------------------------------ where is the db

env_get() { [ -f .env ] && sed -n "s/^$1=//p" .env | tail -n 1 | tr -d '\r' || true; }

DB_HOST=$(env_get DB_HOST); DB_HOST=${DB_HOST:-$MANAGED_HOST}
DB_PORT=$(env_get DB_PORT); DB_PORT=${DB_PORT:-$MANAGED_PORT}
DB_NAME=$(env_get DB_NAME); DB_NAME=${DB_NAME:-$MANAGED_DB}
DB_USER=$(env_get DB_USER); DB_USER=${DB_USER:-$MANAGED_USER}
DB_PASS=$(env_get DB_PASSWORD)
export PGPASSWORD="${DB_PASS:-}"

if [ "$DB_HOST" = "$MANAGED_HOST" ] && [ "$DB_PORT" = "$MANAGED_PORT" ]; then
    MANAGE_DB=yes
else
    MANAGE_DB=no
fi

# PostgreSQL refuses a data directory it cannot chmod to 0700, and a repo on a
# Windows drive under WSL (9p) cannot do that. Prefer beside the code, fall back
# to the Linux home. TRADING_SIM_PGDATA overrides both.
REPO_PGDATA="$PWD/.pgdata"
HOME_PGDATA="${XDG_DATA_HOME:-$HOME/.local/share}/trading-sim/pgdata"

supports_private_dir() {
    mkdir -p "$1" 2>/dev/null || return 1
    chmod 700 "$1" 2>/dev/null || return 1
    [ "$(stat -c %a "$1" 2>/dev/null)" = "700" ]
}

if [ -n "${TRADING_SIM_PGDATA:-}" ]; then
    PGDATA="$TRADING_SIM_PGDATA"
elif [ -f "$REPO_PGDATA/PG_VERSION" ]; then
    PGDATA="$REPO_PGDATA"
elif [ -f "$HOME_PGDATA/PG_VERSION" ]; then
    PGDATA="$HOME_PGDATA"
elif supports_private_dir "$REPO_PGDATA"; then
    PGDATA="$REPO_PGDATA"
else
    rmdir "$REPO_PGDATA" 2>/dev/null || true   # only removes it if still empty
    PGDATA="$HOME_PGDATA"
fi

STATE_DIR="$(dirname "$PGDATA")"
PIDFILE="$STATE_DIR/server.pid"

# ---------------------------------------------------------------- environment

[ "$(uname -s)" = "Linux" ] || die "Run this inside WSL or Linux. The toolchain and PostgreSQL live there, not on Windows."

PGBIN=$(ls -d /usr/lib/postgresql/*/bin 2>/dev/null | sort -V | tail -n 1 || true)
if [ -z "$PGBIN" ]; then
    command -v psql >/dev/null 2>&1 || die "PostgreSQL client not found. Install it with: sudo apt install postgresql"
    PGBIN=$(dirname "$(command -v psql)")
fi

psql_db()  { "$PGBIN/psql" -h "$DB_HOST" -p "$DB_PORT" -U "$DB_USER" -d "$DB_NAME" -v ON_ERROR_STOP=1 -tAq "$@"; }
db_ready() { "$PGBIN/pg_isready" -h "$DB_HOST" -p "$DB_PORT" -q >/dev/null 2>&1; }

cluster_running() { [ -d "$PGDATA" ] && "$PGBIN/pg_ctl" -D "$PGDATA" status >/dev/null 2>&1; }

stop_cluster() {
    if [ "$MANAGE_DB" = no ]; then
        step "database" "left alone (.env points at $DB_HOST:$DB_PORT)"
    elif cluster_running; then
        "$PGBIN/pg_ctl" -D "$PGDATA" -m fast stop >/dev/null 2>&1 || true
        step "database" "stopped"
    else
        step "database" "already stopped"
    fi
}

# A listening socket is the only reliable "is it up" signal: it catches an
# instance started by hand just as well as one started by this script.
port_in_use() {
    (exec 3<>"/dev/tcp/127.0.0.1/$1") 2>/dev/null && exec 3>&- && return 0
    return 1
}

server_pid() {
    [ -f "$PIDFILE" ] || return 1
    local pid
    pid=$(cat "$PIDFILE" 2>/dev/null) || return 1
    [ -n "$pid" ] && kill -0 "$pid" 2>/dev/null || return 1
    printf '%s' "$pid"
}

stop_server() {
    local pid
    if pid=$(server_pid); then
        kill "$pid" 2>/dev/null || true
        for _ in $(seq 20); do
            kill -0 "$pid" 2>/dev/null || break
            sleep 0.2
        done
        kill -9 "$pid" 2>/dev/null || true
        rm -f "$PIDFILE"
        step "server" "stopped"
    elif port_in_use "$APP_PORT"; then
        rm -f "$PIDFILE"
        step "server" "something is on port $APP_PORT but this script did not start it"
        note "stop it yourself, or: pkill -x trading_sim"
    else
        rm -f "$PIDFILE"
        step "server" "not running"
    fi
}

# -------------------------------------------------------------- subcommands

case "${1:-run}" in
    stop)
        echo
        stop_server
        stop_cluster
        echo
        exit 0
        ;;
    reset)
        echo
        stop_server
        stop_cluster
        if [ "$MANAGE_DB" = no ]; then
            note "nothing deleted: .env points at a database this script does not manage"
            note "to clear it yourself: TRUNCATE trades, holdings, users CASCADE;"
        else
            rm -rf "$PGDATA"
            step "database" "deleted $PGDATA"
            note "the next run starts from an empty account"
        fi
        note "the build in build/ was left alone"
        echo
        exit 0
        ;;
    psql)
        db_ready || die "Cannot reach the database at $DB_HOST:$DB_PORT. Start it with: ./run.sh"
        exec "$PGBIN/psql" -h "$DB_HOST" -p "$DB_PORT" -U "$DB_USER" -d "$DB_NAME"
        ;;
    run) ;;
    *)
        die "Unknown command '$1'. Use: run, stop, reset or psql."
        ;;
esac

echo

# ------------------------------------------------------------------ database

if [ "$MANAGE_DB" = yes ]; then
    # PG_VERSION, not the directory: the probe above may have left an empty
    # .pgdata behind, and an empty directory is not an initialised cluster.
    if [ ! -f "$PGDATA/PG_VERSION" ]; then
        mkdir -p "$(dirname "$PGDATA")"
        initdb_log=$(mktemp)
        if ! "$PGBIN/initdb" -D "$PGDATA" -U "$MANAGED_USER" --auth=trust --encoding=UTF8 >"$initdb_log" 2>&1; then
            tail -n 5 "$initdb_log" >&2
            rm -f "$initdb_log"
            die "initdb failed."
        fi
        rm -f "$initdb_log"
        step "database" "created at $PGDATA"
    fi

    if cluster_running; then
        step "database" "already running on port $DB_PORT"
    else
        # Loopback only. Trust auth is safe here because nothing off this
        # machine can reach the socket or the port.
        "$PGBIN/pg_ctl" -D "$PGDATA" -l "$PGDATA/server.log" \
            -o "-p $DB_PORT -k $PGDATA -c listen_addresses=$MANAGED_HOST" -w start >/dev/null 2>&1 \
            || die "Could not start PostgreSQL. Check $PGDATA/server.log"
        step "database" "started on port $DB_PORT"
    fi

    if ! "$PGBIN/psql" -h "$DB_HOST" -p "$DB_PORT" -U "$DB_USER" -d postgres -tAqc \
            "SELECT 1 FROM pg_database WHERE datname = '$DB_NAME'" | grep -q 1; then
        "$PGBIN/createdb" -h "$DB_HOST" -p "$DB_PORT" -U "$DB_USER" "$DB_NAME"
        step "database" "created '$DB_NAME'"
    fi
else
    # Someone else's database, named by .env. Use it, do not manage it.
    db_ready || die "Cannot reach $DB_HOST:$DB_PORT (from .env). Start that server, or point .env at $MANAGED_HOST:$MANAGED_PORT to let this script manage one."
    psql_db -c "SELECT 1" >/dev/null 2>&1 \
        || die "Reached $DB_HOST:$DB_PORT but could not open '$DB_NAME' as '$DB_USER'. Check the DB_ settings in .env."
    step "database" "using $DB_USER@$DB_HOST:$DB_PORT/$DB_NAME from .env"
fi

# Every migration is written to be idempotent, so reapplying them all is both
# the simplest thing to do and what keeps an older database up to date.
applied=0
for f in sql/*.sql; do
    psql_db -f "$f" >/dev/null 2>&1 || die "Migration failed: $f"
    applied=$((applied + 1))
done
step "migrations" "$applied applied"

# -------------------------------------------------------------------- config

if [ ! -f .env ]; then
    cat > .env <<ENV
DB_HOST=$MANAGED_HOST
DB_PORT=$MANAGED_PORT
DB_NAME=$MANAGED_DB
DB_USER=$MANAGED_USER
DB_PASSWORD=local
ALPACA_API_KEY=replace_me
ALPACA_SECRET_KEY=replace_me
ALPACA_BASE_URL=https://data.alpaca.markets
STARTING_BALANCE=100000.00
DB_POOL_SIZE=8
ENV
    step "config" ".env created"
else
    step "config" ".env found, using it"
fi

have_keys=yes
if grep -qE '^ALPACA_(API|SECRET)_KEY=(replace_me)?$' .env; then
    have_keys=no
fi

# --------------------------------------------------------------------- build

need_build=no
if [ ! -x build/trading_sim ]; then
    need_build=yes
elif [ -n "$(find src tests CMakeLists.txt conanfile.txt -newer build/trading_sim 2>/dev/null | head -n 1)" ]; then
    need_build=yes
fi

if [ "$need_build" = yes ]; then
    command -v conan >/dev/null 2>&1 || die "conan not found. Install it with: pip install conan"
    command -v cmake >/dev/null 2>&1 || die "cmake not found. Install it with: sudo apt install cmake"

    mkdir -p build

    if [ ! -f build/conan_toolchain.cmake ]; then
        step "build" "fetching dependencies (first run, this takes a while)"
        conan profile detect --force >/dev/null 2>&1 || true
        conan install . --output-folder=build --build=missing -s build_type=Release >/dev/null \
            || die "conan install failed. Re-run it by hand to see why."
    fi

    step "build" "compiling"

    # Keep the transcript quiet but keep the detail for when it matters.
    # Building on a Windows drive under WSL also emits harmless clock-skew
    # warnings that would otherwise clutter every single run.
    build_log=$(mktemp)

    if ! cmake -S . -B build \
            -DCMAKE_TOOLCHAIN_FILE="$PWD/build/conan_toolchain.cmake" \
            -DCMAKE_BUILD_TYPE=Release >"$build_log" 2>&1; then
        tail -n 20 "$build_log" >&2
        rm -f "$build_log"
        die "cmake configure failed."
    fi

    if ! cmake --build build -j"$(nproc)" >"$build_log" 2>&1; then
        grep -iE "error" "$build_log" | head -n 20 >&2 || tail -n 20 "$build_log" >&2
        rm -f "$build_log"
        die "Build failed. For the full output run: cmake --build build"
    fi

    rm -f "$build_log"

    step "build" "done"
else
    step "build" "up to date"
fi

# -------------------------------------------------------------------- prices

price_count=$(psql_db -c "SELECT count(*) FROM prices")

if [ "$price_count" -eq 0 ] && [ "$have_keys" = no ]; then
    # Without credentials the feed cannot fill this table, and an empty table
    # means nothing to trade. Placeholder marks so the app is usable at once.
    psql_db >/dev/null <<'SQL'
INSERT INTO prices (symbol, price) VALUES
  ('AAPL',255.40),('MSFT',517.20),('GOOGL',246.10),('AMZN',219.80),('TSLA',436.00),
  ('META',735.50),('NVDA',187.30),('JPM',312.60),('BAC',52.40),('WMT',104.90)
ON CONFLICT (symbol) DO NOTHING;
SQL
    step "prices" "seeded with placeholders (no Alpaca keys in .env)"
    note "add ALPACA_API_KEY and ALPACA_SECRET_KEY to .env for live prices and charts"
elif [ "$have_keys" = no ]; then
    step "prices" "$price_count placeholder symbols (no Alpaca keys in .env)"
else
    step "prices" "live from Alpaca"
fi

# ----------------------------------------------------------------------- run

if port_in_use "$APP_PORT"; then
    die "Port $APP_PORT is already in use. Stop the running server with: ./run.sh stop"
fi

echo
step "ready" "http://localhost:$APP_PORT"
note "ctrl-c to stop the server; the database keeps running for a fast restart"
note "./run.sh stop   shuts both down"
echo

mkdir -p "$STATE_DIR"
echo $$ > "$PIDFILE"
exec ./build/trading_sim
