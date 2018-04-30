-- this is the first test (CREATE EXTENSION, no DROP TABLE)
LOAD 'test_decoding';

\set VERBOSITY terse

-- predictability
SET synchronous_commit = on;

DROP TABLE IF EXISTS table_with_pk;
DROP TABLE IF EXISTS table_without_pk;
DROP TABLE IF EXISTS table_with_unique;

CREATE TABLE table_with_pk (
a	smallserial,
b	smallint,
c	int,
d	bigint,
e	numeric(5,3),
f	real not null,
g	double precision,
h	char(10),
i	varchar(30),
j	text,
k	bit varying(20),
l	timestamp,
m	date,
n	boolean not null,
o	json,
p	tsvector,
PRIMARY KEY(b, c, d)
);

CREATE TABLE table_without_pk (
a	smallserial,
b	smallint,
c	int,
d	bigint,
e	numeric(5,3),
f	real not null,
g	double precision,
h	char(10),
i	varchar(30),
j	text,
k	bit varying(20),
l	timestamp,
m	date,
n	boolean not null,
o	json,
p	tsvector
);

CREATE TABLE table_with_unique (
a	smallserial,
b	smallint,
c	int,
d	bigint,
e	numeric(5,3) not null,
f	real not null,
g	double precision not null,
h	char(10),
i	varchar(30),
j	text,
k	bit varying(20),
l	timestamp,
m	date,
n	boolean not null,
o	json,
p	tsvector,
UNIQUE(g, n)
);

CREATE SCHEMA IF NOT EXISTS "test ""schema";

DROP TABLE IF EXISTS "test ""schema"."test "" with 'quotes'";
CREATE TABLE "test ""schema"."test "" with 'quotes'" (
    id            serial primary key,
    "col spaces"  int,
    "col""quotes" int
);

SELECT 'init' FROM pg_create_logical_replication_slot('regression_slot', 'wal2json');

-- INSERT
BEGIN;
INSERT INTO table_with_pk (b, c, d, e, f, g, h, i, j, k, l, m, n, o, p) VALUES(1, 2, 3, 3.54, 876.563452345, 1.23, 'teste', 'testando', 'um texto longo', B'001110010101010', '2013-11-02 17:30:52', '2013-02-04', true, '{ "a": 123 }', 'Old Old Parr'::tsvector);
INSERT INTO table_without_pk (b, c, d, e, f, g, h, i, j, k, l, m, n, o, p) VALUES(1, 2, 3, 3.54, 876.563452345, 1.23, 'teste', 'testando', 'um texto longo', B'001110010101010', '2013-11-02 17:30:52', '2013-02-04', true, '{ "a": 123 }', 'Old Old Parr'::tsvector);
INSERT INTO table_with_unique (b, c, d, e, f, g, h, i, j, k, l, m, n, o, p) VALUES(1, 2, 3, 3.54, 876.563452345, 1.23, 'teste', 'testando', 'um texto longo', B'001110010101010', '2013-11-02 17:30:52', '2013-02-04', true, '{ "a": 123 }', 'Old Old Parr'::tsvector);
INSERT INTO "test ""schema"."test "" with 'quotes'" VALUES (1, 2, 3);
COMMIT;

SELECT data::jsonb - 'xid' FROM pg_logical_slot_get_changes('regression_slot', NULL, NULL, 'pretty-print', '1', 'include-typmod', '0', 'msg-per-record', '1');
SELECT 'stop' FROM pg_drop_replication_slot('regression_slot');
