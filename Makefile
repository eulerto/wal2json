MODULES = wal2json

# message test will fail for <= 9.5
REGRESS = cmdline insert1 update1 update2 update3 update4 delete1 delete2 \
		  delete3 delete4 savepoint specialvalue toast bytea typmod \
		  filtertable selecttable

PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)

HAS_MESSAGE = $(shell $(PG_CONFIG) --version | grep -qE " 9\.4| 9\.5" && echo no || echo yes)

ifeq ($(HAS_MESSAGE),yes)
REGRESS += message
endif


# make installcheck
#
# It can be run but you need to add the following parameters to
# postgresql.conf:
#
# wal_level = logical
# max_replication_slots = 4
#
# Also, you should start the server before executing it.
