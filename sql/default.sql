\set VERBOSITY terse

-- predictability
SET synchronous_commit = on;

CREATE TABLE w2j_default (a serial, b integer DEFAULT 6, c text DEFAULT 'wal2json', d timestamp DEFAULT '2020-07-12 11:55:30', e integer DEFAULT NULL, f integer, PRIMARY KEY(a));
CREATE TABLE w2j_truncate (a serial primary key, b text not null);
-- default expressions whose text contains a double quote, a backslash and a
-- control character (tab); these must be escaped like every other string or
-- the emitted JSON is invalid
CREATE TABLE w2j_default_escape (a serial PRIMARY KEY, b text DEFAULT 'a "quoted" default', c text DEFAULT 'back\slash', d text DEFAULT E'tab\there');

SELECT 'init' FROM pg_create_logical_replication_slot('regression_slot', 'wal2json');

INSERT INTO w2j_default (b, c ,d, e, f) VALUES(2, 'test', '2020-03-01 08:09:00', 80, 10);
INSERT INTO w2j_default DEFAULT VALUES;
UPDATE w2j_default SET b = 3 WHERE a = 1;

INSERT INTO w2j_truncate (b) VALUES('foo@bar.com');
TRUNCATE w2j_truncate;
INSERT INTO w2j_truncate (b) VALUES('foo@bar.com');

-- without include-default parameter
SELECT data FROM pg_logical_slot_peek_changes('regression_slot', NULL, NULL, 'format-version', '1');
SELECT data FROM pg_logical_slot_peek_changes('regression_slot', NULL, NULL, 'format-version', '2');

-- with include-default parameter
SELECT data FROM pg_logical_slot_peek_changes('regression_slot', NULL, NULL, 'format-version', '1', 'include-default', '1');
SELECT data FROM pg_logical_slot_peek_changes('regression_slot', NULL, NULL, 'format-version', '2', 'include-default', '1');

-- drain the changes emitted above so the default-escaping check below is
-- reported on its own
SELECT count(*) > 0 AS drained FROM pg_logical_slot_get_changes('regression_slot', NULL, NULL, 'format-version', '1');

INSERT INTO w2j_default_escape DEFAULT VALUES;

-- show the escaped default expressions ...
SELECT data FROM pg_logical_slot_peek_changes('regression_slot', NULL, NULL, 'format-version', '1', 'include-default', '1');
SELECT data FROM pg_logical_slot_peek_changes('regression_slot', NULL, NULL, 'format-version', '2', 'include-default', '1');
-- ... and prove the output is valid JSON by casting every row to json
SELECT data::json IS NOT NULL AS valid FROM pg_logical_slot_peek_changes('regression_slot', NULL, NULL, 'format-version', '1', 'include-default', '1');
SELECT data::json IS NOT NULL AS valid FROM pg_logical_slot_get_changes('regression_slot', NULL, NULL, 'format-version', '2', 'include-default', '1');

SELECT 'stop' FROM pg_drop_replication_slot('regression_slot');

DROP TABLE w2j_default;
DROP TABLE w2j_truncate;
DROP TABLE w2j_default_escape;
