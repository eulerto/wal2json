#!/bin/bash

PATH=$PATH:/usr/local/go/bin:/usr/lib/postgresql/9.5/bin/
cd /workspace/src/gitlab.subito.int/development/regress-db/wal2json/ && \
rm -f wal2json.o wal2json.so && \
USE_PGXS=1 make && \
# USE_PGXS=1 make installcheck && \
USE_PGXS=1 make install && rm -f wal2json.o wal2json.so 
