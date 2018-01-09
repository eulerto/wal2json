#!/bin/bash

echo "Building wal2json.so"
PATH=$PATH:/usr/pgsql-9.5/bin/
local_dir=$(pwd)
cd $local_dir && \
rm -f wal2json.o wal2json.so && \
rm -rf ./results && \
USE_PGXS=1 make && \
USE_PGXS=1 make install && \
# USE_PGXS=1 make installcheck
rm -f wal2json.o wal2json.so 

