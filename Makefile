MODULES = wal2json

REGRESS = basic
REGRESS_OPTS = --temp-config=./logical.conf

PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)
