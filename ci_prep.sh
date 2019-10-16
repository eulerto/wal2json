#!/bin/bash
export PATH=/usr/lib/postgresql/10/bin:$PATH
cd $HOME/src/github.com/Hireology/wal2json
USE_PGXS=1 make
sudo USE_PGXS=1 make install

psql -c "alter system set wal_level = 'logical'"
psql -c "alter system set max_replication_slots = 1"
sudo service postgresql restart

