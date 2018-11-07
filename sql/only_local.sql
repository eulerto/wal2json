\set VERBOSITY terse

-- predictability
SET synchronous_commit = on;

CREATE TABLE t (a int);
SELECT pg_replication_origin_create('repl');

SELECT 'init' FROM pg_create_logical_replication_slot('regression_slot', 'wal2json');

INSERT INTO t VALUES (1);

SELECT pg_replication_origin_session_setup('repl');
BEGIN;
SELECT pg_replication_origin_xact_setup('0/1234567', current_timestamp);
INSERT INTO t VALUES (2);
COMMIT;
SELECT pg_replication_origin_session_reset();

INSERT INTO t VALUES (3);

\a
\t
SELECT data FROM pg_logical_slot_peek_changes('regression_slot', NULL, NULL);
SELECT data FROM pg_logical_slot_get_changes('regression_slot', NULL, NULL, 'only-local', '1');
\a
\t

SELECT 'stop' FROM pg_drop_replication_slot('regression_slot');

DROP TABLE t;
