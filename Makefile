MODULES = wal2json

REGRESS = cmdline insert1 update1 update2 update3 update4 delete1 delete2 \
		  delete3 delete4 savepoint specialvalue toast bytea message typmod \
		  filtertable selecttable include_timestamp include_lsn include_xids \
		  include_domain_data_type truncate type_oid actions position default \
		  pk rename_column numeric_data_types_as_string partition \
		  include_empty_transaction

# specialvalue test uses Unicode escapes (\uXXXX) whose expected output
# contains non-ASCII characters, hence, the regression database must be
# created as UTF8. Otherwise, it fails if the cluster was initialized with
# another encoding (such as SQL_ASCII).
REGRESS_OPTS = --encoding=UTF8 --temp-instance=tmp_check --temp-config=wal2json.conf

PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)

# truncate API is available in 11+
ifeq ($(MAJORVERSION),10)
REGRESS := $(filter-out truncate, $(REGRESS))
# include_empty_transaction uses a primary key on a partitioned table (11+)
REGRESS := $(filter-out include_empty_transaction, $(REGRESS))
endif

# actions API is available in 11+
# this test should be executed in prior versions, however, truncate will fail.
ifeq ($(MAJORVERSION),10)
REGRESS := $(filter-out actions, $(REGRESS))
endif

