CREATE EXTENSION IF NOT EXISTS "pgcrypto";

CREATE TABLE IF NOT EXISTS holdings (
    id          UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    user_id     UUID NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    symbol      TEXT NOT NULL,
    shares      NUMERIC(18,6) NOT NULL DEFAULT 0,
    UNIQUE(user_id, symbol)
);

CREATE INDEX IF NOT EXISTS idx_holdings_user_id ON holdings(user_id);