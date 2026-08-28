\set VERBOSITY terse

-- predictability
SET synchronous_commit = on;

DROP TABLE IF EXISTS xpto;

CREATE TABLE xpto (
id			int primary key,
quote		text DEFAULT 'say "hi"',
backslash	text DEFAULT 'a\b',
control		text DEFAULT E'a\tb\nc'
);

SELECT 'init' FROM pg_create_logical_replication_slot('regression_slot', 'wal2json');

INSERT INTO xpto (id) VALUES (1);

-- a default expression is escaped like any other string
SELECT data FROM pg_logical_slot_peek_changes('regression_slot', NULL, NULL, 'format-version', '1', 'include-default', '1');
SELECT data FROM pg_logical_slot_peek_changes('regression_slot', NULL, NULL, 'format-version', '2', 'include-default', '1');

-- the emitted lines parse as JSON
SELECT data::json IS NOT NULL FROM pg_logical_slot_peek_changes('regression_slot', NULL, NULL, 'format-version', '1', 'include-default', '1');
SELECT data::json IS NOT NULL FROM pg_logical_slot_peek_changes('regression_slot', NULL, NULL, 'format-version', '2', 'include-default', '1');

SELECT 'stop' FROM pg_drop_replication_slot('regression_slot');
