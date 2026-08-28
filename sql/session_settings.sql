\set VERBOSITY terse

-- predictability
SET synchronous_commit = on;

DROP TABLE IF EXISTS xpto;

CREATE TABLE xpto (
id			int primary key,
ts			timestamptz,
d			date,
iv			interval,
f			float8
);

SELECT 'init' FROM pg_create_logical_replication_slot('regression_slot', 'wal2json');

INSERT INTO xpto VALUES (1, '2020-02-03 04:05:06.789+00', '2020-02-03', '1 year 2 months 3 days 04:05:06', 1048576.00390625);

-- the emitted values do not follow the reader's formatting settings
SET DateStyle = 'SQL, DMY';
SET IntervalStyle = 'sql_standard';
SET TimeZone = 'America/New_York';
SET extra_float_digits = -3;
SELECT data FROM pg_logical_slot_peek_changes('regression_slot', NULL, NULL, 'format-version', '1');
SELECT data FROM pg_logical_slot_peek_changes('regression_slot', NULL, NULL, 'format-version', '2');

-- reading the slot leaves the session alone
SHOW DateStyle;
SHOW IntervalStyle;
SHOW TimeZone;
SHOW extra_float_digits;

RESET DateStyle;
RESET IntervalStyle;
RESET TimeZone;
RESET extra_float_digits;
SELECT data FROM pg_logical_slot_peek_changes('regression_slot', NULL, NULL, 'format-version', '2');

SELECT 'stop' FROM pg_drop_replication_slot('regression_slot');
