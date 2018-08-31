[![Coverity Scan Build Status](https://scan.coverity.com/projects/4832/badge.svg)](https://scan.coverity.com/projects/wal2json)

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

There are several ways to build **wal2json** on Windows. If you are build PostgreSQL too, you can put **wal2json** directory inside contrib, change the contrib Makefile (variable SUBDIRS) and build it following the [Installation from Source Code on Windows](http://www.postgresql.org/docs/current/static/install-windows.html) instructions. However, if you already have PostgreSQL installed, it is also possible to compile **wal2json** out of the tree. Edit `wal2json.vcxproj` file and change `c:\postgres\pg103` to the PostgreSQL prefix directory. The next step is to open this project file in MS Visual Studio and compile it. Final step is to copy `wal2json.dll` to the `pg_config --pkglibdir` directory.

Docker
------

If your local system does not have Postgres installed, or your system's architecture or Postgres version do not
match that of your target database server, for instance because you develop on a Mac, but your database runs on
a Linux system, you can compile inside Docker.

This will produce a 64 bit Linux shared object for the Postgres version of your choice.

```
$ make docker
sed "s/TARGET_VERSION/9.6/" Dockerfile.tmpl > Dockerfile && \
	docker build -t wal2json:dev .
Sending build context to Docker daemon  8.306MB
Step 1/6 : FROM postgres:9.6
 ---> 6eeaec6956fc
Step 2/6 : RUN apt-get update
 ---> Using cache
 ---> 84e031ceed79
Step 3/6 : RUN apt-get install -y autoconf automake g++ libtool make postgresql-server-dev-9.6
 ---> Using cache
 ---> 59374cbd9d27
Step 4/6 : WORKDIR /src
 ---> Using cache
 ---> 3a37931ed387
Step 5/6 : ENV USE_PGXS=1
 ---> Using cache
 ---> 61c81e3f80b3
Step 6/6 : CMD make
 ---> Using cache
 ---> 3c66b60de834
Successfully built 3c66b60de834
Successfully tagged wal2json:dev
docker run --rm -v /Users/evzijst/work/permpublisher/contrib/wal2json:/src wal2json:dev
gcc -Wall -Wmissing-prototypes -Wpointer-arith -Wdeclaration-after-statement -Wendif-labels -Wmissing-format-attribute -Wformat-security -fno-strict-aliasing -fwrapv -fexcess-precision=standard -g -g -O2 -fdebug-prefix-map=.=. -fstack-protector-strong -Wformat -Werror=format-security -fno-omit-frame-pointer -fPIC -I. -I./ -I/usr/include/postgresql/9.6/server -I/usr/include/postgresql/internal -Wdate-time -D_FORTIFY_SOURCE=2 -D_GNU_SOURCE -I/usr/include/libxml2  -I/usr/include/mit-krb5  -c -o wal2json.o wal2json.c
gcc -Wall -Wmissing-prototypes -Wpointer-arith -Wdeclaration-after-statement -Wendif-labels -Wmissing-format-attribute -Wformat-security -fno-strict-aliasing -fwrapv -fexcess-precision=standard -g -g -O2 -fdebug-prefix-map=.=. -fstack-protector-strong -Wformat -Werror=format-security -fno-omit-frame-pointer -fPIC -L/usr/lib/x86_64-linux-gnu -Wl,-z,relro -Wl,-z,now  -L/usr/lib/x86_64-linux-gnu/mit-krb5 -Wl,--as-needed  -shared -o wal2json.so wal2json.o
```

By default, the Docker rule compiles against Postgres 9.6. To target a different version:

```
$ TARGET_VERSION=9.4 make docker
```

When targeting a specific version, specify only the major and minor version components ("9.6", not "9.6.8").

Configuration
=============

postgresql.conf
---------------

You need to set up at least two parameters at postgresql.conf:

```
wal_level = logical
max_replication_slots = 1
```

After changing these parameters, a restart is needed.

Parameters
----------

* `include-xids`: add _xid_ to each changeset. Default is _false_.
* `include-timestamp`: add _timestamp_ to each changeset. Default is _false_.
* `include-schemas`: add _schema_ to each change. Default is _true_.
* `include-types`: add _type_ to each change. Default is _true_.
* `include-typmod`: add modifier to types that have it (eg. varchar(20) instead of varchar). Default is _true_.
* `include-type-oids`: add type oids. Default is _false_.
* `include-not-null`: add _not null_ information as _columnoptionals_. Default is _false_.
* `pretty-print`: add spaces and indentation to JSON structures. Default is _false_.
* `write-in-chunks`: write after every change instead of every changeset. Default is _false_.
* `include-lsn`: add _nextlsn_ to each changeset. Default is _false_.
* `include-unchanged-toast`: add TOAST value even if it was not modified. Since TOAST values are usually large, this option could save IO and bandwidth if it is disabled. Default is _true_.
* `filter-tables`: exclude rows from the specified tables. Default is empty which means that no table will be filtered. It is a comma separated value. The tables should be schema-qualified. `*.foo` means table foo in all schemas and `bar.*` means all tables in schema bar. Special characters (space, single quote, comma, period, asterisk) must be escaped with backslash. Schema and table are case-sensitive. Table `"public"."Foo bar"` should be specified as `public.Foo\ bar`.
* `add-tables`: include only rows from the specified tables. Default is all tables from all schemas. It has the same rules from `filter-tables`.

Examples
========

There are two ways to obtain the changes (JSON objects) from **wal2json** plugin: calling functions via SQL or pg_recvlogical.

pg_recvlogical
--------------

Besides the configuration above, it is necessary to configure a replication connection to use pg_recvlogical.

First, add a replication connection rule at pg_hba.conf:

```
local    replication     myuser                     trust
```

Also, set max_wal_senders at postgresql.conf:

```
max_wal_senders = 1
```

A restart is necessary if you changed max_wal_senders.

You are ready to try **wal2json**. In one terminal:

```
$ pg_recvlogical -d postgres --slot test_slot --create-slot -P wal2json
$ pg_recvlogical -d postgres --slot test_slot --start -o pretty-print=1 -f -
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

$ psql -At -f /tmp/example1.sql postgres
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
{
	"change": [
		{
			"kind": "insert",
			"schema": "public",
			"table": "table_with_pk",
			"columnnames": ["a", "b", "c"],
			"columntypes": ["integer", "character varying(30)", "timestamp without time zone"],
			"columnvalues": [1, "Backup and Restore", "2018-03-27 11:58:28.988414"]
		}
		,{
			"kind": "insert",
			"schema": "public",
			"table": "table_with_pk",
			"columnnames": ["a", "b", "c"],
			"columntypes": ["integer", "character varying(30)", "timestamp without time zone"],
			"columnvalues": [2, "Tuning", "2018-03-27 11:58:28.988414"]
		}
		,{
			"kind": "insert",
			"schema": "public",
			"table": "table_with_pk",
			"columnnames": ["a", "b", "c"],
			"columntypes": ["integer", "character varying(30)", "timestamp without time zone"],
			"columnvalues": [3, "Replication", "2018-03-27 11:58:28.988414"]
		}
		,{
			"kind": "delete",
			"schema": "public",
			"table": "table_with_pk",
			"oldkeys": {
				"keynames": ["a", "c"],
				"keytypes": ["integer", "timestamp without time zone"],
				"keyvalues": [1, "2018-03-27 11:58:28.988414"]
			}
		}
		,{
			"kind": "delete",
			"schema": "public",
			"table": "table_with_pk",
			"oldkeys": {
				"keynames": ["a", "c"],
				"keytypes": ["integer", "timestamp without time zone"],
				"keyvalues": [2, "2018-03-27 11:58:28.988414"]
			}
		}
		,{
			"kind": "insert",
			"schema": "public",
			"table": "table_without_pk",
			"columnnames": ["a", "b", "c"],
			"columntypes": ["integer", "numeric(5,2)", "text"],
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
CREATE TABLE table2_with_pk (a SERIAL, b VARCHAR(30), c TIMESTAMP NOT NULL, PRIMARY KEY(a, c));
CREATE TABLE table2_without_pk (a SERIAL, b NUMERIC(5,2), c TEXT);

SELECT 'init' FROM pg_create_logical_replication_slot('test_slot', 'wal2json');

BEGIN;
INSERT INTO table2_with_pk (b, c) VALUES('Backup and Restore', now());
INSERT INTO table2_with_pk (b, c) VALUES('Tuning', now());
INSERT INTO table2_with_pk (b, c) VALUES('Replication', now());
DELETE FROM table2_with_pk WHERE a < 3;

INSERT INTO table2_without_pk (b, c) VALUES(2.34, 'Tapir');
-- it is not added to stream because there isn't a pk or a replica identity
UPDATE table2_without_pk SET c = 'Anta' WHERE c = 'Tapir';
COMMIT;

SELECT data FROM pg_logical_slot_get_changes('test_slot', NULL, NULL, 'pretty-print', '1');
SELECT 'stop' FROM pg_drop_replication_slot('test_slot');
```

The script above produces the output below:

```
$ psql -At -f /tmp/example2.sql postgres
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
psql:/tmp/example2.sql:17: WARNING:  table "table2_without_pk" without primary key or replica identity is nothing
{
	"change": [
		{
			"kind": "insert",
			"schema": "public",
			"table": "table2_with_pk",
			"columnnames": ["a", "b", "c"],
			"columntypes": ["integer", "character varying(30)", "timestamp without time zone"],
			"columnvalues": [1, "Backup and Restore", "2018-03-27 12:05:29.914496"]
		}
		,{
			"kind": "insert",
			"schema": "public",
			"table": "table2_with_pk",
			"columnnames": ["a", "b", "c"],
			"columntypes": ["integer", "character varying(30)", "timestamp without time zone"],
			"columnvalues": [2, "Tuning", "2018-03-27 12:05:29.914496"]
		}
		,{
			"kind": "insert",
			"schema": "public",
			"table": "table2_with_pk",
			"columnnames": ["a", "b", "c"],
			"columntypes": ["integer", "character varying(30)", "timestamp without time zone"],
			"columnvalues": [3, "Replication", "2018-03-27 12:05:29.914496"]
		}
		,{
			"kind": "delete",
			"schema": "public",
			"table": "table2_with_pk",
			"oldkeys": {
				"keynames": ["a", "c"],
				"keytypes": ["integer", "timestamp without time zone"],
				"keyvalues": [1, "2018-03-27 12:05:29.914496"]
			}
		}
		,{
			"kind": "delete",
			"schema": "public",
			"table": "table2_with_pk",
			"oldkeys": {
				"keynames": ["a", "c"],
				"keytypes": ["integer", "timestamp without time zone"],
				"keyvalues": [2, "2018-03-27 12:05:29.914496"]
			}
		}
		,{
			"kind": "insert",
			"schema": "public",
			"table": "table2_without_pk",
			"columnnames": ["a", "b", "c"],
			"columntypes": ["integer", "numeric(5,2)", "text"],
			"columnvalues": [1, 2.34, "Tapir"]
		}
	]
}
stop
```

License
=======

> Copyright (c) 2013-2018, Euler Taveira de Oliveira
> All rights reserved.

> Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:

> Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.

> Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.

> Neither the name of the Euler Taveira de Oliveira nor the names of its contributors may be used to endorse or promote products derived from this software without specific prior written permission.

> THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
