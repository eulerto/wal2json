\set VERBOSITY terse

-- predictability
SET synchronous_commit = on;

DROP TABLE IF EXISTS missing_toast;

CREATE TABLE missing_toast (
id				serial primary key,
an_integer	integer,
toasted_col1	text,
toasted_col2	text
);

SELECT 'init' FROM pg_create_logical_replication_slot('regression_slot', 'wal2json');

-- uncompressed external toast data
INSERT INTO missing_toast (toasted_col1, toasted_col2) SELECT string_agg(g.i::text, ''), string_agg((g.i*2)::text, '') FROM generate_series(1, 2000) g(i);

-- update of existing column
UPDATE missing_toast SET toasted_col1 = (SELECT string_agg((g.i*2)::text, '') FROM generate_series(1, 2000) g(i)) where id = 1;

UPDATE missing_toast set an_integer = 1 where id = 1;

UPDATE missing_toast SET toasted_col1 = (SELECT string_agg((g.i*2)::text, '') FROM generate_series(1, 2000) g(i)), 
toasted_col2 = (SELECT string_agg((g.i*2)::text, '') FROM generate_series(1, 2000) g(i))
where id = 1;

SELECT data FROM pg_logical_slot_get_changes('regression_slot', NULL, NULL, 'pretty-print', '1', 'include-typmod', '0', 'include-missing-toast', '1');
SELECT 'stop' FROM pg_drop_replication_slot('regression_slot');
