-- Test include-empty-transaction option
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

-- format v1: with include-empty-transaction (default), filtered/DDL transactions produce empty changesets
SELECT data FROM pg_logical_slot_peek_changes('regression_slot', NULL, NULL, 'format-version', '1', 'add-tables', 'public.w2j_kept');
-- format v1: without include-empty-transaction, empty transactions are gone
SELECT data FROM pg_logical_slot_peek_changes('regression_slot', NULL, NULL, 'format-version', '1', 'add-tables', 'public.w2j_kept', 'include-empty-transaction', '0');
-- format v1: include-empty-transaction=0 with write-in-chunks
SELECT data FROM pg_logical_slot_peek_changes('regression_slot', NULL, NULL, 'format-version', '1', 'add-tables', 'public.w2j_kept', 'include-empty-transaction', '0', 'write-in-chunks', '1');
-- format v2: with include-empty-transaction (default)
SELECT data FROM pg_logical_slot_peek_changes('regression_slot', NULL, NULL, 'format-version', '2', 'add-tables', 'public.w2j_kept');
-- format v2: without include-empty-transaction
SELECT data FROM pg_logical_slot_get_changes('regression_slot', NULL, NULL, 'format-version', '2', 'add-tables', 'public.w2j_kept', 'include-empty-transaction', '0');

-- messages: transactional message marks the transaction as non-empty;
-- non-transactional messages are unaffected; a transaction whose only
-- message is prefix-filtered is empty
SELECT 1 FROM pg_logical_emit_message(true, 'wal2json', 'kept message');
SELECT 1 FROM pg_logical_emit_message(false, 'wal2json', 'non-transactional message');
SELECT 1 FROM pg_logical_emit_message(true, 'filtered', 'filtered message');

-- format v1: filtered transactional message leaves an empty transaction by default
SELECT data FROM pg_logical_slot_peek_changes('regression_slot', NULL, NULL, 'format-version', '1', 'filter-msg-prefixes', 'filtered');
-- format v1: without include-empty-transaction the filtered-message transaction disappears
SELECT data FROM pg_logical_slot_peek_changes('regression_slot', NULL, NULL, 'format-version', '1', 'filter-msg-prefixes', 'filtered', 'include-empty-transaction', '0');
-- format v2: without include-empty-transaction
SELECT data FROM pg_logical_slot_get_changes('regression_slot', NULL, NULL, 'format-version', '2', 'filter-msg-prefixes', 'filtered', 'include-empty-transaction', '0');

-- VACUUM FULL and REFRESH MATERIALIZED VIEW flood the slot with empty
-- transactions; transaction counts vary across versions so assert on counts,
-- not raw output
VACUUM FULL w2j_kept;
REFRESH MATERIALIZED VIEW w2j_mv;
SELECT count(*) > 0 AS has_empty_xacts FROM pg_logical_slot_peek_changes('regression_slot', NULL, NULL, 'format-version', '1', 'add-tables', 'public.w2j_kept') WHERE data = '{"change":[]}';
SELECT count(*) AS remaining FROM pg_logical_slot_get_changes('regression_slot', NULL, NULL, 'format-version', '1', 'add-tables', 'public.w2j_kept', 'include-empty-transaction', '0');

-- a change that is not emitted (no tuple identifier) does not make the
-- transaction non-empty
CREATE TABLE w2j_nothing (a integer);
ALTER TABLE w2j_nothing REPLICA IDENTITY NOTHING;
INSERT INTO w2j_nothing (a) VALUES (1);
SELECT count(*) > 0 AS consumed FROM pg_logical_slot_get_changes('regression_slot', NULL, NULL);
UPDATE w2j_nothing SET a = 2;
DELETE FROM w2j_nothing;
SELECT data FROM pg_logical_slot_peek_changes('regression_slot', NULL, NULL, 'format-version', '1', 'include-empty-transaction', '0');
SELECT data FROM pg_logical_slot_get_changes('regression_slot', NULL, NULL, 'format-version', '2', 'include-empty-transaction', '0');

-- changes: filter-tables, actions, partition-root, and a transaction whose
-- first change is filtered out
BEGIN;
INSERT INTO w2j_filtered (b) VALUES (2);
INSERT INTO w2j_kept (a) VALUES (2);
INSERT INTO w2j_kept (a) VALUES (3);
COMMIT;
INSERT INTO w2j_filtered (b) VALUES (3);
DELETE FROM w2j_kept WHERE a = 3;
BEGIN;
SAVEPOINT s1;
INSERT INTO w2j_kept (a) VALUES (4);
ROLLBACK TO SAVEPOINT s1;
COMMIT;
INSERT INTO w2j_part (a, t) VALUES (2, 'one');
-- format v1
SELECT data FROM pg_logical_slot_peek_changes('regression_slot', NULL, NULL, 'format-version', '1', 'filter-tables', 'public.w2j_filtered', 'actions', 'insert', 'include-empty-transaction', '0');
SELECT data FROM pg_logical_slot_peek_changes('regression_slot', NULL, NULL, 'format-version', '1', 'filter-tables', 'public.w2j_filtered', 'actions', 'insert', 'include-empty-transaction', '0', 'write-in-chunks', '1');
SELECT data FROM pg_logical_slot_peek_changes('regression_slot', NULL, NULL, 'format-version', '1', 'filter-tables', 'public.w2j_filtered', 'actions', 'insert', 'include-empty-transaction', '0', 'pretty-print', '1');
SELECT data FROM pg_logical_slot_peek_changes('regression_slot', NULL, NULL, 'format-version', '1', 'add-tables', 'public.w2j_part', 'partition-root', '1', 'include-empty-transaction', '0');
-- format v2
SELECT data FROM pg_logical_slot_peek_changes('regression_slot', NULL, NULL, 'format-version', '2', 'filter-tables', 'public.w2j_filtered', 'actions', 'insert', 'include-empty-transaction', '0');
SELECT data FROM pg_logical_slot_peek_changes('regression_slot', NULL, NULL, 'format-version', '2', 'filter-tables', 'public.w2j_filtered', 'actions', 'insert', 'include-empty-transaction', '0', 'include-transaction', '0');
SELECT data::json->>'action' AS action, (data::json->>'xid') IS NOT NULL AS has_xid FROM pg_logical_slot_peek_changes('regression_slot', NULL, NULL, 'format-version', '2', 'add-tables', 'public.w2j_kept', 'actions', 'insert', 'include-empty-transaction', '0', 'include-xids', '1');
SELECT data FROM pg_logical_slot_get_changes('regression_slot', NULL, NULL, 'format-version', '2', 'add-tables', 'public.w2j_part', 'partition-root', '1', 'include-empty-transaction', '0');

-- TRUNCATE and messages: add-msg-prefixes, a transactional message that
-- starts the transaction, and a TRUNCATE excluded by actions
TRUNCATE w2j_kept, w2j_filtered;
TRUNCATE w2j_filtered;
BEGIN;
SELECT 1 FROM pg_logical_emit_message(true, 'wal2json', 'first message');
INSERT INTO w2j_kept (a) VALUES (5);
COMMIT;
BEGIN;
SELECT 1 FROM pg_logical_emit_message(true, 'other', 'other message');
COMMIT;
-- format v1 (TRUNCATE is not supported)
SELECT data FROM pg_logical_slot_peek_changes('regression_slot', NULL, NULL, 'format-version', '1', 'filter-tables', 'public.w2j_filtered', 'add-msg-prefixes', 'wal2json', 'include-empty-transaction', '0');
SELECT data FROM pg_logical_slot_peek_changes('regression_slot', NULL, NULL, 'format-version', '1', 'filter-tables', 'public.w2j_filtered', 'add-msg-prefixes', 'wal2json', 'include-empty-transaction', '0', 'write-in-chunks', '1');
-- format v2
SELECT data FROM pg_logical_slot_peek_changes('regression_slot', NULL, NULL, 'format-version', '2', 'filter-tables', 'public.w2j_filtered', 'add-msg-prefixes', 'wal2json', 'include-empty-transaction', '0');
SELECT data FROM pg_logical_slot_get_changes('regression_slot', NULL, NULL, 'format-version', '2', 'filter-tables', 'public.w2j_filtered', 'add-msg-prefixes', 'wal2json', 'actions', 'insert,update,delete', 'include-empty-transaction', '0');

-- format v2: include-transaction and include-empty-transaction; a
-- transaction without changes produces no output if either one is false
CREATE TABLE w2j_ddl (c integer);
DROP TABLE w2j_ddl;
INSERT INTO w2j_kept (a) VALUES (6);
SELECT data FROM pg_logical_slot_peek_changes('regression_slot', NULL, NULL, 'format-version', '2', 'add-tables', 'public.w2j_kept', 'include-transaction', '1', 'include-empty-transaction', '1');
SELECT data FROM pg_logical_slot_peek_changes('regression_slot', NULL, NULL, 'format-version', '2', 'add-tables', 'public.w2j_kept', 'include-transaction', '1', 'include-empty-transaction', '0');
SELECT data FROM pg_logical_slot_peek_changes('regression_slot', NULL, NULL, 'format-version', '2', 'add-tables', 'public.w2j_kept', 'include-transaction', '0', 'include-empty-transaction', '1');
SELECT data FROM pg_logical_slot_get_changes('regression_slot', NULL, NULL, 'format-version', '2', 'add-tables', 'public.w2j_kept', 'include-transaction', '0', 'include-empty-transaction', '0');

SELECT 'stop' FROM pg_drop_replication_slot('regression_slot');
DROP MATERIALIZED VIEW w2j_mv;
DROP TABLE w2j_kept, w2j_filtered, w2j_part, w2j_nothing;
