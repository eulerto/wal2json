\set VERBOSITY terse

-- predictability
SET synchronous_commit = on;

SELECT 'init' FROM pg_create_logical_replication_slot('regression_slot', 'wal2json');

DROP TABLE IF EXISTS nextxid ;

CREATE TABLE nextxid (id integer PRIMARY KEY);
INSERT INTO nextxid values (1);

-- convert 32 bit epoch and 32 bit xid into 64 bit from txid_current
SELECT ((max(((data::json) -> 'epoch')::text::int)::bit(32) << 32) | max(((data::json) -> 'nextxid')::text::int)::bit(32))::bigint = txid_current() FROM pg_logical_slot_peek_changes('regression_slot', NULL, NULL, 'include-next-xids', '1');

SELECT data FROM pg_logical_slot_peek_changes('regression_slot', NULL, NULL) where ((data::json) -> 'nextxid') IS NOT NULL;
SELECT 'stop' FROM pg_drop_replication_slot('regression_slot');
