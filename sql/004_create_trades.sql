CREATE EXTENSION IF NOT EXISTS "pgcrypto";

CREATE TABLE IF NOT EXISTS trades (
    id          UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    user_id     UUID NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    symbol      TEXT NOT NULL,
    side        TEXT NOT NULL CHECK(side IN('BUY','SELL')),
    shares      NUMERIC(18,6) NOT NULL,
    price       NUMERIC(18,6) NOT NULL,
    total       NUMERIC(18,2) NOT NULL,
    executed_at TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE INDEX IF NOT EXISTS idx_trades_user_id ON trades(user_id);
CREATE INDEX IF NOT EXISTS idx_trades_executed_at ON trades(executed_at DESC);