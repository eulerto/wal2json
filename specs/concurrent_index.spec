# CREATE INDEX CONCURRENTLY vs logical decoding.
#
# An index created by CREATE INDEX CONCURRENTLY while an older transaction
# was still in progress used to make wal2json fail with
#   ERROR: could not open relation with OID <oid>
# when decoding that transaction's changes, permanently wedging the slot:
# decoding a transaction that committed after CIC's first phase caches the
# new (invalid) index in the table's rd_indexlist, and decoding the older
# transaction's UPDATE afterwards opened every listed index under an older
# historic snapshot to which the new index's pg_class row is not visible.
#
# The WHERE clause keeps the output independent of incidental empty
# transactions (the CIC phases themselves, autovacuum).

setup
{
    CREATE TABLE cic_t (id int PRIMARY KEY, v text);
    INSERT INTO cic_t VALUES (1, 'seed');
}

setup
{
    SELECT 'init' FROM pg_create_logical_replication_slot('isolation_slot', 'wal2json');
}

teardown
{
    SELECT 'stop' FROM pg_drop_replication_slot('isolation_slot');
    DROP TABLE cic_t;
}

session s1
step s1_begin  { BEGIN; }
step s1_update { UPDATE cic_t SET v = 'x2' WHERE id = 1; }
step s1_commit { COMMIT; }

session s2
step s2_cic    { CREATE INDEX CONCURRENTLY cic_i ON cic_t (v); }

session s3
step s3_insert { INSERT INTO cic_t VALUES (2, 'y'); }

session s4
step s4_get    { SELECT data FROM pg_logical_slot_get_changes('isolation_slot', NULL, NULL, 'format-version', '2') WHERE data LIKE '%cic_t%'; }

# s2_cic commits its first phase (catalog entries for the new index), then
# blocks in WaitForLockers on s1's open transaction; the isolation tester
# proceeds once the session is detected as waiting. s3's insert-only
# transaction is decoded first (caching the new index in rd_indexlist),
# then s1's older update trips the bug on unpatched wal2json.
permutation s1_begin s1_update s2_cic s3_insert s1_commit s4_get
