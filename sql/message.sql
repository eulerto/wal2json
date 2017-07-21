\set VERBOSITY terse

-- predictability
SET synchronous_commit = on;

CREATE TABLE message_table (a smallserial, b smallint);

SELECT 'init' FROM pg_create_logical_replication_slot('regression_slot', 'wal2json');

-- One message
SELECT 'emit' FROM pg_logical_emit_message(true, 'foo', 'bar');
SELECT data FROM pg_logical_slot_get_changes('regression_slot', NULL, NULL);

-- Two messages
BEGIN;
SELECT 'emit' FROM pg_logical_emit_message(true, 'foo', 'bar');
SELECT 'emit' FROM pg_logical_emit_message(true, 'foo', 'bar');
COMMIT;
SELECT data FROM pg_logical_slot_get_changes('regression_slot', NULL, NULL);

-- Message sandwiched by changes
BEGIN;
INSERT INTO message_table (b) VALUES (1);
SELECT 'emit' FROM pg_logical_emit_message(true, 'foo', 'bar');
INSERT INTO message_table (b) VALUES (2);
COMMIT;
SELECT data FROM pg_logical_slot_get_changes('regression_slot', NULL, NULL);

-- Changes sandwiched by messages
BEGIN;
SELECT 'emit' FROM pg_logical_emit_message(true, 'before', 'before');
INSERT INTO message_table (b) VALUES (3);
SELECT 'emit' FROM pg_logical_emit_message(true, 'after', 'after');
COMMIT;
SELECT data FROM pg_logical_slot_get_changes('regression_slot', NULL, NULL);

-- Changes with newlines and quotes
SELECT 'emit' FROM pg_logical_emit_message(true, 'funky prefix', 'well " " " "" "" '' '' 
'' '' ] [ { } foo');
SELECT data FROM pg_logical_slot_get_changes('regression_slot', NULL, NULL);

-- Pretty
BEGIN;
SELECT 'emit' FROM pg_logical_emit_message(true, 'pretty prefix', 'pretty1');
SELECT 'emit' FROM pg_logical_emit_message(true, 'pretty prefix', 'pretty2');
COMMIT;
SELECT data FROM pg_logical_slot_get_changes('regression_slot', NULL, NULL, 'pretty-print', '1');

-- Small message renders correct size
SELECT 'emit' FROM pg_logical_emit_message(true, 'a pretty prefix that is long', 'a');
SELECT data FROM pg_logical_slot_get_changes('regression_slot', NULL, NULL, 'pretty-print', '1');

-- Non-transactional messages
BEGIN;
INSERT INTO message_table (b) VALUES (1);
SELECT 'emit' FROM pg_logical_emit_message(false, 'non-transactional prefix', 'non-transactional message');
INSERT INTO message_table (b) VALUES (2);
COMMIT;
SELECT data FROM pg_logical_slot_get_changes('regression_slot', NULL, NULL);

-- Standalone non-transactional messages
SELECT 'emit' FROM pg_logical_emit_message(false, 'non-transactional prefix', 'standalone non-transactional message');
SELECT pg_sleep(0.5); -- need a slight wait for the message to make it to decoding
SELECT data FROM pg_logical_slot_get_changes('regression_slot', NULL, NULL);

-- Bytea
SELECT 'emit' FROM pg_logical_emit_message(true, 'bytea prefix', decode('DEADBEEF', 'hex'));
SELECT data FROM pg_logical_slot_get_changes('regression_slot', NULL, NULL);

SELECT 'stop' FROM pg_drop_replication_slot('regression_slot');
