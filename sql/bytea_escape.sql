\set VERBOSITY terse

-- predictability
SET synchronous_commit = on;

DROP TABLE IF EXISTS xpto;

CREATE TABLE xpto (
id			int primary key,
bincol		bytea
);

SELECT 'init' FROM pg_create_logical_replication_slot('regression_slot', 'wal2json');

INSERT INTO xpto (id, bincol) VALUES (1, '\xdeadbeef'), (2, '\x00'), (3, ''), (4, decode('5461706972757300ff', 'hex'));

-- the emitted value does not depend on the reader's bytea_output
SET bytea_output = 'escape';
SELECT data FROM pg_logical_slot_peek_changes('regression_slot', NULL, NULL, 'format-version', '1');
SELECT data FROM pg_logical_slot_peek_changes('regression_slot', NULL, NULL, 'format-version', '2');
RESET bytea_output;
SELECT data FROM pg_logical_slot_peek_changes('regression_slot', NULL, NULL, 'format-version', '1');
SELECT data FROM pg_logical_slot_peek_changes('regression_slot', NULL, NULL, 'format-version', '2');

-- reading the slot leaves the session alone
SET bytea_output = 'escape';
SELECT count(*) > 0 FROM pg_logical_slot_peek_changes('regression_slot', NULL, NULL, 'format-version', '2');
SHOW bytea_output;
RESET bytea_output;

SELECT 'stop' FROM pg_drop_replication_slot('regression_slot');
