\set VERBOSITY terse

-- predictability
SET synchronous_commit = on;

DROP TABLE IF EXISTS tbl;
CREATE TABLE tbl (a integer, b integer, primary key(a));

SELECT 'init' FROM pg_create_logical_replication_slot('regression_slot', 'wal2json');

INSERT INTO tbl (a, b) VALUES(1,2);
UPDATE tbl SET a=3;
DELETE FROM tbl WHERE a=3;

-- without include-type-oids parameter
SELECT data FROM pg_logical_slot_peek_changes('regression_slot', NULL, NULL, 'format-version', '1');
SELECT data FROM pg_logical_slot_peek_changes('regression_slot', NULL, NULL, 'format-version', '2');

-- with include-type-oids parameter
SELECT data FROM pg_logical_slot_peek_changes('regression_slot', NULL, NULL, 'format-version', '1', 'include-type-oids', '1');
SELECT data FROM pg_logical_slot_peek_changes('regression_slot', NULL, NULL, 'format-version', '2', 'include-type-oids', '1');

SELECT 'stop' FROM pg_drop_replication_slot('regression_slot');
