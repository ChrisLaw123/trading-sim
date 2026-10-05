-- There are no passwords in this simulator; players are identified by name only.
-- This drops the credential column from any database that still has one.
-- A no-op on a database created from the current 001.
ALTER TABLE users DROP COLUMN IF EXISTS password_hash;
