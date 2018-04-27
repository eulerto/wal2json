\set VERBOSITY terse

-- predictability
SET synchronous_commit = on;

SELECT 'init' FROM pg_create_logical_replication_slot('regression_slot', 'wal2json');

SELECT data FROM pg_logical_slot_get_changes('regression_slot', NULL, NULL, 'nosuchopt', '42');

-- don't include not-null constraint by default
DROP TABLE IF EXISTS table_optional;
CREATE TABLE table_optional (
a smallserial,
b integer,
c boolean not null,
PRIMARY KEY(a)
);
INSERT INTO table_optional (b, c) VALUES(NULL, TRUE);
UPDATE table_optional SET b = 123 WHERE a = 1;
DELETE FROM table_optional WHERE a = 1;
DROP TABLE table_optional;
SELECT data::jsonb - 'xid' FROM pg_logical_slot_get_changes('regression_slot', NULL, NULL, 'include-not-null', '1', 'msg-per-record', '1');

-- By default don't write in chunks
DROP TABLE IF EXISTS x;
CREATE TABLE x ();
DROP TABLE x;
SELECT data::jsonb - 'xid' FROM pg_logical_slot_peek_changes('regression_slot', NULL, NULL, 'msg-per-record', '1');
SELECT data::jsonb - 'xid' FROM pg_logical_slot_get_changes('regression_slot', NULL, NULL, 'msg-per-record', '1');

DROP TABLE IF EXISTS gimmexid;
CREATE TABLE gimmexid (id integer PRIMARY KEY);
INSERT INTO gimmexid values (1);
DROP TABLE gimmexid;
SELECT max(((data::json) -> 'xid')::text::int) < txid_current() FROM pg_logical_slot_peek_changes('regression_slot', NULL, NULL, 'msg-per-record', '1');
SELECT max(((data::json) -> 'xid')::text::int) + 10 > txid_current() FROM pg_logical_slot_peek_changes('regression_slot', NULL, NULL, 'msg-per-record', '1');


SELECT 'stop' FROM pg_drop_replication_slot('regression_slot');
