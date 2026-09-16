\set ON_ERROR_STOP 1
\pset pager off

DROP EXTENSION IF EXISTS mmddyyyy CASCADE;
CREATE EXTENSION mmddyyyy;

DO $assert$
DECLARE
    type_length smallint;
    type_alignment "char";
    type_storage "char";
BEGIN
    SELECT typlen, typalign, typstorage
    INTO type_length, type_alignment, type_storage
    FROM pg_type
    WHERE oid = 'mmddyyyy'::regtype;

    IF type_length <> 10 OR type_alignment <> 'c' OR type_storage <> 'p' THEN
        RAISE EXCEPTION 'unexpected mmddyyyy layout: length %, alignment %, storage %',
            type_length, type_alignment, type_storage;
    END IF;

    IF pg_column_size('09/15/2026'::mmddyyyy) <> 10 THEN
        RAISE EXCEPTION 'mmddyyyy payload is not exactly 10 bytes';
    END IF;

    IF '09/15/2026'::mmddyyyy::date <> date '2026-09-15' OR
         date '2000-02-29'::mmddyyyy::date <> date '2000-02-29' OR
       '01/01/0001'::mmddyyyy::date <> date '0001-01-01' OR
       '12/31/9999'::mmddyyyy::date <> date '9999-12-31' THEN
        RAISE EXCEPTION 'date casts did not round trip';
    END IF;

     IF NOT ('12/31/2026'::mmddyyyy < '01/01/2027'::mmddyyyy) OR
         NOT ('01/01/2027'::mmddyyyy > '09/15/2026'::mmddyyyy) THEN
        RAISE EXCEPTION 'B-tree comparison is not chronological';
    END IF;

     IF NOT ('09/15/2026'::mmddyyyy <@ '09/*/*'::mmddyyyy_pattern) OR
         NOT ('09/15/2026'::mmddyyyy <@ '*/15/*'::mmddyyyy_pattern) OR
         '09/15/2026'::mmddyyyy <@ '10/*/*'::mmddyyyy_pattern THEN
        RAISE EXCEPTION 'pattern matching returned an incorrect result';
    END IF;

    BEGIN
        PERFORM '02/29/2025'::mmddyyyy;
        RAISE EXCEPTION 'nonexistent date was accepted';
    EXCEPTION
        WHEN datetime_field_overflow THEN NULL;
    END;

    BEGIN
        PERFORM '02/30/*'::mmddyyyy_pattern;
        RAISE EXCEPTION 'impossible partial date was accepted';
    EXCEPTION
        WHEN invalid_datetime_format THEN NULL;
    END;
END
$assert$;

CREATE TABLE events (
    id bigint GENERATED ALWAYS AS IDENTITY PRIMARY KEY,
    happened_on mmddyyyy NOT NULL
);

INSERT INTO events (happened_on)
SELECT generated_at::date::mmddyyyy
FROM generate_series(
    date '1900-01-01',
    date '2099-12-31',
    interval '1 day'
) AS series(generated_at);

CREATE INDEX events_happened_on_btree ON events (happened_on);
CREATE INDEX events_happened_on_gist ON events USING gist (happened_on);
ANALYZE events;

DO $assert$
DECLARE
    actual bigint;
BEGIN
    SELECT count(*) INTO actual FROM events;
    IF actual <> 73049 THEN
        RAISE EXCEPTION 'expected 73049 generated dates, got %', actual;
    END IF;

    SELECT count(*) INTO actual
    FROM events WHERE happened_on <@ '09/*/*';
    IF actual <> 6000 THEN
        RAISE EXCEPTION 'expected 6000 September dates, got %', actual;
    END IF;

    SELECT count(*) INTO actual
    FROM events WHERE happened_on <@ '*/15/*';
    IF actual <> 2400 THEN
        RAISE EXCEPTION 'expected 2400 fifteenths, got %', actual;
    END IF;

    SELECT count(*) INTO actual
    FROM events WHERE happened_on <@ '02/29/*';
    IF actual <> 49 THEN
        RAISE EXCEPTION 'expected 49 leap days, got %', actual;
    END IF;

     IF ('09/01/1900'::mmddyyyy <-> '09/01/2099'::mmddyyyy_pattern) >=
         ('08/01/2099'::mmddyyyy <-> '09/01/2099'::mmddyyyy_pattern) THEN
        RAISE EXCEPTION 'month does not dominate year in the distance metric';
    END IF;

     IF ('09/01/1900'::mmddyyyy <-> '09/01/2099'::mmddyyyy_pattern) >=
         ('09/02/2099'::mmddyyyy <-> '09/01/2099'::mmddyyyy_pattern) THEN
        RAISE EXCEPTION 'day does not dominate year in the distance metric';
    END IF;

     IF ('12/15/2026'::mmddyyyy <-> '01/15/2026'::mmddyyyy_pattern) <>
         ('02/15/2026'::mmddyyyy <-> '01/15/2026'::mmddyyyy_pattern) THEN
        RAISE EXCEPTION 'month distance is not circular around January';
    END IF;
END
$assert$;

SET enable_seqscan = off;

DO $assert$
DECLARE
    plan json;
BEGIN
    EXECUTE $plan$
        EXPLAIN (FORMAT JSON, COSTS OFF)
        SELECT count(*) FROM events
        WHERE happened_on <@ '09/*/*'
    $plan$ INTO plan;
    IF plan::text NOT LIKE '%events_happened_on_gist%' THEN
        RAISE EXCEPTION 'partial-date query did not produce a GiST plan: %', plan;
    END IF;

    EXECUTE $plan$
        EXPLAIN (FORMAT JSON, COSTS OFF)
        SELECT happened_on FROM events
        ORDER BY happened_on <-> '09/01/2099'
        LIMIT 50
    $plan$ INTO plan;
    IF plan::text NOT LIKE '%events_happened_on_gist%' OR
       plan::text NOT LIKE '%Index Only Scan%' THEN
        RAISE EXCEPTION 'KNN query did not produce a GiST index-only plan: %', plan;
    END IF;

    EXECUTE $plan$
        EXPLAIN (FORMAT JSON, COSTS OFF)
        SELECT happened_on FROM events
                WHERE happened_on >= '09/01/2026'
                    AND happened_on < '10/01/2026'
    $plan$ INTO plan;
    IF plan::text NOT LIKE '%events_happened_on_btree%' THEN
        RAISE EXCEPTION 'chronological range did not produce a B-tree plan: %', plan;
    END IF;
END
$assert$;

CREATE TEMP TABLE knn_index AS
SELECT happened_on::date AS happened_on,
    happened_on <-> '09/01/2099'::mmddyyyy_pattern AS distance
FROM events
ORDER BY happened_on <-> '09/01/2099'::mmddyyyy_pattern
LIMIT 50;

CREATE TEMP TABLE knn_partial_index AS
SELECT happened_on::date AS happened_on,
    happened_on <-> '09/*/2099'::mmddyyyy_pattern AS distance
FROM events
ORDER BY happened_on <-> '09/*/2099'::mmddyyyy_pattern
LIMIT 60;

SET enable_seqscan = on;
SET enable_indexscan = off;
SET enable_indexonlyscan = off;
SET enable_bitmapscan = off;

CREATE TEMP TABLE knn_sequential AS
SELECT happened_on::date AS happened_on,
    happened_on <-> '09/01/2099'::mmddyyyy_pattern AS distance
FROM events
ORDER BY happened_on <-> '09/01/2099'::mmddyyyy_pattern
LIMIT 50;

CREATE TEMP TABLE knn_partial_sequential AS
SELECT happened_on::date AS happened_on,
    happened_on <-> '09/*/2099'::mmddyyyy_pattern AS distance
FROM events
ORDER BY happened_on <-> '09/*/2099'::mmddyyyy_pattern
LIMIT 60;

DO $assert$
BEGIN
    IF EXISTS (
        (SELECT * FROM knn_index EXCEPT ALL SELECT * FROM knn_sequential)
        UNION ALL
        (SELECT * FROM knn_sequential EXCEPT ALL SELECT * FROM knn_index)
    ) THEN
        RAISE EXCEPTION 'GiST KNN result differs from sequential distance ordering';
    END IF;

    IF EXISTS (
        (SELECT * FROM knn_partial_index
         EXCEPT ALL
         SELECT * FROM knn_partial_sequential)
        UNION ALL
        (SELECT * FROM knn_partial_sequential
         EXCEPT ALL
         SELECT * FROM knn_partial_index)
    ) THEN
        RAISE EXCEPTION 'partial-pattern GiST KNN result differs from sequential ordering';
    END IF;
END
$assert$;

RESET enable_seqscan;
RESET enable_indexscan;
RESET enable_indexonlyscan;
RESET enable_bitmapscan;

SELECT 'PASS: mmddyyyy base type, B-tree, multi-page GiST, patterns, and KNN' AS result;