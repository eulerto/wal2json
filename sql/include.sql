\set VERBOSITY terse

-- predictability
SET synchronous_commit = on;

DROP TABLE IF EXISTS t1;
DROP TABLE IF EXISTS t2;
DROP TABLE IF EXISTS t3;

CREATE TABLE t1 (id int PRIMARY KEY);
CREATE TABLE t2 (id int PRIMARY KEY);
CREATE TABLE t3 (id int PRIMARY KEY);
CREATE TABLE s1 (id int PRIMARY KEY);

SELECT 'init' FROM pg_create_logical_replication_slot('regression_slot', 'wal2json');


-- Include commands in single tables

insert into t1 values (1);
insert into t2 values (2);
insert into t3 values (3);
update t1 set id = 10 where id = 1;
update t2 set id = 20 where id = 2;
update t3 set id = 30 where id = 3;
delete from t1 where id = 10;
delete from t2 where id = 20;
delete from t3 where id = 30;

SELECT data FROM pg_logical_slot_get_changes(
	'regression_slot', NULL, NULL, 'include-xids', '0', 'pretty-print', '1',
	'include-table', 't1', 'include-table', 't3');


-- Include commands on a pattern of tables

insert into t1 values (1);
insert into t2 values (2);
insert into s1 values (3);

SELECT data FROM pg_logical_slot_get_changes(
	'regression_slot', NULL, NULL, 'include-xids', '0', 'pretty-print', '1',
	'include-table', '~t');


insert into t1 values (4);
insert into t2 values (5);
insert into s1 values (6);

SELECT data FROM pg_logical_slot_get_changes('regression_slot', NULL, NULL,
	'include-xids', '0', 'pretty-print', '1', 'skip-empty-xacts', '1',
	'include-table', '~^.1$');


-- Exclude a table after inclusion

insert into t1 values (7);
insert into t2 values (8);
insert into t3 values (9);
insert into s1 values (10);

SELECT data FROM pg_logical_slot_get_changes('regression_slot', NULL, NULL,
	'include-xids', '0', 'pretty-print', '1', 'skip-empty-xacts', '1',
	'include-table', '~^t', 'exclude-table', 't2');


-- Exclude a single table

insert into t1 values (11);
insert into t2 values (12);
insert into t3 values (13);


SELECT data FROM pg_logical_slot_get_changes('regression_slot', NULL, NULL,
	'include-xids', '0', 'pretty-print', '1', 'skip-empty-xacts', '1',
	'exclude-table', 't2');


-- Exclude a pattern

insert into t1 values (14);
insert into t2 values (15);
insert into t3 values (16);
insert into s1 values (17);


SELECT data FROM pg_logical_slot_get_changes('regression_slot', NULL, NULL,
	'include-xids', '0', 'pretty-print', '1', 'skip-empty-xacts', '1',
	'exclude-table', '~.1');


-- Include after exclusion

insert into t1 values (18);
insert into t2 values (19);
insert into t3 values (20);
insert into s1 values (21);


SELECT data FROM pg_logical_slot_get_changes('regression_slot', NULL, NULL,
	'include-xids', '0', 'pretty-print', '1', 'skip-empty-xacts', '1',
	'exclude-table', '~t', 'include-table', '~.2');


SELECT 'stop' FROM pg_drop_replication_slot('regression_slot');
