MODULES = wal2json

REGRESS = cmdline insert1 update1 update2 update3 update4 delete1 delete2 \
		  delete3 delete4 savepoint specialvalue toast bytea message typmod \
		  filtertable selecttable include_timestamp include_lsn include_xids \
		  include_domain_data_type truncate actions position default pk rename_column

EXTRA_CLEAN = Dockerfile
PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)

# message API is available in 9.6+
ifneq (,$(findstring $(MAJORVERSION),9.4 9.5))
REGRESS := $(filter-out message, $(REGRESS))
endif

# truncate API is available in 11+
ifneq (,$(findstring $(MAJORVERSION),9.4 9.5 9.6 10))
REGRESS := $(filter-out truncate, $(REGRESS))
endif

# actions API is available in 11+
# this test should be executed in prior versions, however, truncate will fail.
ifneq (,$(findstring $(MAJORVERSION),9.4 9.5 9.6 10))
REGRESS := $(filter-out actions, $(REGRESS))
endif

# make installcheck
#
# It can be run but you need to add the following parameters to
# postgresql.conf:
#
# wal_level = logical
# max_replication_slots = 10
#
# Also, you should start the server before executing it.

# When compiling inside Docker, use env var $DOCKER_TARGET to specify which
# Postgres version (major:minor) to build against. Default is 9.6.
DOCKER_TARGET?=9.6

.PHONY: image
image :
	sed "s/TARGET_VERSION/$(DOCKER_TARGET)/" Dockerfile.tmpl > Dockerfile && \
	docker build -t wal2json:dev .

.PHONY: docker
docker : image
	docker run --rm -v $(PWD):/src wal2json:dev
