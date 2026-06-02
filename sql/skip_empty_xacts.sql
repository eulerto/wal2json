-- Test skip-empty-xacts option (issue #106)
\set VERBOSITY terse

CREATE TABLE w2j_kept (a integer primary key);
CREATE TABLE w2j_filtered (b integer primary key);
CREATE TABLE w2j_part (a integer, t text, PRIMARY KEY (a, t)) PARTITION BY LIST (t);
CREATE TABLE w2j_part_one PARTITION OF w2j_part FOR VALUES IN ('one');
CREATE MATERIALIZED VIEW w2j_mv AS SELECT count(*) AS n FROM w2j_kept;

SELECT 'init' FROM pg_create_logical_replication_slot('regression_slot', 'wal2json');

-- workload: only the first INSERT survives add-tables filtering
INSERT INTO w2j_kept (a) VALUES (1);
INSERT INTO w2j_filtered (b) VALUES (1);
INSERT INTO w2j_part (a, t) VALUES (1, 'one');
CREATE TABLE w2j_ddl (c integer);
DROP TABLE w2j_ddl;
TRUNCATE w2j_filtered;

-- format v1: without skip-empty-xacts, filtered/DDL transactions produce empty changesets
SELECT data FROM pg_logical_slot_peek_changes('regression_slot', NULL, NULL, 'format-version', '1', 'add-tables', 'public.w2j_kept');
-- format v1: with skip-empty-xacts, empty transactions are gone
SELECT data FROM pg_logical_slot_peek_changes('regression_slot', NULL, NULL, 'format-version', '1', 'add-tables', 'public.w2j_kept', 'skip-empty-xacts', '1');
-- format v1: skip-empty-xacts with write-in-chunks
SELECT data FROM pg_logical_slot_peek_changes('regression_slot', NULL, NULL, 'format-version', '1', 'add-tables', 'public.w2j_kept', 'skip-empty-xacts', '1', 'write-in-chunks', '1');
-- format v2: without skip-empty-xacts
SELECT data FROM pg_logical_slot_peek_changes('regression_slot', NULL, NULL, 'format-version', '2', 'add-tables', 'public.w2j_kept');
-- format v2: with skip-empty-xacts
SELECT data FROM pg_logical_slot_get_changes('regression_slot', NULL, NULL, 'format-version', '2', 'add-tables', 'public.w2j_kept', 'skip-empty-xacts', '1');

-- messages: transactional message marks the transaction as non-empty;
-- non-transactional messages are unaffected; a transaction whose only
-- message is prefix-filtered is empty
SELECT 1 FROM pg_logical_emit_message(true, 'wal2json', 'kept message');
SELECT 1 FROM pg_logical_emit_message(false, 'wal2json', 'non-transactional message');
SELECT 1 FROM pg_logical_emit_message(true, 'filtered', 'filtered message');

-- format v1: filtered transactional message leaves an empty transaction without the option
SELECT data FROM pg_logical_slot_peek_changes('regression_slot', NULL, NULL, 'format-version', '1', 'filter-msg-prefixes', 'filtered');
-- format v1: with skip-empty-xacts the filtered-message transaction disappears
SELECT data FROM pg_logical_slot_peek_changes('regression_slot', NULL, NULL, 'format-version', '1', 'filter-msg-prefixes', 'filtered', 'skip-empty-xacts', '1');
-- format v2: with skip-empty-xacts
SELECT data FROM pg_logical_slot_get_changes('regression_slot', NULL, NULL, 'format-version', '2', 'filter-msg-prefixes', 'filtered', 'skip-empty-xacts', '1');

-- VACUUM FULL and REFRESH MATERIALIZED VIEW flood the slot with empty
-- transactions (issue #106); transaction counts vary across versions so
-- assert on counts, not raw output
VACUUM FULL w2j_kept;
REFRESH MATERIALIZED VIEW w2j_mv;
SELECT count(*) > 0 AS has_empty_xacts FROM pg_logical_slot_peek_changes('regression_slot', NULL, NULL, 'format-version', '1', 'add-tables', 'public.w2j_kept') WHERE data = '{"change":[]}';
SELECT count(*) AS remaining FROM pg_logical_slot_get_changes('regression_slot', NULL, NULL, 'format-version', '1', 'add-tables', 'public.w2j_kept', 'skip-empty-xacts', '1');

SELECT 'stop' FROM pg_drop_replication_slot('regression_slot');
DROP MATERIALIZED VIEW w2j_mv;
DROP TABLE w2j_kept, w2j_filtered, w2j_part;
