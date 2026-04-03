CREATE EXTENSION IF NOT EXISTS "pgcrypto";

CREATE TABLE IF NOT EXISTS prices (
    symbol      TEXT PRIMARY KEY,
    price       NUMERIC(18,6) NOT NULL,
    updated_at  TIMESTAMPTZ NOT NULL DEFAULT now()
);