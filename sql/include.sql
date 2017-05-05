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

insert into t1 values (1);
insert into t2 values (2);
insert into t3 values (3);
update t1 set id = 10 where id = 1;
update t2 set id = 20 where id = 2;
update t3 set id = 30 where id = 3;
delete from t1 where id = 10;
delete from t2 where id = 20;
delete from t3 where id = 30;

SELECT data FROM pg_logical_slot_get_changes('regression_slot', NULL, NULL, 'include-xids', '0', 'pretty-print', '1', 'include-table', 't1', 'include-table', 't3');


insert into t1 values (1);
insert into t2 values (2);
insert into s1 values (3);

SELECT data FROM pg_logical_slot_get_changes('regression_slot', NULL, NULL, 'include-xids', '0', 'pretty-print', '1', 'include-table', '~t');


insert into t1 values (4);
insert into t2 values (5);
insert into s1 values (6);

SELECT data FROM pg_logical_slot_get_changes('regression_slot', NULL, NULL, 'include-xids', '0', 'pretty-print', '1', 'include-table', '~^.1$');


SELECT 'stop' FROM pg_drop_replication_slot('regression_slot');
