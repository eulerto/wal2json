Introduction
============

**wal2json** is an output plugin for logical decoding. It means that the plugin have access to tuples produced by INSERT and UPDATE. Also, UPDATE/DELETE old row versions can be accessed depending on the configured replica identity. Changes can be consumed using the streaming protocol (logical replication slots) or by a special SQL API.

The **wal2json** output plugin produces a JSON object per transaction. All of the new/old tuples are available in the JSON object. Also, there are options to include properties such as transaction timestamp, schema-qualified, data types, and transaction ids.

**wal2json** is released under PostgreSQL license.

Requirements
============

* PostgreSQL 9.4+

Build and Install
=================

This extension is supported on [those platforms](http://www.postgresql.org/docs/current/static/supported-platforms.html) that PostgreSQL is. The installation steps depend on your operating system.

You can also keep up with the latest fixes and features cloning the Git repository.

```
$ git clone https://github.com/eulerto/wal2json.git
```

Unix based Operating Systems
----------------------------

Before use this extension, you should build it and load it at the desirable database.

```
$ git clone https://github.com/eulerto/wal2json.git
$ PATH=/path/to/bin/pg_config:$PATH
$ USE_PGXS=1 make
$ USE_PGXS=1 make install
```

Windows
-------

Sorry, never tried it. ;(

Configuration
=============

You need to set up at least two parameters at postgresql.conf:

```
wal_level = logical
max_replication_slots = 1
```

After changing these parameters, a restart is needed.

Parameters
==========

Parameters list supported:

```
| Parameter Name    | Values  | Default  |
|-------------------|---------|----------|
| include-xids      | 1 / 0   | 1        |
| filter-tables     | String  | Empty    |
| force-toast-table | 1 / 0   | 1        |
| include-timestamp | 1 / 0   | 1        |
| include-schemas   | 1 / 0   | 1        |
| include-types     | 1 / 0   | 1        |
| pretty-print      | 1 / 0   | 0        |
| write-in-chunks   | 1 / 0   | 0        |
| include-lsn       | 1 / 0   | 0        |
| include-not-null  | 1 / 0   | 1        |
| include-typmod    | 1 / 0   | 1        |
| include-type-oids | 1 / 0   | 1        |

Sample "filter-tables": 
	SELECT data FROM pg_logical_slot_get_changes('test_slot', NULL, NULL, 'filter-tables', 'schema-name_1.table_1,schema-name_2.table_2,');

Note:
	Every table is splitted with comma (with final comma).
```

Examples
========

There are two ways to obtain the changes (JSON objects) from **wal2json** plugin: (i) calling functions via SQL or (ii) pg_recvlogical.

pg_recvlogical
--------------

Besides the configuration above, it is necessary to configure a replication connection to use pg_recvlogical.

First, add an entry at pg_hba.conf:

```
local    replication     myuser                     trust
host     replication     myuser     10.1.2.3/32     trust
```

Also, set max_wal_senders at postgresql.conf:

```
max_wal_senders = 1
```

A restart is necessary if you changed max_wal_senders.

You are ready to try wal2json. In one terminal:

```
$ pg_recvlogical -d postgres --slot test_slot --create-slot -P wal2json
$ pg_recvlogical -d postgres --slot test_slot --start -o pretty-print=1 -o write-in-chunks=0 -f -
```

In another terminal:

```
$ cat /tmp/example1.sql
CREATE TABLE table_with_pk (a SERIAL, b VARCHAR(30), c TIMESTAMP NOT NULL, PRIMARY KEY(a, c));
CREATE TABLE table_without_pk (a SERIAL, b NUMERIC(5,2), c TEXT);

BEGIN;
INSERT INTO table_with_pk (b, c) VALUES('Backup and Restore', now());
INSERT INTO table_with_pk (b, c) VALUES('Tuning', now());
INSERT INTO table_with_pk (b, c) VALUES('Replication', now());
DELETE FROM table_with_pk WHERE a < 3;

INSERT INTO table_without_pk (b, c) VALUES(2.34, 'Tapir');
-- it is not added to stream because there isn't a pk or a replica identity
UPDATE table_without_pk SET c = 'Anta' WHERE c = 'Tapir';
COMMIT;

$ psql -At -f /tmp/example1.sql
CREATE TABLE
CREATE TABLE
BEGIN
INSERT 0 1
INSERT 0 1
INSERT 0 1
DELETE 2
INSERT 0 1
UPDATE 1
COMMIT
```

The output in the first terminal is:

```
{
	"change": [
	]
}
{
	"change": [
	]
}
WARNING:  table "table_without_pk" without primary key or replica identity is nothing
CONTEXTO:  slot "test_slot", output plugin "wal2json", in the change callback, associated LSN 0/126E5F70
{
	"change": [
		{
			"kind": "insert",
			"schema": "public",
			"table": "table_with_pk",
			"columnnames": ["a", "b", "c"],
			"columntypes": ["int4", "varchar", "timestamp"],
			"columnvalues": [1, "Backup and Restore", "2015-08-27 16:46:35.818038"]
		}
		,{
			"kind": "insert",
			"schema": "public",
			"table": "table_with_pk",
			"columnnames": ["a", "b", "c"],
			"columntypes": ["int4", "varchar", "timestamp"],
			"columnvalues": [2, "Tuning", "2015-08-27 16:46:35.818038"]
		}
		,{
			"kind": "insert",
			"schema": "public",
			"table": "table_with_pk",
			"columnnames": ["a", "b", "c"],
			"columntypes": ["int4", "varchar", "timestamp"],
			"columnvalues": [3, "Replication", "2015-08-27 16:46:35.818038"]
		}
		,{
			"kind": "delete",
			"schema": "public",
			"table": "table_with_pk",
			"oldkeys": {
				"keynames": ["a", "c"],
				"keytypes": ["int4", "timestamp"],
				"keyvalues": [1, "2015-08-27 16:46:35.818038"]
			}
		}
		,{
			"kind": "delete",
			"schema": "public",
			"table": "table_with_pk",
			"oldkeys": {
				"keynames": ["a", "c"],
				"keytypes": ["int4", "timestamp"],
				"keyvalues": [2, "2015-08-27 16:46:35.818038"]
			}
		}
		,{
			"kind": "insert",
			"schema": "public",
			"table": "table_without_pk",
			"columnnames": ["a", "b", "c"],
			"columntypes": ["int4", "numeric", "text"],
			"columnvalues": [1, 2.34, "Tapir"]
		}
	]
}
```

Dropping the slot in the first terminal:

```
Ctrl+C
$ pg_recvlogical -d postgres --slot test_slot --drop-slot
```

SQL functions
-------------

```
$ cat /tmp/example2.sql
CREATE TABLE table_with_pk (a SERIAL, b VARCHAR(30), c TIMESTAMP NOT NULL, PRIMARY KEY(a, c));
CREATE TABLE table_without_pk (a SERIAL, b NUMERIC(5,2), c TEXT);

SELECT 'init' FROM pg_create_logical_replication_slot('test_slot', 'wal2json');

BEGIN;
INSERT INTO table_with_pk (b, c) VALUES('Backup and Restore', now());
INSERT INTO table_with_pk (b, c) VALUES('Tuning', now());
INSERT INTO table_with_pk (b, c) VALUES('Replication', now());
DELETE FROM table_with_pk WHERE a < 3;

INSERT INTO table_without_pk (b, c) VALUES(2.34, 'Tapir');
-- it is not added to stream because there isn't a pk or a replica identity
UPDATE table_without_pk SET c = 'Anta' WHERE c = 'Tapir';
COMMIT;

SELECT data FROM pg_logical_slot_get_changes('test_slot', NULL, NULL, 'pretty-print', '1', 'write-in-chunks', '0');
SELECT 'stop' FROM pg_drop_replication_slot('test_slot');
```

The script above produces the output below:

```
$ psql -At -f /tmp/example2.sql
CREATE TABLE
CREATE TABLE
init
BEGIN
INSERT 0 1
INSERT 0 1
INSERT 0 1
DELETE 2
INSERT 0 1
UPDATE 1
COMMIT
psql:/tmp/example2.sql:17: WARNING:  table "table_without_pk" without primary key or replica identity is nothing
CONTEXTO:  slot "test_slot", output plugin "wal2json", in the change callback, associated LSN 0/12713E40
{
	"change": [
		{
			"kind": "insert",
			"schema": "public",
			"table": "table_with_pk",
			"columnnames": ["a", "b", "c"],
			"columntypes": ["int4", "varchar", "timestamp"],
			"columnvalues": [1, "Backup and Restore", "2015-08-27 16:49:37.218511"]
		}
		,{
			"kind": "insert",
			"schema": "public",
			"table": "table_with_pk",
			"columnnames": ["a", "b", "c"],
			"columntypes": ["int4", "varchar", "timestamp"],
			"columnvalues": [2, "Tuning", "2015-08-27 16:49:37.218511"]
		}
		,{
			"kind": "insert",
			"schema": "public",
			"table": "table_with_pk",
			"columnnames": ["a", "b", "c"],
			"columntypes": ["int4", "varchar", "timestamp"],
			"columnvalues": [3, "Replication", "2015-08-27 16:49:37.218511"]
		}
		,{
			"kind": "delete",
			"schema": "public",
			"table": "table_with_pk",
			"oldkeys": {
				"keynames": ["a", "c"],
				"keytypes": ["int4", "timestamp"],
				"keyvalues": [1, "2015-08-27 16:49:37.218511"]
			}
		}
		,{
			"kind": "delete",
			"schema": "public",
			"table": "table_with_pk",
			"oldkeys": {
				"keynames": ["a", "c"],
				"keytypes": ["int4", "timestamp"],
				"keyvalues": [2, "2015-08-27 16:49:37.218511"]
			}
		}
		,{
			"kind": "insert",
			"schema": "public",
			"table": "table_without_pk",
			"columnnames": ["a", "b", "c"],
			"columntypes": ["int4", "numeric", "text"],
			"columnvalues": [1, 2.34, "Tapir"]
		}
	]
}
stop
```

License
=======

> Copyright (c) 2013-2017, Euler Taveira de Oliveira
> All rights reserved.

> Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:

> Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.

> Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.

> Neither the name of the Euler Taveira de Oliveira nor the names of its contributors may be used to endorse or promote products derived from this software without specific prior written permission.

> THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
