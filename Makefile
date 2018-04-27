MODULES = wal2json

# message test will fail for <= 9.5
REGRESS = cmdline insert1 update1 update2 update3 update4 delete1 delete2 \
		  delete3 delete4 savepoint specialvalue toast bytea message typmod \
		  filtertable selecttable \
	    bytea_alt cmdline_alt delete1_alt delete2_alt delete3_alt \
	    delete4_alt filtertable_alt insert1_alt message_alt \
	    savepoint_alt selecttable_alt specialvalue_alt toast_alt \
	    typmod_alt update1_alt update2_alt update3_alt update4_alt

PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)

# make installcheck
#
# It can be run but you need to add the following parameters to
# postgresql.conf:
#
# wal_level = logical
# max_replication_slots = 4
#
# Also, you should start the server before executing it.
