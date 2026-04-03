CREATE EXTENSION IF NOT EXISTS "pgcrypto";

CREATE TABLE IF NOT EXISTS users (
    id          UUID PRIMARY KEY DEFAULT gen_random_uuid(),    
    username    TEXT UNIQUE NOT NULL,
    balance     NUMERIC(18,2) NOT NULL DEFAULT 100000.00,
    created_at  TIMESTAMPTZ NOT NULL DEFAULT now()
);