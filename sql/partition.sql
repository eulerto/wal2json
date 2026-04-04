-- predictability
SET synchronous_commit = on;
SET extra_float_digits = 0;

CREATE TABLE orders (id integer, info text, ts timestamp) PARTITION BY RANGE (ts);
CREATE TABLE orders_2024 PARTITION OF orders FOR VALUES FROM ('2024-01-01') TO ('2025-01-01');
CREATE TABLE orders_2025 PARTITION OF orders FOR VALUES FROM ('2025-01-01') TO ('2026-01-01') PARTITION BY RANGE (id);
CREATE TABLE orders_2025_low PARTITION OF orders_2025 FOR VALUES FROM (0) TO (100);

ALTER TABLE orders REPLICA IDENTITY FULL;
ALTER TABLE orders_2024 REPLICA IDENTITY FULL;
ALTER TABLE orders_2025 REPLICA IDENTITY FULL;
ALTER TABLE orders_2025_low REPLICA IDENTITY FULL;

SELECT 'init' FROM pg_create_logical_replication_slot('regression_slot', 'wal2json');

BEGIN;
INSERT INTO orders (id, info, ts) VALUES(1, 'test1', '2024-06-15');
INSERT INTO orders (id, info, ts) VALUES(2, 'test2', '2025-03-01');
UPDATE orders SET info = 'test1u' WHERE id = 1;
DELETE FROM orders WHERE id = 2;
COMMIT;

-- default (partition-root is false): partition name is used
SELECT data FROM pg_logical_slot_peek_changes('regression_slot', NULL, NULL, 'format-version', '2');
SELECT data FROM pg_logical_slot_peek_changes('regression_slot', NULL, NULL, 'format-version', '1', 'pretty-print', '1');

-- partition-root is true: root partitioned table name is used
SELECT data FROM pg_logical_slot_peek_changes('regression_slot', NULL, NULL, 'format-version', '2', 'partition-root', '1');
SELECT data FROM pg_logical_slot_peek_changes('regression_slot', NULL, NULL, 'format-version', '1', 'pretty-print', '1', 'partition-root', 'true');

-- table filtering uses the partition name if partition-root is false
SELECT data FROM pg_logical_slot_peek_changes('regression_slot', NULL, NULL, 'format-version', '2', 'filter-tables', 'public.orders_2024');
SELECT data FROM pg_logical_slot_peek_changes('regression_slot', NULL, NULL, 'format-version', '2', 'add-tables', 'public.orders');

-- table filtering uses the root partitioned table name if partition-root is true
SELECT data FROM pg_logical_slot_peek_changes('regression_slot', NULL, NULL, 'format-version', '2', 'partition-root', '1', 'filter-tables', 'public.orders');
SELECT data FROM pg_logical_slot_peek_changes('regression_slot', NULL, NULL, 'format-version', '2', 'partition-root', '1', 'add-tables', 'public.orders');
SELECT data FROM pg_logical_slot_peek_changes('regression_slot', NULL, NULL, 'format-version', '2', 'partition-root', '1', 'add-tables', 'public.orders_2024');

-- truncate uses the same rules
BEGIN;
TRUNCATE orders_2024;
COMMIT;
SELECT data FROM pg_logical_slot_peek_changes('regression_slot', NULL, NULL, 'format-version', '2', 'partition-root', '1', 'actions', 'truncate');
SELECT data FROM pg_logical_slot_get_changes('regression_slot', NULL, NULL, 'format-version', '2', 'partition-root', '1', 'actions', 'truncate', 'filter-tables', 'public.orders');

SELECT 'stop' FROM pg_drop_replication_slot('regression_slot');

DROP TABLE orders;
