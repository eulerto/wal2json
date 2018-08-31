MODULES = wal2json

# message test will fail for <= 9.5
REGRESS = cmdline insert1 update1 update2 update3 update4 delete1 delete2 \
		  delete3 delete4 savepoint specialvalue toast bytea message typmod \
		  filtertable selecttable

EXTRA_CLEAN = Dockerfile
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
