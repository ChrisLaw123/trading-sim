-- Defence in depth for the trade path: even if application logic regresses,
-- the database refuses to hold negative cash or negative share counts.
-- Postgres has no ADD CONSTRAINT IF NOT EXISTS, hence the catalog checks.

DO $$
BEGIN
    IF NOT EXISTS (
        SELECT 1 FROM pg_constraint WHERE conname = 'users_balance_non_negative'
    ) THEN
        ALTER TABLE users
            ADD CONSTRAINT users_balance_non_negative CHECK (balance >= 0);
    END IF;

    IF NOT EXISTS (
        SELECT 1 FROM pg_constraint WHERE conname = 'holdings_shares_non_negative'
    ) THEN
        ALTER TABLE holdings
            ADD CONSTRAINT holdings_shares_non_negative CHECK (shares >= 0);
    END IF;
END
$$;
