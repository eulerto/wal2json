\set VERBOSITY terse

-- predictability
SET synchronous_commit = on;

SELECT 'init' FROM pg_create_logical_replication_slot('regression_slot', 'wal2json');

-- Unknown option
SELECT data FROM pg_logical_slot_get_changes('regression_slot', NULL, NULL,
	'nosuchopt', '42');

-- Regexp error
SELECT data FROM pg_logical_slot_get_changes('regression_slot', NULL, NULL,
	'include-table', '~(');

SELECT 'stop' FROM pg_drop_replication_slot('regression_slot');
