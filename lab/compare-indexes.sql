\set ON_ERROR_STOP 1
\pset pager off
\timing on

CREATE EXTENSION IF NOT EXISTS mdydate;
CREATE EXTENSION IF NOT EXISTS pageinspect;

DROP TABLE IF EXISTS calendar_days;
CREATE TABLE calendar_days (
    id integer GENERATED ALWAYS AS IDENTITY PRIMARY KEY,
    happened_on mdydate NOT NULL
);

\echo '\nGenerate the complete mdydate domain: 0001-01-01 through 9999-12-31.'
INSERT INTO calendar_days (happened_on)
SELECT generated_at::date::mdydate
FROM generate_series(
    date '0001-01-01',
    date '9999-12-31',
    interval '1 day'
) AS series(generated_at);

DO $assert$
DECLARE
    actual_count bigint;
BEGIN
    SELECT count(*) INTO actual_count FROM calendar_days;
    IF actual_count <> 3652059 THEN
        RAISE EXCEPTION 'expected 3652059 dates, got %', actual_count;
    END IF;
END
$assert$;

\echo '\nOne compound B-tree, ordered month then day then year.'
CREATE INDEX calendar_days_mdy_btree
ON calendar_days (
    mdydate_month(happened_on),
    mdydate_day(happened_on),
    mdydate_year(happened_on)
)
INCLUDE (happened_on);

VACUUM (ANALYZE, FREEZE) calendar_days;

\echo '\nB-tree metadata: level 0 is a leaf; higher levels contain separator keys.'
SELECT root, level, fastroot, fastlevel
FROM bt_metap('calendar_days_mdy_btree');

\echo '\nB-TREE PHASE: GiST does not exist yet, making each chosen index unambiguous.'
\echo 'INCLUDE makes the expression B-tree covering without changing its M/D/Y search order.'
\echo 'Use one worker and index-only scans so shared buffers approximate index pages visited.'
SET max_parallel_workers_per_gather = 0;
SET enable_seqscan = off;
SET enable_bitmapscan = off;

\echo '\nB-tree A: month = 9. This constrains the leading key.'
EXPLAIN (ANALYZE, BUFFERS, COSTS OFF, TIMING OFF)
SELECT count(*)
FROM calendar_days
WHERE mdydate_month(happened_on) = 9;

\echo '\nB-tree B: day = 15. With no month equality, the tree must cross every month range.'
EXPLAIN (ANALYZE, BUFFERS, COSTS OFF, TIMING OFF)
SELECT count(*)
FROM calendar_days
WHERE mdydate_day(happened_on) = 15;

\echo '\nB-tree C: day = 15 and year = 2026. It still lacks the leading month key.'
EXPLAIN (ANALYZE, BUFFERS, COSTS OFF, TIMING OFF)
SELECT count(*)
FROM calendar_days
WHERE mdydate_day(happened_on) = 15
  AND mdydate_year(happened_on) = 2026;

\echo '\nB-tree D: exact date. All three ordered keys are constrained.'
EXPLAIN (ANALYZE, BUFFERS, COSTS OFF, TIMING OFF)
SELECT count(*)
FROM calendar_days
WHERE mdydate_month(happened_on) = 9
  AND mdydate_day(happened_on) = 15
  AND mdydate_year(happened_on) = 2026;

RESET max_parallel_workers_per_gather;
RESET enable_seqscan;
RESET enable_bitmapscan;

\echo '\nBuild one multidimensional GiST over the same three logical components.'
CREATE INDEX calendar_days_mdy_gist
ON calendar_days USING gist (happened_on);

\echo '\nTable and index sizes. The B-tree INCLUDE payload makes it a fair covering scan.'
SELECT relname,
       relkind,
       relpages,
       pg_size_pretty(pg_relation_size(oid)) AS size
FROM pg_class
WHERE oid IN (
    'calendar_days'::regclass,
    'calendar_days_mdy_btree'::regclass,
    'calendar_days_mdy_gist'::regclass
)
ORDER BY relkind, relname;

\echo '\nGiST block 0 is the root. With millions of rows it is an internal page.'
SELECT *
FROM gist_page_opaque_info(get_raw_page('calendar_days_mdy_gist', 0));

\echo '\nThe root has one tuple per child page; each key is that child subtree bounding box.'
SELECT count(*) AS root_downlinks
FROM gist_page_items(
    get_raw_page('calendar_days_mdy_gist', 0),
    'calendar_days_mdy_gist'::regclass
);

\echo '\nA sample of root downlinks and their actual stored keys.'
SELECT itemoffset,
       ctid AS child_page,
       keys AS subtree_bounds
FROM gist_page_items(
    get_raw_page('calendar_days_mdy_gist', 0),
    'calendar_days_mdy_gist'::regclass
)
ORDER BY itemoffset
LIMIT 12;

\echo '\nGIST PHASE: use the same single-worker, index-only settings.'
SET max_parallel_workers_per_gather = 0;
SET enable_seqscan = off;
SET enable_bitmapscan = off;

\echo '\nGiST A: month = 9.'
EXPLAIN (ANALYZE, BUFFERS, COSTS OFF, TIMING OFF)
SELECT count(*)
FROM calendar_days
WHERE happened_on <@ '09/*/*';

\echo '\nGiST B: day = 15. Day can participate without a month constraint.'
EXPLAIN (ANALYZE, BUFFERS, COSTS OFF, TIMING OFF)
SELECT count(*)
FROM calendar_days
WHERE happened_on <@ '*/15/*';

\echo '\nGiST C: day = 15 and year = 2026. Both dimensions prune simultaneously.'
EXPLAIN (ANALYZE, BUFFERS, COSTS OFF, TIMING OFF)
SELECT count(*)
FROM calendar_days
WHERE happened_on <@ '*/15/2026';

\echo '\nGiST D: exact date.'
EXPLAIN (ANALYZE, BUFFERS, COSTS OFF, TIMING OFF)
SELECT count(*)
FROM calendar_days
WHERE happened_on <@ '09/15/2026';

RESET max_parallel_workers_per_gather;
RESET enable_seqscan;
RESET enable_bitmapscan;

\echo '\nThe comparison is structural: inspect index buffers and plans, not one timing run.'