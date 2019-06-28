\set VERBOSITY terse

-- predictability
SET synchronous_commit = on;

DROP TABLE IF EXISTS tbl;
CREATE TABLE tbl (id int);

DROP VIEW IF EXISTS tbl_view;
CREATE MATERIALIZED VIEW tbl_view AS select * from tbl;

SELECT 'init' FROM pg_create_logical_replication_slot('regression_slot', 'wal2json');

-- New slot should have zero changes
SELECT count(*) = 0 FROM pg_logical_slot_get_changes('regression_slot', NULL, NULL);

-- Refreshing Materialized Views generates empty transactions
REFRESH MATERIALIZED VIEW tbl_view;

-- New slot should have one change
SELECT count(*) = 1 FROM pg_logical_slot_get_changes('regression_slot', NULL, NULL);

-- Refreshing Materialized Views generates empty transactions
REFRESH MATERIALIZED VIEW tbl_view;

-- The plugin should ignore the empty transaction
SELECT count(*) = 0 FROM pg_logical_slot_get_changes('regression_slot', NULL, NULL, 'skip-empty-xacts', '1');

SELECT 'stop' FROM pg_drop_replication_slot('regression_slot');