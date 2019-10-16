#!/bin/bash
cd $HOME/src/github.com/Hireology/wal2json
USE_PGXS=1 PG_CONFIG=/usr/lib/postgresql/10/bin/pg_config make -e
sudo USE_PGXS=1 PG_CONFIG=/usr/lib/postgresql/10/bin/pg_config make -e install

psql -c "alter system set wal_level = 'logical'"
psql -c "alter system set max_replication_slots = 1"
sudo service postgresql restart

