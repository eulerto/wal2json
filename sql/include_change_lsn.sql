\set VERBOSITY terse

-- predictability
SET synchronous_commit = on;

DROP TABLE IF EXISTS changelsn;
CREATE TABLE changelsn (a SERIAL PRIMARY KEY, b bool, c varchar(60), d real);

SELECT 'init' FROM pg_create_logical_replication_slot('regression_slot', 'wal2json');

INSERT INTO changelsn (b, c, d) VALUES('t', 'test1', '+inf');
SELECT count(*) = 1, count(distinct((((data::json)->'change'->>0)::json -> 'lsn')::TEXT)) = 1 FROM pg_logical_slot_get_changes('regression_slot', NULL, NULL, 'include-change-lsn', '1');

-- two rows should have two records and two distinct change lsns
INSERT INTO changelsn (b, c, d) VALUES('f', 'test2', 'nan');
DELETE FROM changelsn WHERE c = 'test1';
SELECT count(*) = 2, count(distinct((((data::json)->'change'->>0)::json -> 'lsn')::TEXT)) = 2 FROM pg_logical_slot_get_changes('regression_slot', NULL, NULL, 'include-change-lsn', '1');

SELECT 'stop' FROM pg_drop_replication_slot('regression_slot');
